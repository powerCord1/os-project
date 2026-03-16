#include <cpu.h>
#include <debug.h>
#include <gdt.h>
#include <heap.h>
#include <idt.h>
#include <interrupts.h>
#include <lapic.h>
#include <limine.h>
#include <scheduler.h>
#include <smp.h>
#include <string.h>
#include <tss.h>

#define AP_STACK_SIZE 16384

cpu_t cpus[MAX_CPUS];
uint32_t cpu_count = 0;

static volatile uint32_t aps_booted = 0;

static void ap_idle(void *arg)
{
    (void)arg;
    while (1)
        __asm__ volatile("hlt");
}

static void ap_entry(struct limine_mp_info *info)
{
    uint32_t id = (uint32_t)info->extra_argument;
    cpu_t *cpu = &cpus[id];

    gdt_load();
    idt_load();
    tss_load(id);

    wrmsr(MSR_GS_BASE, (uint64_t)cpu);

    lapic_init();
    lapic_timer_init(1000);

    cpu->idle_thread = thread_create(ap_idle, NULL);
    cpu->current_thread = cpu->idle_thread;
    cpu->online = true;

    __atomic_add_fetch(&aps_booted, 1, __ATOMIC_SEQ_CST);

    log_info("CPU %d online (LAPIC ID %d)", id, cpu->lapic_id);

    enable_interrupts();

    while (1)
        __asm__ volatile("hlt");
}

void smp_init(void)
{
    struct limine_mp_response *mp = mp_request.response;
    if (!mp) {
        log_warn("No MP response from Limine, single CPU mode");
        cpu_count = 1;
        cpus[0].lapic_id = lapic_id();
        cpus[0].cpu_id = 0;
        cpus[0].online = true;
        wrmsr(MSR_GS_BASE, (uint64_t)&cpus[0]);
        return;
    }

    cpu_count = mp->cpu_count;
    if (cpu_count > MAX_CPUS)
        cpu_count = MAX_CPUS;

    memset(cpus, 0, sizeof(cpus));

    for (uint32_t i = 0; i < cpu_count; i++) {
        struct limine_mp_info *cpu_info = mp->cpus[i];
        cpus[i].lapic_id = cpu_info->lapic_id;
        cpus[i].cpu_id = i;
        cpus[i].online = false;

        if (cpu_info->lapic_id == mp->bsp_lapic_id) {
            cpus[i].online = true;
            wrmsr(MSR_GS_BASE, (uint64_t)&cpus[i]);
        }
    }

    log_info("SMP: %d CPUs detected (BSP LAPIC ID %d)", cpu_count,
             mp->bsp_lapic_id);
}

void smp_start_aps(void)
{
    struct limine_mp_response *mp = mp_request.response;
    if (!mp || cpu_count <= 1)
        return;

    for (uint32_t i = 0; i < cpu_count; i++) {
        struct limine_mp_info *cpu_info = mp->cpus[i];
        if (cpu_info->lapic_id == mp->bsp_lapic_id)
            continue;

        void *stack = malloc(AP_STACK_SIZE);
        if (!stack) {
            log_err("Failed to allocate AP stack for CPU %d", i);
            continue;
        }
        cpus[i].tss_rsp0 = (uint64_t)stack + AP_STACK_SIZE;

        cpu_info->extra_argument = i;
        __atomic_store_n(&cpu_info->goto_address, ap_entry, __ATOMIC_SEQ_CST);
    }

    // Wait for APs to come online
    uint32_t expected = cpu_count - 1;
    while (__atomic_load_n(&aps_booted, __ATOMIC_SEQ_CST) < expected)
        cpu_pause();

    log_info("SMP: All %d APs online", expected);
}

cpu_t *smp_get_current_cpu(void)
{
    return (cpu_t *)rdmsr(MSR_GS_BASE);
}

uint32_t smp_get_cpu_count(void)
{
    return cpu_count;
}
