#include <cpuid.h>
#include <stdbool.h>
#include <stdint.h>

#include <cpu.h>
#include <debug.h>
#include <interrupts.h>
#include <io.h>
#include <sound.h>
#include <stdio.h>
#include <timer.h>

static uint64_t tsc_freq_hz = 0;
static uint64_t tsc_at_boot = 0;

static inline uint64_t rdtsc()
{
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

void cpu_init()
{
    log_verbose("Enabling SIMD extensions");
    sse_init();
    enable_a20();
    enable_mce();
}

void halt()
{
    __asm__ volatile("hlt");
}

__attribute__((noreturn)) void halt_cf()
{
    disable_interrupts();
    while (1) {
        halt();
    }
}

char get_cpu_vendor()
{
    return 0x00;
}

void idle()
{
    check_beep();
    halt();
}

bool is_pe_enabled()
{
    return get_cr0().bit_list.pe;
}

void sse_init()
{
    cr0_t cr0 = get_cr0();
    cr0.bit_list.em = 0;
    cr0.bit_list.mp = 1;
    set_cr0(cr0);

    cr4_t cr4 = get_cr4();
    cr4.bit_list.osfxsr = 1;
    cr4.bit_list.osxmmexcpt = 1;
    set_cr4(cr4);
}

void enable_mce()
{
    cr4_t cr4 = get_cr4();
    cr4.bit_list.mce = 1;
    set_cr4(cr4);
}

bool is_apic_enabled()
{
    unsigned int eax, ebx, ecx, edx;
    __cpuid(1, eax, ebx, ecx, edx);
    return (edx & (1 << 9));
}

void tsc_init()
{
    unsigned int eax, ebx, ecx, edx;
    __cpuid(0, eax, ebx, ecx, edx);
    unsigned int max_leaf = eax;

    __cpuid(0x80000000, eax, ebx, ecx, edx);
    if (eax >= 0x80000007) {
        __cpuid(0x80000007, eax, ebx, ecx, edx);
        if (edx & (1 << 8)) { // Check if Invariant TSC is supported
            log_info("TSC: Invariant TSC is supported.");
        } else {
            log_warn("TSC: Invariant TSC is NOT supported. TSC may be "
                     "unreliable.");
        }
    } else {
        log_warn("TSC: Extended CPUID leaf 0x80000007 not available. Cannot "
                 "determine Invariant TSC support.");
    }

    // Get TSC frequency from CPUID if available
    if (max_leaf >= 0x15) {
        __cpuid(0x15, eax, ebx, ecx, edx);
        if (ebx != 0 && eax != 0) {
            uint64_t core_crystal_freq = ecx;
            if (core_crystal_freq == 0 && max_leaf >= 0x16) {
                unsigned int eax16, ebx16, ecx16, edx16;
                __cpuid(0x16, eax16, ebx16, ecx16, edx16);
                if (eax16 != 0) { // Core crystal clock in MHz
                    core_crystal_freq = (uint64_t)eax16 * 1000000;
                }
            }

            if (core_crystal_freq != 0) {
                tsc_freq_hz = (core_crystal_freq * ebx) / eax;
                log_info("TSC: Frequency is %lu Hz (from CPUID 0x15).",
                         tsc_freq_hz);
                goto tsc_init_done;
            }
        }
    }

    if (max_leaf >= 0x16) {
        __cpuid(0x16, eax, ebx, ecx, edx);
        if (ebx != 0) { // TSC frequency in MHz
            tsc_freq_hz = (uint64_t)ebx * 1000000;
            log_info("TSC: Frequency is %lu Hz (from CPUID 0x16).",
                     tsc_freq_hz);
            goto tsc_init_done;
        }
    }

    // Fallback: calibrate against PIT
    log_warn("TSC: Could not determine frequency from CPUID. Calibrating "
             "against PIT...");
    disable_interrupts();
    log_verbose("Calibrating against PIT...");
    uint64_t start_tsc_calib = rdtsc();
    enable_interrupts();

    wait_ms(50);

    disable_interrupts();
    uint64_t end_tsc_calib = rdtsc();
    enable_interrupts();

    uint64_t tsc_delta = end_tsc_calib - start_tsc_calib;
    tsc_freq_hz = tsc_delta * 20; // 50ms is 1/20th of a second
    log_info("TSC: Calibrated frequency to be ~%lu Hz.", tsc_freq_hz);

tsc_init_done:
    tsc_at_boot = rdtsc();
}

// read nanoseconds since boot using the TSC
uint64_t get_ns_since_boot()
{
    if (tsc_freq_hz == 0) {
        return 0;
    }
    uint64_t current_tsc = rdtsc();
    uint64_t tsc_delta = current_tsc - tsc_at_boot;
    return (tsc_delta * 1000000000) / tsc_freq_hz;
}

void enable_a20()
{
    log_verbose("Enabling A20 line");
    uint8_t a20_status = inb(0x92);
    if (!(a20_status & 2)) {
        a20_status |= 2;
        a20_status &= ~1; // Ensure we don't accidentally cause a fast reset
        outb(0x92, a20_status);
    }
}

#define SET_CR_STUB(cr_name, type)                                             \
    void set_##cr_name(type val)                                               \
    {                                                                          \
        __asm__ volatile("mov %0, %%" #cr_name : : "r"(val.raw) : "memory");   \
    }

#define GET_CR_STUB(cr_name, type)                                             \
    type get_##cr_name()                                                       \
    {                                                                          \
        type val;                                                              \
        __asm__ volatile("mov %%" #cr_name ", %0" : "=r"(val.raw));            \
        return val;                                                            \
    }

GET_CR_STUB(cr0, cr0_t)
GET_CR_STUB(cr4, cr4_t)
SET_CR_STUB(cr0, cr0_t)
SET_CR_STUB(cr4, cr4_t)

cr2_t get_cr2()
{
    cr2_t val;
    __asm__ volatile("mov %%cr2, %0" : "=r"(val));
    return val;
}

void set_cr2(cr2_t val)
{
    __asm__ volatile("mov %0, %%cr2" : : "r"(val) : "memory");
}

cr3_t get_cr3()
{
    cr3_t val;
    __asm__ volatile("mov %%cr3, %0" : "=r"(val));
    return val;
}

void set_cr3(cr3_t val)
{
    __asm__ volatile("mov %0, %%cr3" : : "r"(val) : "memory");
}

rflags_t get_rflags()
{
    rflags_t rflags;
    __asm__ volatile("pushfq\n\t"
                     "pop %0"
                     : "=rm"(rflags.raw));
    return rflags;
}

void set_rflags(rflags_t rflags)
{
    __asm__ volatile("push %0\n\t"
                     "popfq"
                     :
                     : "rm"(rflags.raw));
}

#undef SET_CR_STUB
#undef GET_CR_STUB

// TODO: make a function to get and set cr value to prevent repetitive code