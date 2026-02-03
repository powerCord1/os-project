#include <stddef.h>

#include <interrupts.h>
#include <pit.h>
#include <timer.h>

void wait_ms(uint64_t ms)
{
    size_t eticks = pit_ticks + ms;
    while (pit_ticks < eticks) {
        enable_interrupts();
        __asm__ volatile("hlt");
        disable_interrupts();
    }
}

void wait_ns(uint64_t us)
{
}