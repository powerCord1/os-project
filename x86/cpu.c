#include <cpuid.h>

#include <cpu.h>
#include <interrupts.h>
#include <stdio.h>

void halt()
{
    __asm__ volatile("hlt");
}

__attribute__((noreturn)) void halt_catch_fire()
{
    disable_interrupts();
    while (1) {
        halt();
    }
}

char get_cpu_vendor()
{
    return 0x00;
}