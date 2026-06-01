#include <debug.h>
#include <gdt.h>
#include <smp.h>
#include <string.h>
#include <tss.h>

static tss_t tss_entries[MAX_CPUS];

void tss_init(void)
{
    for (uint32_t i = 0; i < cpu_count; i++) {
        memset(&tss_entries[i], 0, sizeof(tss_t));
        tss_entries[i].iopb_offset = sizeof(tss_t);
        gdt_install_tss(i, &tss_entries[i]);
    }
    tss_load(0);
    log_info("TSS initialized for %d CPUs", cpu_count);
}

void tss_set_rsp0(uint32_t cpu_id, uint64_t rsp0)
{
    if (cpu_id < MAX_CPUS)
        tss_entries[cpu_id].rsp0 = rsp0;
}

void tss_load(uint32_t cpu_id)
{
    uint16_t selector = GDT_TSS_BASE + cpu_id * 16;
    __asm__ volatile("ltr %0" : : "r"(selector));
}
