#include <stdint.h>
#include <string.h>

#include <debug.h>
#include <gdt.h>
#include <tss.h>

gdt_entry_t gdt[GDT_ENTRIES];
gdtr_t gdtr;

static void gdt_set_gate(int num, uint8_t access, uint8_t flags)
{
    gdt[num].limit_low = 0;
    gdt[num].base_low = 0;
    gdt[num].base_mid = 0;
    gdt[num].access = access;
    gdt[num].flags_limit_high = flags;
    gdt[num].base_high = 0;
}

void gdt_init()
{
    memset(gdt, 0, sizeof(gdt));

    gdt_set_gate(0, 0, 0);
    gdt_set_gate(1, 0x9A, 0x20);
    gdt_set_gate(2, 0x92, 0x00);

    gdtr.limit = sizeof(gdt) - 1;
    gdtr.base = (uintptr_t)&gdt[0];
    gdt_flush();
}

void gdt_install_tss(int cpu_id, tss_t *tss)
{
    int idx = 3 + cpu_id * 2;
    uint64_t base = (uint64_t)tss;
    uint32_t limit = sizeof(tss_t) - 1;

    // Low 8 bytes (normal system segment descriptor)
    gdt[idx].limit_low = limit & 0xFFFF;
    gdt[idx].base_low = base & 0xFFFF;
    gdt[idx].base_mid = (base >> 16) & 0xFF;
    gdt[idx].access = 0x89; // present, 64-bit TSS (available)
    gdt[idx].flags_limit_high = (limit >> 16) & 0x0F;
    gdt[idx].base_high = (base >> 24) & 0xFF;

    // High 8 bytes (upper 32 bits of base)
    uint32_t *high = (uint32_t *)&gdt[idx + 1];
    high[0] = (uint32_t)(base >> 32);
    high[1] = 0;
}

void gdt_flush()
{
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

void gdt_load()
{
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
