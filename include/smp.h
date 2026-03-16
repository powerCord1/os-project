#pragma once

#include <stdbool.h>
#include <stdint.h>

#define MAX_CPUS 16

#define MSR_GS_BASE 0xC0000101

typedef struct thread thread_t;

typedef struct {
    uint32_t lapic_id;
    uint32_t cpu_id;
    thread_t *current_thread;
    thread_t *idle_thread;
    bool online;
    uint64_t tss_rsp0;
} cpu_t;

extern cpu_t cpus[MAX_CPUS];
extern uint32_t cpu_count;

void smp_init(void);
void smp_start_aps(void);
cpu_t *smp_get_current_cpu(void);
uint32_t smp_get_cpu_count(void);

static inline void wrmsr(uint32_t msr, uint64_t val)
{
    uint32_t lo = (uint32_t)val;
    uint32_t hi = (uint32_t)(val >> 32);
    __asm__ volatile("wrmsr" : : "c"(msr), "a"(lo), "d"(hi) : "memory");
}

static inline uint64_t rdmsr(uint32_t msr)
{
    uint32_t lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}
