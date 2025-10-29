#include <gdt.h>

void gdt_init()
{
    gdt_set_gate(0, 0, 0, 0, 0);
    gdt_set_gate(1, 0, 0xFFFFFFFF, 0x9A, 0xCF);
    gdt_set_gate(2, 0, 0xFFFFFFFF, 0x92, 0xCF);
    gdtr.limit = (sizeof(gdt)) - 1;
    gdtr.base = &gdt[0];
    gdt_flush();
}

void gdt_set_gate(int num, unsigned long base, unsigned long limit,
                  unsigned char access, unsigned char flags_limit_high)
{
    gdt[num].base_low = (base & 0xFFFF);
    gdt[num].base_mid = (base >> 16) & 0xFF;
    gdt[num].base_high = (base >> 24) & 0xFF;
    gdt[num].limit_low = (limit & 0xFFFF);
    gdt[num].flags_limit_high = ((limit >> 16) & 0x0F);
    gdt[num].flags_limit_high |= (flags_limit_high & 0xF0);
    gdt[num].access = access;
}

void gdt_flush()
{
    __asm__ volatile("lidt %0" : : "m"(gdtr));
    __asm__ volatile("ljmp $0x08, %0" : : "i"(&reload_cs));
}

void reload_cs()
{
    __asm__ volatile("mov %%ds, %0\n"
                     "mov %%es, %0\n"
                     "mov %%fs, %0\n"
                     "mov %%gs, %0\n"
                     "mov %%ss, %0\n"
                     :
                     : "r"(0x10));
}