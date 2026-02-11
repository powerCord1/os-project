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

uint64_t exception_dispatch(uint64_t rsp, uint8_t vector)
{
    if (vector < 32) {
        if (exception_handlers[vector]) {
            exception_handlers[vector]((interrupt_frame_t *)rsp);
        } else {
            log_err("Unhandled exception %d: %s", vector, exceptions[vector]);
            // Fatal exceptions if no handler is present
            if (vector == 8 || (vector >= 10 && vector <= 14) || vector == 17 ||
                vector == 20 || vector == 30) {
                panic("Fatal unhandled exception");
            }
        }
    }
    return rsp;
}

void exception_install_handler(uint8_t vector,
                               void (*handler)(interrupt_frame_t *))
{
    if (vector < 32) {
        exception_handlers[vector] = handler;
    }
}

void exception_uninstall_handler(uint8_t vector)
{
    if (vector < 32) {
        exception_handlers[vector] = NULL;
    }
}

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

void irq_install_handler(uint8_t irq, uint64_t (*handler)(uint64_t, void *),
                         void *ctx)
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

void irq_uninstall_handler(uint8_t irq, uint64_t (*handler)(uint64_t, void *),
                           void *ctx)
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

extern void isr_div_err();
extern void isr_debug();
extern void isr_nmi_int();
extern void isr_breakpoint();
extern void isr_overflow();
extern void isr_bound_range();
extern void isr_invalid_opcode();
extern void isr_fpu_not_available();
extern void isr_double_fault();
extern void isr_coprocessor_seg_overrun();
extern void isr_invalid_tss();
extern void isr_invalid_segment();
extern void isr_seg_fault();
extern void isr_gpf();
extern void isr_page_fault();
extern void isr_unhandled();
extern void isr_fpu_err();
extern void isr_alignment_check();
extern void isr_machine_check();
extern void isr_simd_floating_point();
extern void isr_virtualisation();
extern void isr_control_protection();
extern void isr_res22();
extern void isr_res23();
extern void isr_res24();
extern void isr_res25();
extern void isr_res26();
extern void isr_res27();
extern void isr_res28();
extern void isr_vmm_comm();
extern void isr_security_protection();
extern void isr_res31();

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

    for (int i = 0; i < 32; i++) {
        exception_handlers[i] = NULL;
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
        &isr_res22,
        &isr_res23,
        &isr_res24,
        &isr_res25,
        &isr_res26,
        &isr_res27,
        &isr_res28,
        &isr_vmm_comm,
        &isr_security_protection,
        &isr_res31,

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
    register_exceptions();
    log_verbose("Initialising PIC");
    pic_init();
    enable_interrupts();

    // Check if interrupts are enabled after running sti
    if (unlikely(!are_interrupts_enabled())) {
        panic("Failed to enable interrupts");
    }
}

void register_exceptions()
{
    exception_install_handler(14, page_fault_handler);
    exception_install_handler(8, double_fault_handler);
}

void idt_load()
{
    __asm__ volatile("lidt %0" : : "m"(idtr) : "memory");
}