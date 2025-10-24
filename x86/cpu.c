#include <cpuid.h>

#include <cpu.h>
#include <stdio.h>

void halt(void)
{
    __asm__ volatile("hlt");
}

char get_cpu_vendor(void)
{
    return 0x00;
}