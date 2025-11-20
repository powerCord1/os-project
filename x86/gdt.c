#include <stdint.h>

#include <debug.h>
#include <gdt.h>

gdt_entry_t gdt[GDT_ENTRIES];
gdtr_t gdtr;

static void gdt_set_gate(int num, uint8_t access, uint8_t flags)
{
    log_verbose("Setting GDT entry %d", num);
    gdt[num].limit_low = 0;
    gdt[num].base_low = 0;
    gdt[num].base_mid = 0;
    gdt[num].access = access;
    gdt[num].flags_limit_high = flags;
    gdt[num].base_high = 0;
}

void gdt_init()
{
    log_verbose("Setting GDT descriptors");
    gdt_set_gate(0, 0, 0);
    gdt_set_gate(1, 0x9A, 0x20);
    gdt_set_gate(2, 0x92, 0x00);

    gdtr.limit = sizeof(gdt) - 1;
    gdtr.base = (uintptr_t)&gdt[0];
    gdt_flush();
}

void gdt_flush()
{
    log_verbose("Flushing GDT");
    __asm__ volatile("lgdt %0\n\t"
                     "push $0x08\n\t"
                     "lea 1f(%%rip), %%rax\n\t"
                     "push %%rax\n\t"
                     "retfq\n\t"
                     "1:\n\t"
                     "mov $0x10, %%ax\n\t"
                     "mov %%ax, %%ds\n\t"
                     "mov %%ax, %%es\n\t"
                     "mov %%ax, %%fs\n\t"
                     "mov %%ax, %%gs\n\t"
                     "mov %%ax, %%ss\n\t"
                     :
                     : "m"(gdtr)
                     : "rax", "memory");
}