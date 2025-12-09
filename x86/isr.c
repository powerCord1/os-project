#include <stdint.h>

#include <debug.h>
#include <isr.h>
#include <keyboard.h>
#include <panic.h>
#include <pic.h>
#include <pit.h>
#include <stdio.h>
#include <string.h>

#define ISR_STUB(n, string)                                                    \
    __attribute__((naked)) void isr_##n()                                      \
    {                                                                          \
        __asm__ volatile("push %rdi\n"                                         \
                         "push %rsi\n"                                         \
                         "push %rdx\n"                                         \
                         "push %rcx\n"                                         \
                         "push %rbx\n"                                         \
                         "push %rax\n"                                         \
                         "push %rbp\n"                                         \
                         "push %r8\n"                                          \
                         "push %r9\n"                                          \
                         "push %r10\n"                                         \
                         "push %r11\n"                                         \
                         "push %r12\n"                                         \
                         "push %r13\n"                                         \
                         "push %r14\n"                                         \
                         "push %r15\n"                                         \
                                                                               \
                         "pop %r15\n"                                          \
                         "pop %r14\n"                                          \
                         "pop %r13\n"                                          \
                         "pop %r12\n"                                          \
                         "pop %r11\n"                                          \
                         "pop %r10\n"                                          \
                         "pop %r9\n"                                           \
                         "pop %r8\n"                                           \
                         "pop %rbp\n"                                          \
                         "pop %rax\n"                                          \
                         "pop %rbx\n"                                          \
                         "pop %rcx\n"                                          \
                         "pop %rdx\n"                                          \
                         "pop %rsi\n"                                          \
                         "pop %rdi\n"                                          \
                         "iretq\n");                                           \
    }

#define ISR_STUB_ERR(n, string)                                                \
    __attribute__((naked)) void isr_##n()                                      \
    {                                                                          \
        panic(string);                                                         \
    }

ISR_STUB(div_err, exceptions[0]);
ISR_STUB(debug, exceptions[1]);
ISR_STUB(nmi_int, exceptions[2]);
ISR_STUB(breakpoint, exceptions[3]);
ISR_STUB(overflow, exceptions[4]);
ISR_STUB(bound_range, exceptions[5]);
ISR_STUB(invalid_opcode, exceptions[6]);
ISR_STUB(fpu_not_available, exceptions[7]);
ISR_STUB_ERR(double_fault, exceptions[8]);
ISR_STUB(coprocessor_seg_overrun, exceptions[9]);
ISR_STUB_ERR(invalid_tss, exceptions[10]);
ISR_STUB_ERR(invalid_segment, exceptions[11]);
ISR_STUB_ERR(seg_fault, exceptions[12]);
ISR_STUB_ERR(gpf, exceptions[13]);
ISR_STUB_ERR(page_fault, exceptions[14]);
ISR_STUB(fpu_err, exceptions[15]);
ISR_STUB_ERR(alignment_check, exceptions[16]);
ISR_STUB(machine_check, exceptions[17]);
ISR_STUB(simd_floating_point, exceptions[18]);
ISR_STUB(virtualisation, exceptions[19]);
ISR_STUB_ERR(control_protection, exceptions[20]);
ISR_STUB(unhandled, exceptions[21]);
ISR_STUB_ERR(security_protection, exceptions[22]);

#define IRQ_HANDLER(n, handler, irq_num)                                       \
    __attribute__((naked)) void n()                                            \
    {                                                                          \
        __asm__ volatile("push %rdi\n"                                         \
                         "push %rsi\n"                                         \
                         "push %rdx\n"                                         \
                         "push %rcx\n"                                         \
                         "push %rbx\n"                                         \
                         "push %rax\n"                                         \
                         "push %rbp\n"                                         \
                         "push %r8\n"                                          \
                         "push %r9\n"                                          \
                         "push %r10\n"                                         \
                         "push %r11\n"                                         \
                         "push %r12\n"                                         \
                         "push %r13\n"                                         \
                         "push %r14\n"                                         \
                         "push %r15\n"                                         \
                         "call " #handler "\n"                                 \
                         "movb $" #irq_num ", %al\n"                           \
                         "call pic_sendEOI\n"                                  \
                         "pop %r15\n"                                          \
                         "pop %r14\n"                                          \
                         "pop %r13\n"                                          \
                         "pop %r12\n"                                          \
                         "pop %r11\n"                                          \
                         "pop %r10\n"                                          \
                         "pop %r9\n"                                           \
                         "pop %r8\n"                                           \
                         "pop %rbp\n"                                          \
                         "pop %rax\n"                                          \
                         "pop %rbx\n"                                          \
                         "pop %rcx\n"                                          \
                         "pop %rdx\n"                                          \
                         "pop %rsi\n"                                          \
                         "pop %rdi\n"                                          \
                         "iretq\n");                                           \
    }

IRQ_HANDLER(isr_pit, pit_handler, 0)
IRQ_HANDLER(isr_keyboard, keyboard_handler, 1)