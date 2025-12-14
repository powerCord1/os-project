#include <io.h>
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>

#include <stdio.h>
#include <string.h>

#define PORT 0x3f8 // COM1

void serial_init()
{
    outb(PORT + 1, 0x00); // Disable all interrupts
    outb(PORT + 3, 0x80); // Enable DLAB (set baud rate divisor)
    outb(PORT + 0, 0x03); // Set divisor to 3 (lo byte) 38400 baud
    outb(PORT + 1, 0x00); //                  (hi byte)
    outb(PORT + 3, 0x03); // 8 bits, no parity, one stop bit
    outb(PORT + 2, 0xC7); // Enable FIFO, clear them, with 14-byte threshold
    outb(PORT + 4, 0x03); // RTS/DSR set, IRQs disabled
    outb(PORT + 4, 0x1E); // Set in loopback mode, test the serial chip
    outb(PORT + 0, 0xAE); // Test serial chip (send byte 0xAE and check if
                          // serial returns same byte)

    // Check if serial is faulty (i.e: not same byte as sent)
    if (inb(PORT + 0) != 0xAE) {
        // TODO: do something here
    }

    // If serial is not faulty set it in normal operation mode
    // (not-loopback with IRQs enabled and OUT#1 and OUT#2 bits enabled)
    outb(PORT + 4, 0x0F);
}

int serial_received()
{
    return inb(PORT + 5) & 1;
}

char read_serial()
{
    while (serial_received() == 0)
        ;

    return inb(PORT);
}

int is_transmit_empty()
{
    return inb(PORT + 5) & 0x20;
}

void write_serial(char a)
{
    while (is_transmit_empty() == 0)
        ;

    outb(PORT, a);
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

int vserial_printf(const char *restrict format, va_list parameters)
{
    return vprintf_generic(serial_putchar, format, parameters);
}

int serial_printf(const char *restrict format, ...)
{
    va_list parameters;
    va_start(parameters, format);
    int written = vserial_printf(format, parameters);
    va_end(parameters);
    return written;
}