#include <io.h>
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>

#include <pci.h>
#include <serial.h>
#include <stdio.h>
#include <string.h>

#define COM1_BASE 0x3f8

#define BAUD_DIVISOR 0x01

serial_config_t debug_port = {0, false, false};
bool serial_initialised = false;
bool sol_enabled = false;

void serial_init()
{
    pci_device_t sol = pci_get_device(0x07, 0x00, 0x02);
    if (sol.vendor_id != 0xffff) {
        uint64_t base = pci_get_bar_address(&sol, 0);

        uint32_t bar_val =
            pci_read_dword(sol.bus, sol.device, sol.function, 0x10);
        bool is_mmio = !(bar_val & 1);

        uint16_t cmd = pci_read_word(sol.bus, sol.device, sol.function, 0x04);
        cmd |= (1 << 0) | (1 << 1) | (1 << 2); // Enable IO, mem, and bus master
        pci_write_word(sol.bus, sol.device, sol.function, 0x04, cmd);

        sol_enabled = true;
        serial_configure(base, is_mmio);
    } else {
        // Fallback to COM1 if SOL isn't found
        serial_configure(COM1_BASE, false);
    }
    serial_initialised = true;
}

static void serial_configure(uint64_t base, bool is_mmio)
{
    debug_port.base = base;
    debug_port.is_mmio = is_mmio;
    debug_port.exists = true;

    serial_out(1, 0x00);         // Disable all interrupts
    serial_out(3, 0x80);         // Enable DLAB (set baud rate divisor)
    serial_out(0, BAUD_DIVISOR); // Set divisor (lo byte)
    serial_out(1, 0x00);         // (hi byte)
    serial_out(3, 0x03);         // 8 bits, no parity, one stop bit
    serial_out(2,
               0xC7);    // Enable FIFO, clear them, with 14-byte threshold
    serial_out(4, 0x03); // RTS/DSR set, IRQs disabled
    // serial_out(4, 0x1E); // Set in loopback mode, test the serial chip
    // serial_out(0, 0xAE); // Test serial chip (send byte 0xAE and check if
    //                      // serial returns same byte)

    // Check if serial is faulty (i.e: not same byte as sent)
    // if (serial_in(0) != 0xAE) {
    //     // TODO: do something here
    // }

    // If serial is not faulty set it in normal operation mode
    // (not-loopback with IRQs enabled and OUT#1 and OUT#2 bits enabled)
    // serial_out(4, 0x0F);
}

static void serial_out(uint8_t offset, uint8_t value)
{
    if (debug_port.is_mmio) {
        *(volatile uint8_t *)(debug_port.base + offset) = value;
    } else {
        outb((uint16_t)(debug_port.base + offset), value);
    }
}

static uint8_t serial_in(uint8_t offset)
{
    if (debug_port.is_mmio) {
        return *(volatile uint8_t *)(debug_port.base + offset);
    } else {
        return inb((uint16_t)(debug_port.base + offset));
    }
}

static char read_serial()
{
    while (!serial_received())
        ;

    return serial_in(0);
}

void write_serial(char a)
{
    if (!debug_port.exists) {
        return;
    }
    if (!serial_initialised) {
        return;
    }

    while (!is_transmit_empty())
        ;

    serial_out(0, a);
}

static int serial_received()
{
    return serial_in(5) & 1;
}

static int is_transmit_empty()
{
    return serial_in(5) & 0x20;
}

void serial_writestring(const char *s)
{
    while (*s) {
        write_serial(*s++);
    }
}

void serial_writestringln(const char *s)
{
    serial_writestring(s);
    serial_writestring("\n");
}

static int serial_putchar(int c)
{
    write_serial((char)c);
    return c;
}

static int serial_putchar_wrapper(int c, void *unused)
{
    (void)unused; // this is so the function can be passed to vprintf_generic,
                  // which requires a second paramater
    return serial_putchar(c);
}

int vserial_printf(const char *restrict format, va_list parameters)
{
    return vprintf_generic(serial_putchar_wrapper, NULL, format, parameters);
}

int serial_printf(const char *restrict format, ...)
{
    va_list parameters;
    va_start(parameters, format);
    int written = vserial_printf(format, parameters);
    va_end(parameters);
    return written;
}