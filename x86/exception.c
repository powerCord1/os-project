#include <stdint.h>

#include <exception.h>
#include <panic.h>
#include <stdio.h>
#include <string.h>

void isr_div_err()
{
    panic("divide by zero");
}

void isr_debug()
{
    panic("debug exception");
}

void isr_nmi_int()
{
    panic("nonmaskable external interrupt");
}

void isr_breakpoint()
{
    panic("breakpoint hit");
}

void isr_overflow()
{
    panic("overflow");
}

void isr_bound_range()
{
    panic("BOUND range exceeded");
}

void isr_invalid_opcode()
{
    panic("unknown instruction");
}

void isr_fpu_not_available()
{
    panic("FPU not available");
}

void isr_double_fault()
{
    panic("double fault");
}

void isr_coprocessor_seg_overrun()
{
    panic("coprocessor segment overrun");
}

void isr_invalid_tss()
{
    panic("invalid TSS");
}

void isr_invalid_segment()
{
    panic("unable to load segment");
}

void isr_seg_fault()
{
    panic("stack-segment fault");
}

void isr_gpf()
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
    panic("general protection fault");
}

void isr_page_fault()
{
    panic("I/O page fault");
}

void isr_fpu_err()
{
    panic("x87 FPU floating-point error");
}

void isr_alignment_check()
{
    panic("alignment check");
}

void isr_machine_check()
{
    panic("machine check");
}

void isr_simd_floating_point()
{
    panic("SIMD floating-point exception");
}

void isr_virtualisation()
{
    panic("virtualisation exception");
}

void isr_control_protection()
{
    panic("control protection exception");
}

void isr_security_protection()
{
    panic("security exception");
}