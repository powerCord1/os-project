#pragma once

#include <stdint.h>

struct interrupt_frame {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rax, rbx, rcx, rdx, rsi, rdi;
    uint64_t error_code;
    uint64_t rip, cs, rflags, rsp, ss;
};

void exception_handler(struct interrupt_frame *frame);

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
extern void isr_fpu_err();
extern void isr_alignment_check();
extern void isr_machine_check();
extern void isr_simd_floating_point();
extern void isr_virtualisation();
extern void isr_control_protection();
extern void isr_unhandled();
extern void isr_security_protection();
extern void isr_pit();
extern void isr_keyboard();

static const char *exceptions[] = {
    "Divide Error",
    "Debug",
    "Non Maskable Interrupt",
    "Breakpoint",
    "Overflow",
    "Bound Range Exceeded",
    "Invalid Opcode",
    "Device Not Available (FPU)",
    "Double Fault",
    "Coprocessor Segment Overrun",
    "Invalid TSS",
    "Segment Not Present",
    "Stack-Segment Fault",
    "General Protection Fault",
    "Page Fault",
    "x87 FPU Floating-Point Error",
    "Alignment Check",
    "Machine Check",
    "SIMD Floating-Point Exception",
    "Virtualization Exception",
    "Control Protection Exception",
    "Reserved",
    "Security Exception",
};
