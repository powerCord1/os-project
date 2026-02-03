#include <acpi.h>
#include <array.h>
#include <ata.h>
#include <cpu.h>
#include <debug.h>
#include <font.h>
#include <framebuffer.h>
#include <fs.h>
#include <gdt.h>
#include <heap.h>
#include <init.h>
#include <interrupts.h>
#include <keyboard.h>
#include <limine.h>
#include <pit.h>
#include <serial.h>
#include <stdio.h>
#include <tty.h>
#include <verinfo.h>

boot_task_t early_boot_tasks[] = {
    {.msg = "Init serial", .func = serial_init}, // so we can get debug info
    {.msg = "Log boot info", .func = log_boot_info},
    {.msg = "Store boot time", .func = store_boot_time},
};

boot_task_t boot_tasks[] = {
    {.msg = "Send Limine requests", .func = limine_init},
    {.msg = "Init font", .func = font_init},
    {.msg = "Init framebuffer", .func = fb_init},
    {.msg = "Init GDT", .func = gdt_init},
    {.msg = "Init IDT", .func = idt_init},
    {.msg = "Init PIT", .func = pit_init},
    {.msg = "Init CPU Features", .func = cpu_init},
    {.msg = "Init heap", .func = heap_init},
    // {.msg = "Init ACPI", .func = acpi_init},
    {.msg = "Init disk drivers and filesystems", .func = fs_init},
    {.msg = "Init keyboard", .func = kbd_init},
};

boot_task_t late_boot_tasks[] = {};

void sys_init()
{
    init_early();
    execute_tasks(boot_tasks, ARRAY_SIZE(boot_tasks));
    init_late();
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