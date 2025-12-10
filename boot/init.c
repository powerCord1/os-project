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
#include <tty.h>

void sys_init()
{
    serial_init(); // init serial early so we can get debug info

    log_info("Build version: %s", BUILD_VERSION);
    log_info("Build time: %s", BUILD_TIME);
    log_info("Commit: %s", COMMIT);

    store_boot_time();
    log_info("Boot time: %d/%d/%d %d:%d:%d", boot_time.day, boot_time.month,
             boot_time.year, boot_time.hour, boot_time.minute,
             boot_time.second);

    log_verbose("Initializing Limine");
    limine_init();
    log_verbose("Initializing PSF font");
    font_init();
    log_verbose("Initializing framebuffer");
    fb_init();
    log_verbose("Initializing CPU features");
    cpu_init();
    log_verbose("Initializing GDT");
    gdt_init();
    log_verbose("Initializing IDT");
    idt_init();
    log_verbose("Initializing heap");
    heap_init();
    log_verbose("Initialising ATA driver");
    ata_init();
    log_verbose("Automounting file systems");
    fs_init();
    log_verbose("Initializing PIT");
    pit_init(1000);
    log_verbose("Initializing keyboard");
    kbd_init();
    log_verbose("Enabling interrupts");
    enable_interrupts();
    log_verbose("Waiting for an interrupt");
    wait_for_interrupt();
}

void store_boot_time()
{
    cmos_get_datetime(&boot_time);
}