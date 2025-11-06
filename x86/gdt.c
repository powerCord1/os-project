#include <gdt.h>

gdt_entry_t gdt[GDT_ENTRIES];
gdtr_t gdtr;

void gdt_init()
{
    gdt_set_gate(0, 0, 0, 0, 0);
    gdt_set_gate(1, 0, 0xFFFFFFFF, 0x9A, 0xCF);
    gdt_set_gate(2, 0, 0xFFFFFFFF, 0x92, 0xCF);
    gdt_set_gate(3, 0, 0xFFFFF, 0x92, 0xCF); // 1MB flat segment for BIOS calls
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
    __asm__ volatile("lgdt %0\n\t"
                     "mov $0x10, %%ax\n\t"
                     "mov %%ax, %%ds\n\t"
                     "mov %%ax, %%es\n\t"
                     "mov %%ax, %%fs\n\t"
                     "mov %%ax, %%gs\n\t"
                     "mov %%ax, %%ss\n\t"
                     "ljmp $0x08, $1f\n\t" // reload CS
                     "1:\n\t"
                     :
                     : "m"(gdtr)
                     : "ax");
}