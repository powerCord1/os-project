#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <cpu.h>
#include <debug.h>
#include <gdt.h>
#include <heap.h>
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

static struct irq_handler_entry *irq_handlers[16];

uint64_t irq_dispatch(uint64_t rsp, uint8_t irq)
{
    if (irq < 16) {
        struct irq_handler_entry *handler = irq_handlers[irq];
        while (handler) {
            if (handler->handler) {
                rsp = handler->handler(rsp, handler->ctx);
            }
            handler = handler->next;
        }
    } else {
        log_warn("irq_dispatch: Tried to call invalid IRQ: %d", irq);
    }
    return rsp;
}

void irq_install_handler(uint8_t irq, uint64_t (*handler)(uint64_t, void *), void *ctx)
{
    if (irq < 16) {
        struct irq_handler_entry *new_handler =
            malloc(sizeof(struct irq_handler_entry));
        if (!new_handler) {
            panic("Failed to allocate memory for IRQ handler");
        }

        new_handler->handler = handler;
        new_handler->ctx = ctx;
        new_handler->next = NULL;

        if (irq_handlers[irq] == NULL) {
            irq_handlers[irq] = new_handler;
        } else {
            struct irq_handler_entry *current = irq_handlers[irq];
            while (current->next) {
                current = current->next;
            }
            current->next = new_handler;
        }
        irq_clear_mask(irq);
    }
}

void irq_uninstall_handler(uint8_t irq, uint64_t (*handler)(uint64_t, void *), void *ctx)
{
    if (irq < 16) {
        struct irq_handler_entry *current = irq_handlers[irq];
        struct irq_handler_entry *prev = NULL;

        while (current) {
            if (current->handler == handler && current->ctx == ctx) {
                if (prev) {
                    prev->next = current->next;
                } else {
                    irq_handlers[irq] = current->next;
                }
                free(current);
                break;
            }
            prev = current;
            current = current->next;
        }

        if (irq_handlers[irq] == NULL) {
            irq_set_mask(irq);
        }
    }
}

extern void isr_irq0();
extern void isr_irq1();
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
    for (int i = 0; i < 16; i++) {
        irq_handlers[i] = NULL;
    }

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
        &isr_irq0,
        &isr_irq1,
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
    enable_interrupts();

    // Check if interrupts are enabled after running sti
    if (unlikely(!are_interrupts_enabled())) {
        panic("Failed to enable interrupts");
    }
}

void idt_load()
{
    __asm__ volatile("lidt %0" : : "m"(idtr) : "memory");
}