#include <cpu.h>
#include <debug.h>
#include <framebuffer.h>
#include <gdt.h>
#include <heap.h>
#include <init.h>
#include <interrupts.h>
#include <limine.h>
#include <pit.h>
#include <serial.h>
#include <tty.h>

void sys_init()
{
    serial_init();

    log_verbose("Initializing Limine");
    limine_init();
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
    log_verbose("Initializing PIT");
    pit_init(1000);
}