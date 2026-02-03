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
    tsc_init();
    enable_a20();
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
    uint64_t cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    return (cr0 & 1);
}

void sse_init()
{
    uint64_t cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 &= ~(1 << 2); // Clear EM bit
    cr0 |= (1 << 1);  // Set MP bit
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0));

    uint64_t cr4;
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= (1 << 9);  // Set OSFXSR bit
    cr4 |= (1 << 10); // Set OSXMMEXCPT bit
    __asm__ volatile("mov %0, %%cr4" : : "r"(cr4));
}

bool is_apic_enabled()
{
    unsigned int eax, ebx, ecx, edx;
    __cpuid(1, eax, ebx, ecx, edx);
    return (edx & (1 << 9));
}

static void tsc_init()
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