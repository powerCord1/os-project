#include <stdbool.h>

#include <debug.h>
#include <interrupts.h>
#include <io.h>
#include <panic.h>
#include <pit.h>
#include <prediction.h>
#include <stdio.h>

#include <scheduler.h>

#define PIT_FREQUENCY 1000

volatile uint64_t pit_ticks = 0;
bool pit_initialised = false;

void pit_init()
{
    uint32_t frequency = PIT_FREQUENCY;
    log_verbose("Setting PIT frequency to %d", frequency);
    if (frequency == 0) {
        log_warn("PIT frequency is zero");
        return;
    }

    uint16_t divisor = PIT_BASE_FREQUENCY / frequency;

    outb(PIT_CMD_PORT, 0x36);

    outb(PIT_CHANNEL0_DATA_PORT, (uint8_t)(divisor & 0xFF));
    outb(PIT_CHANNEL0_DATA_PORT, (uint8_t)((divisor >> 8) & 0xFF));

    log_verbose("Installing PIT interrupt handler");
    irq_install_handler(IRQ_TYPE_PIT,
                        (uint64_t (*)(uint64_t, void *))pit_handler, NULL);

    log_verbose("Checking PIT is triggering interrupts");
    pit_check();
    pit_initialised = true;
}

uint64_t pit_handler(uint64_t rsp)
{
    unlikely_warn(++pit_ticks == UINT64_MAX,
                  "PIT tick overflow, system may be unstable");

    return scheduler_schedule(rsp);
}

void pit_check()
{
    uint64_t i = 0;
    uint64_t init_pit_ticks = pit_ticks;
    while (pit_ticks == init_pit_ticks) {
        // TODO: panic after 2 seconds by getting timestamp, as CPU speed
        // can change
        if (++i == 10000000000) {
            panic("PIT check timed out");
        }
    }
}