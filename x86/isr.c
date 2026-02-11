#include <stdint.h>

#include <cpu.h>
#include <debug.h>
#include <interrupts.h>
#include <isr.h>
#include <keyboard.h>
#include <panic.h>
#include <pic.h>
#include <pit.h>
#include <stdio.h>
#include <string.h>

#define PUSH_REGS()                                                            \
    __asm__ volatile("push %rdi\n"                                             \
                     "push %rsi\n"                                             \
                     "push %rdx\n"                                             \
                     "push %rcx\n"                                             \
                     "push %rbx\n"                                             \
                     "push %rax\n"                                             \
                     "push %rbp\n"                                             \
                     "push %r8\n"                                              \
                     "push %r9\n"                                              \
                     "push %r10\n"                                             \
                     "push %r11\n"                                             \
                     "push %r12\n"                                             \
                     "push %r13\n"                                             \
                     "push %r14\n"                                             \
                     "push %r15\n");

#define POP_REGS()                                                             \
    __asm__ volatile("pop %r15\n"                                              \
                     "pop %r14\n"                                              \
                     "pop %r13\n"                                              \
                     "pop %r12\n"                                              \
                     "pop %r11\n"                                              \
                     "pop %r10\n"                                              \
                     "pop %r9\n"                                               \
                     "pop %r8\n"                                               \
                     "pop %rbp\n"                                              \
                     "pop %rax\n"                                              \
                     "pop %rbx\n"                                              \
                     "pop %rcx\n"                                              \
                     "pop %rdx\n"                                              \
                     "pop %rsi\n"                                              \
                     "pop %rdi\n");

#define IRETQ() __asm__ volatile("iretq\n");

#define EXCEPTION_HANDLER(n, vector)                                           \
    __attribute__((naked)) void isr_##n()                                      \
    {                                                                          \
        __asm__ volatile("push $0\n");                                         \
        PUSH_REGS();                                                           \
        __asm__ volatile("mov %rsp, %rdi\n"                                    \
                         "mov $" #vector ", %rsi\n"                            \
                         "call exception_dispatch\n"                           \
                         "mov %rax, %rsp\n");                                  \
        POP_REGS();                                                            \
        __asm__ volatile("add $8, %rsp\n");                                    \
        IRETQ();                                                               \
    }

#define EXCEPTION_HANDLER_ERR(n, vector)                                       \
    __attribute__((naked)) void isr_##n()                                      \
    {                                                                          \
        PUSH_REGS();                                                           \
        __asm__ volatile("mov %rsp, %rdi\n"                                    \
                         "mov $" #vector ", %rsi\n"                            \
                         "call exception_dispatch\n"                           \
                         "mov %rax, %rsp\n");                                  \
        POP_REGS();                                                            \
        __asm__ volatile("add $8, %rsp\n");                                    \
        IRETQ();                                                               \
    }

EXCEPTION_HANDLER(div_err, 0)
EXCEPTION_HANDLER(debug, 1)
EXCEPTION_HANDLER(nmi_int, 2)
EXCEPTION_HANDLER(breakpoint, 3)
EXCEPTION_HANDLER(overflow, 4)
EXCEPTION_HANDLER(bound_range, 5)
EXCEPTION_HANDLER(invalid_opcode, 6)
EXCEPTION_HANDLER(fpu_not_available, 7)
EXCEPTION_HANDLER_ERR(double_fault, 8)
EXCEPTION_HANDLER(coprocessor_seg_overrun, 9)
EXCEPTION_HANDLER_ERR(invalid_tss, 10)
EXCEPTION_HANDLER_ERR(invalid_segment, 11)
EXCEPTION_HANDLER_ERR(seg_fault, 12)
EXCEPTION_HANDLER_ERR(gpf, 13)
EXCEPTION_HANDLER_ERR(page_fault, 14)
EXCEPTION_HANDLER(unhandled, 15) // Vector 15
EXCEPTION_HANDLER(fpu_err, 16)
EXCEPTION_HANDLER_ERR(alignment_check, 17)
EXCEPTION_HANDLER(machine_check, 18)
EXCEPTION_HANDLER(simd_floating_point, 19)
EXCEPTION_HANDLER(virtualisation, 20)
EXCEPTION_HANDLER_ERR(control_protection, 21)
// Reserved vectors
EXCEPTION_HANDLER(res22, 22)
EXCEPTION_HANDLER(res23, 23)
EXCEPTION_HANDLER(res24, 24)
EXCEPTION_HANDLER(res25, 25)
EXCEPTION_HANDLER(res26, 26)
EXCEPTION_HANDLER(res27, 27)
EXCEPTION_HANDLER(res28, 28)
EXCEPTION_HANDLER_ERR(vmm_comm, 29)
EXCEPTION_HANDLER_ERR(security_protection, 30)
EXCEPTION_HANDLER(res31, 31)

#define IRQ_HANDLER(n, handler, irq_num)                                       \
    __attribute__((naked)) void n()                                            \
    {                                                                          \
        PUSH_REGS()                                                            \
        __asm__ volatile("mov %rsp, %rdi\n"                                    \
                         "call " #handler "\n"                                 \
                         "mov %rax, %rsp\n"                                    \
                         "movb $" #irq_num ", %al\n"                           \
                         "call pic_sendEOI\n");                                \
        POP_REGS()                                                             \
        IRETQ()                                                                \
    }

IRQ_HANDLER(isr_pit, pit_handler, 0)
IRQ_HANDLER(isr_keyboard, keyboard_handler, 1)

#define IRQ_HANDLER_GENERIC(n, irq_num)                                        \
    __attribute__((naked)) void n()                                            \
    {                                                                          \
        PUSH_REGS()                                                            \
        __asm__ volatile("mov %rsp, %rdi\n"                                    \
                         "mov $" #irq_num ", %rsi\n"                           \
                         "call irq_dispatch\n"                                 \
                         "mov %rax, %rsp\n"                                    \
                         "movb $" #irq_num ", %al\n"                           \
                         "call pic_sendEOI\n");                                \
        POP_REGS()                                                             \
        IRETQ()                                                                \
    }

IRQ_HANDLER_GENERIC(isr_irq0, 0)
IRQ_HANDLER_GENERIC(isr_irq1, 1)
IRQ_HANDLER_GENERIC(isr_irq2, 2)
IRQ_HANDLER_GENERIC(isr_irq3, 3)
IRQ_HANDLER_GENERIC(isr_irq4, 4)
IRQ_HANDLER_GENERIC(isr_irq5, 5)
IRQ_HANDLER_GENERIC(isr_irq6, 6)
IRQ_HANDLER_GENERIC(isr_irq7, 7)
IRQ_HANDLER_GENERIC(isr_irq8, 8)
IRQ_HANDLER_GENERIC(isr_irq9, 9)
IRQ_HANDLER_GENERIC(isr_irq10, 10)
IRQ_HANDLER_GENERIC(isr_irq11, 11)
IRQ_HANDLER_GENERIC(isr_irq12, 12)
IRQ_HANDLER_GENERIC(isr_irq13, 13)
IRQ_HANDLER_GENERIC(isr_irq14, 14)
IRQ_HANDLER_GENERIC(isr_irq15, 15)

void page_fault_handler(interrupt_frame_t *frame)
{
    uint64_t rip = frame->rip;
    cr2_t address = get_cr2();

    char buf[128];
    snprintf(buf, sizeof(buf),
             "Page fault\nInstruction: 0x%016lx\nAddress: 0x%016lx\n", rip,
             address);
    panic(buf);
}

void double_fault_handler(interrupt_frame_t *frame)
{
}