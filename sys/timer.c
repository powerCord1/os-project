#include <stddef.h>

#include <cpu.h>
#include <interrupts.h>
#include <pit.h>
#include <timer.h>

static uint64_t tsc_freq_hz = 0;
static uint64_t tsc_at_boot = 0;

void wait_ms(uint64_t ms)
{
    if (!pit_initialised) {
        return;
    }
    size_t eticks = pit_ticks + ms;
    while (pit_ticks < eticks) {
        enable_interrupts();
        __asm__ volatile("hlt");
        disable_interrupts();
    }
}

void wait_us(uint64_t us)
{
    wait_ns(us * 1000);
}

void wait_ns(uint64_t ns)
{
    if (!pit_initialised) {
        return;
    }
    uint64_t start = get_ts();
    uint64_t end = start + ns;
    while (get_ts() < end) {
        cpu_pause();
    }
}