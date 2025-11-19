#include <cpuid.h>
#include <stdbool.h>
#include <stdint.h>

#include <cpu.h>
#include <interrupts.h>
#include <pit.h>
#include <stdio.h>

void cpu_init()
{
    sse_init();
}

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

void idle()
{
    pit_check_beep();
    halt();
}

bool is_pe_enabled()
{
    uint64_t cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    return (cr0 & 1);
}

void sse_init()
{
    uint64_t cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 &= ~(1 << 2); // Clear EM bit
    cr0 |= (1 << 1);  // Set MP bit
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0));

    uint64_t cr4;
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= (1 << 9);  // Set OSFXSR bit
    cr4 |= (1 << 10); // Set OSXMMEXCPT bit
    __asm__ volatile("mov %0, %%cr4" : : "r"(cr4));
}