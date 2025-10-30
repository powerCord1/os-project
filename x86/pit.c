#include <debug.h>
#include <io.h>
#include <pit.h>
#include <stdio.h>

#define PIT_BASE_FREQUENCY 1193182
#define PIT_CMD_PORT 0x43
#define PIT_CHANNEL0_DATA_PORT 0x40

size_t pit_ticks = 0;

void pit_init(uint32_t frequency)
{
    if (frequency == 0) {
        log_warn("PIT frequency is zero");
        return;
    }

    uint16_t divisor = PIT_BASE_FREQUENCY / frequency;

    outb(PIT_CMD_PORT, 0x36);

    outb(PIT_CHANNEL0_DATA_PORT, (uint8_t)(divisor & 0xFF));
    outb(PIT_CHANNEL0_DATA_PORT, (uint8_t)((divisor >> 8) & 0xFF));
}

void pit_handler()
{
    pit_ticks++;
    if (pit_ticks % 100 == 0) {
        log_info("PIT ticks: %u", pit_ticks);
    }
}