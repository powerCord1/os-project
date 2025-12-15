#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <cpu.h>
#include <debug.h>
#include <gdt.h>
#include <idt.h>
#include <init.h>
#include <interrupts.h>
#include <io.h>
#include <isr.h>
#include <keyboard.h>
#include <panic.h>
#include <pic.h>
#include <pit.h>
#include <stdio.h>
#include <string.h>
#include <tty.h>

#define VECTOR_TABLE_SIZE 48

idt_entry_t idt[IDT_ENTRIES];
idtr_t idtr;

void enable_interrupts()
{
    __asm__("sti");
}

void disable_interrupts()
{
    __asm__("cli");
}

bool are_interrupts_enabled()
{
    unsigned long flags;
    __asm__ volatile("pushf\n\t"
                     "pop %0"
                     : "=g"(flags));
    return flags & (1 << 9);
}

void idt_set_descriptor(uint8_t vector, void *isr, uint8_t flags)
{
    idt_entry_t *descriptor = &idt[vector];
    uint64_t isr_addr = (uint64_t)isr;

    descriptor->isr_low = isr_addr & 0xFFFF;
    descriptor->kernel_cs = GDT_CODE_SEGMENT;
    descriptor->ist = 0;
    descriptor->attributes = flags;
    descriptor->isr_mid = (isr_addr >> 16) & 0xFFFF;
    descriptor->isr_high = (isr_addr >> 32) & 0xFFFFFFFF;
    descriptor->reserved = 0;
}

void idt_init()
{
    if (is_apic_enabled()) {
        log_verbose("APIC enabled");
    }

    idtr.base = (uint64_t)&idt[0];
    idtr.limit = (uint16_t)sizeof(idt_entry_t) * IDT_ENTRIES - 1;

    void *vector_table[VECTOR_TABLE_SIZE] = {
        // exceptions
        &isr_div_err,
        &isr_debug,
        &isr_nmi_int,
        &isr_breakpoint,
        &isr_overflow,
        &isr_bound_range,
        &isr_invalid_opcode,
        &isr_fpu_not_available,
        &isr_double_fault,
        &isr_coprocessor_seg_overrun,
        &isr_invalid_tss,
        &isr_invalid_segment,
        &isr_seg_fault,
        &isr_gpf,
        &isr_page_fault,
        &isr_unhandled,
        &isr_fpu_err,
        &isr_alignment_check,
        &isr_machine_check,
        &isr_simd_floating_point,
        &isr_virtualisation,
        &isr_control_protection,
        &isr_unhandled,
        &isr_unhandled,
        &isr_unhandled,
        &isr_unhandled,
        &isr_unhandled,
        &isr_unhandled,
        &isr_unhandled,
        &isr_unhandled,
        &isr_security_protection,
        &isr_unhandled,

        // interrupts
        &isr_pit,
        &isr_keyboard,
        &isr_unhandled,
        &isr_unhandled,
        &isr_unhandled,
        &isr_unhandled,
        &isr_unhandled,
        &isr_unhandled,
        &isr_unhandled,
        &isr_unhandled,
        &isr_unhandled,
        &isr_unhandled,
        &isr_unhandled,
        &isr_unhandled,
        &isr_unhandled,
        &isr_unhandled,
    };

    log_verbose("Setting IDT descriptors");
    for (size_t i = 0; i < VECTOR_TABLE_SIZE; i++) {
        log_verbose("Setting descriptor %d", i);
        if (i >= IDT_ENTRIES) {
            panic("vector table too large for IDT");
        }
        idt_set_descriptor(i, vector_table[i], 0x8E);
    }

    log_verbose("Loading IDT");
    idt_load();
    log_verbose("Initialising PIC");
    pic_init();
    log_verbose("Clearing PIC masks");
    irq_clear_mask(IRQ_TYPE_PIT);
    irq_clear_mask(IRQ_TYPE_KEYBOARD);
}

void idt_load()
{
    __asm__ volatile("lidt %0" : : "m"(idtr) : "memory");
}

void wait_for_interrupt()
{
    uint64_t i = 0;
    uint64_t init_pit_ticks = pit_ticks;
    while (pit_ticks == init_pit_ticks) {
        // TODO: panic after 2 seconds by getting CMOS info, as CPU speed can
        // change
        if (++i == 10000000000) {
            panic("Timed out while waiting for interrupt");
        }
    }
}