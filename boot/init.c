#include <acpi.h>
#include <array.h>
#include <ata.h>
#include <cpu.h>
#include <debug.h>
#include <fat32.h>
#include <font.h>
#include <framebuffer.h>
#include <fs.h>
#include <gdt.h>
#include <heap.h>
#include <init.h>
#include <interrupts.h>
#include <ioapic.h>
#include <keyboard.h>
#include <lapic.h>
#include <limine.h>
#include <mouse.h>
#include <pic.h>
#include <pit.h>
#include <pmm.h>
#include <process.h>
#include <scheduler.h>
#include <serial.h>
#include <smp.h>
#include <stdio.h>
#include <tss.h>
#include <tty.h>
#include <verinfo.h>
#include <vmm.h>
#include <wasm_runner.h>

bool is_system_initialised = false;

boot_task_t early_boot_tasks[] = {
    {.msg = "Set CPU features", .func = cpu_init},
#if EARLY_SERIAL
    {.msg = "Init serial", .func = serial_init},
#endif
    {.msg = "Log boot info", .func = log_boot_info},
    {.msg = "Store boot time", .func = store_boot_time},
};

boot_task_t boot_tasks[] = {
    {.msg = "Send Limine requests", .func = limine_init},
    {.msg = "Init font", .func = font_init},
    {.msg = "Init framebuffer", .func = fb_init},
#if !EARLY_SERIAL
    {.msg = "Init serial", .func = serial_init},
#endif
    {.msg = "Init heap", .func = heap_init},
    {.msg = "Init GDT", .func = gdt_init},
    {.msg = "Init IDT", .func = idt_init},
    {.msg = "Init PMM", .func = pmm_init},
    {.msg = "Init VMM", .func = vmm_init},
    {.msg = "Init PIT", .func = pit_init},
    {.msg = "Init TSC", .func = tsc_init},
#if ACPI_ENABLED
    {.msg = "Init ACPI", .func = acpi_init},
    {.msg = "Parse MADT", .func = acpi_parse_madt},
#endif
    {.msg = "Init SMP", .func = smp_init},
    {.msg = "Init LAPIC", .func = lapic_init},
    {.msg = "Init IOAPIC", .func = ioapic_setup},
    {.msg = "Init TSS", .func = tss_init},
    {.msg = "Register filesystem drivers", .func = fat32_init},
    {.msg = "Init disk drivers and filesystems", .func = fs_init},
    {.msg = "Init mouse", .func = mouse_init},
    {.msg = "Init keyboard", .func = kbd_init},
    {.msg = "Init TTY", .func = tty_init},
    {.msg = "Init scheduler", .func = scheduler_init},
    {.msg = "Init process table", .func = proc_table_init},
    {.msg = "Init WASM runtime", .func = wasm_runtime_setup},
};

boot_task_t late_boot_tasks[] = {
    {.msg = "Start LAPIC timer", .func = lapic_timer_start},
    {.msg = "Start APs", .func = smp_start_aps},
    {.msg = "Start scheduler", .func = scheduler_start},
};

void sys_init()
{
    init_early();
    execute_tasks(boot_tasks, ARRAY_SIZE(boot_tasks));
    init_late();
    is_system_initialised = true;
}

void init_early()
{
    execute_tasks(early_boot_tasks, ARRAY_SIZE(early_boot_tasks));
}

void init_late()
{
    log_verbose("Running late initialisation");
    execute_tasks(late_boot_tasks, ARRAY_SIZE(late_boot_tasks));
}

void store_boot_time()
{
    cmos_get_datetime(&boot_time);
    log_info("Boot time: %d/%d/%d %d:%d:%d", boot_time.day, boot_time.month,
             boot_time.year, boot_time.hour, boot_time.minute,
             boot_time.second);
}

void log_boot_info()
{
    log_info("Build version: %s", verinfo.version);
    log_info("Build time: %s", verinfo.buildtime);
    log_info("Commit: %s", verinfo.commit);
}

void execute_tasks(boot_task_t *tasks, size_t num_tasks)
{
    for (size_t i = 0; i < num_tasks; i++) {
        printf("%s\n", tasks[i].msg);
        tasks[i].func();
    }
}
