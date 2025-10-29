#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <cpu.h>
#include <debug.h>
#include <exception.h>
#include <idt.h>
#include <interrupts.h>
#include <io.h>
#include <keyboard.h>
#include <panic.h>
#include <pic.h>
#include <stdio.h>
#include <string.h>
#include <tty.h>

#define VECTOR_TABLE_SIZE 48

void enable_interrupts()
{
    __asm__("sti");
}

void disable_interrupts()
{
    __asm__("cli");
}

void idt_set_descriptor(uint8_t vector, void *isr, uint8_t flags)
{
    idt_entry_t *descriptor = &idt[vector];

    descriptor->isr_low = (uint32_t)isr & 0xFFFF;
    descriptor->kernel_cs = 0x08;   // segment selector
    descriptor->attributes = flags; // int, trap or task gate
    descriptor->isr_high = (uint32_t)isr >> 16;
    descriptor->reserved = 0;
}

void idt_set_descriptor_int(uint8_t vector, void *isr)
{
    idt_set_descriptor(vector, isr, 0x8E);
}

void idt_set_descriptor_trap(uint8_t vector, void *isr)
{
    idt_set_descriptor(vector, isr, 0x8F);
}

void exception_handler()
{
    uint32_t err_code;
    __asm__ volatile("pop %%eax\n"
                     "movl $0, %0"
                     : "=r"(err_code)
                     :
                     : "%eax");
    char hex_str[9];
    itohexa(hex_str, err_code);
    printf("error code: 0x%s", hex_str);
    panic("Critical system error");
}

void interrupt_handler()
{
    term_writestringln("interrupt triggered");
}

__attribute__((naked)) void isr_stub_keyboard()
{
    __asm__ volatile("pusha\n"
                     "mov %ds, %ax\n"
                     "push %eax\n"

                     "mov $0x10, %ax\n"
                     "mov %ax, %ds\n"
                     "mov %ax, %es\n"
                     "mov %ax, %fs\n"
                     "mov %ax, %gs\n"

                     "call handle_keypress\n"

                     "push $1\n"
                     "call pic_sendEOI\n"
                     "add $4, %esp\n"

                     "pop %eax\n"
                     "mov %ax, %ds\n"
                     "mov %ax, %es\n"
                     "mov %ax, %fs\n"
                     "mov %ax, %gs\n"

                     "popa\n"
                     "iret");
}

void isr_err_stub_common()
{
    exception_handler();
    __asm__("iret");
}

void idt_init()
{
    idtr.base = (idt_entry_t *)&idt[0];
    idtr.limit = (uint16_t)sizeof(idt_entry_t) * VECTOR_TABLE_SIZE;

    void *except_vector_table[VECTOR_TABLE_SIZE] = {
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
        NULL,
        &isr_fpu_err,
        &isr_alignment_check,
        &isr_machine_check,
        &isr_simd_floating_point,
        &isr_virtualisation,
        &isr_control_protection,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        &isr_security_protection,
        NULL,

        // interrupts
        NULL,
        &isr_stub_keyboard,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
    };

    for (uint8_t i = 0; i < VECTOR_TABLE_SIZE; i++) {
        idt_set_descriptor_int(i, except_vector_table[i]);
    }

    __asm__ volatile("lidt %0" : : "m"(idtr));
    pic_init();
    irq_clear_mask(1);
    enable_interrupts();
}