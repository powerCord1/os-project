#include <stdbool.h>

#include <debug.h>
#include <interrupts.h>
#include <io.h>
#include <pit.h>
#include <stdio.h>

#define PIT_FREQUENCY 1000

volatile uint64_t pit_ticks = 0;

void pit_init()
{
    uint32_t frequency = PIT_FREQUENCY;
    log_verbose("Setting PIT frequency to %d", frequency);
    if (frequency == 0) {
        log_warn("PIT frequency is zero");
        return;
    }

    uint16_t divisor = PIT_BASE_FREQUENCY / frequency;

    //
    outb(PIT_CMD_PORT, 0x36);

    outb(PIT_CHANNEL0_DATA_PORT, (uint8_t)(divisor & 0xFF));
    outb(PIT_CHANNEL0_DATA_PORT, (uint8_t)((divisor >> 8) & 0xFF));
}

void pit_handler()
{
    if (++pit_ticks == UINT64_MAX) {
        log_warn("PIT tick overflow");
        log_warn("gang wtf reboot now");
    }
}

void pit_wait_ms(size_t ticks)
{
    size_t eticks = pit_ticks + ticks;
    while (pit_ticks < eticks) {
        enable_interrupts();
        __asm__ volatile("hlt");
        disable_interrupts();
    }
}