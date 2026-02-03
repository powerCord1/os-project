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
#include <prediction.h>
#include <stdio.h>
#include <string.h>
#include <tty.h>

#define VECTOR_TABLE_SIZE 48

idt_entry_t idt[IDT_ENTRIES];
idtr_t idtr;

void irq_dispatch(uint8_t irq)
{
    if (irq < 16 && irq_handlers[irq].handler) {
        irq_handlers[irq].handler(irq_handlers[irq].ctx);
    } else {
        log_warn("irq_dispatch: Tried to call invalid IRQ: %d", irq);
    }
}

void irq_install_handler(uint8_t irq, void (*handler)(void *), void *ctx)
{
    if (irq < 16) {
        irq_handlers[irq].handler = handler;
        irq_handlers[irq].ctx = ctx;
        irq_clear_mask(irq);
    }
}

// void irq_uninstall_handler()

extern void isr_irq2();
extern void isr_irq3();
extern void isr_irq4();
extern void isr_irq5();
extern void isr_irq6();
extern void isr_irq7();
extern void isr_irq8();
extern void isr_irq9();
extern void isr_irq10();
extern void isr_irq11();
extern void isr_irq12();
extern void isr_irq13();
extern void isr_irq14();
extern void isr_irq15();

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
    uint64_t flags;
    __asm__ volatile("pushfq\n\t"
                     "pop %0"
                     : "=rm"(flags));
    return (flags >> 9) & 1;
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
        &isr_irq2,
        &isr_irq3,
        &isr_irq4,
        &isr_irq5,
        &isr_irq6,
        &isr_irq7,
        &isr_irq8,
        &isr_irq9,
        &isr_irq10,
        &isr_irq11,
        &isr_irq12,
        &isr_irq13,
        &isr_irq14,
        &isr_irq15,
    };

    log_verbose("Setting IDT descriptors");
    for (size_t i = 0; i < VECTOR_TABLE_SIZE; i++) {
        if (i >= IDT_ENTRIES) {
            panic("Vector table too large for IDT");
        }
        idt_set_descriptor(i, vector_table[i], 0x8E);
        io_wait(); // Prevent synchronisation issues
    }

    log_verbose("Loading IDT");
    idt_load();
    log_verbose("Initialising PIC");
    pic_init();
    log_verbose("Clearing PIC masks");
    irq_clear_mask(IRQ_TYPE_PIT);
    irq_clear_mask(IRQ_TYPE_KEYBOARD);
    enable_interrupts();
    check_interrupts();
}

void idt_load()
{
    __asm__ volatile("lidt %0" : : "m"(idtr) : "memory");
}

void check_interrupts()
{
    // Check if interrupts are enabled after running sti
    if (unlikely(!are_interrupts_enabled())) {
        panic("Failed to enable interrupts");
    }

    // Check if the system is receiving interrupts
    uint64_t i = 0;
    uint64_t init_pit_ticks = pit_ticks;
    while (pit_ticks == init_pit_ticks) {
        // TODO: panic after 2 seconds by getting timestamp, as CPU speed can
        // change
        if (++i == 10000000000) {
            panic("Interrupt check timed out");
        }
    }
}