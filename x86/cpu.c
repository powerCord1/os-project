#include <cpuid.h>
#include <stdbool.h>
#include <stdint.h>

#include <cpu.h>
#include <debug.h>
#include <interrupts.h>
#include <sound.h>
#include <stdio.h>

void cpu_init()
{
    log_verbose("Enabling SIMD extensions");
    sse_init();
}

void halt()
{
    __asm__ volatile("hlt");
}

__attribute__((noreturn)) void halt_cf()
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
    check_beep();
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

bool is_apic_enabled()
{
    unsigned int eax, ebx, ecx, edx;
    __cpuid(1, eax, ebx, ecx, edx);
    return (edx & (1 << 9));
}