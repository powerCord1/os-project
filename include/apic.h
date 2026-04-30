#pragma once

#include <stdint.h>
#include <stdbool.h>

#define LAPIC_ID 0x0020
#define LAPIC_VER 0x0030
#define LAPIC_TPR 0x0080
#define LAPIC_APR 0x0090
#define LAPIC_PPR 0x00A0
#define LAPIC_EOI 0x00B0
#define LAPIC_RRD 0x00C0
#define LAPIC_LDR 0x00D0
#define LAPIC_DFR 0x00E0
#define LAPIC_SIV 0x00F0
#define LAPIC_ISR 0x0100
#define LAPIC_TMR 0x0180
#define LAPIC_IRR 0x0200
#define LAPIC_ESR 0x0280
#define LAPIC_ICRL 0x0300
#define LAPIC_ICRH 0x0310
#define LAPIC_LVT_TMR 0x0320
#define LAPIC_LVT_THRM 0x0330
#define LAPIC_LVT_PERF 0x0340
#define LAPIC_LVT_LINT0 0x0350
#define LAPIC_LVT_LINT1 0x0360
#define LAPIC_LVT_ERR 0x0370
#define LAPIC_TMRINIT 0x0380
#define LAPIC_TMRCURR 0x0390
#define LAPIC_TMRDIV 0x03E0

#define IOAPICID 0x00
#define IOAPICVER 0x01
#define IOAPICARB 0x02
#define IOREDTBL 0x10

void apic_init();
void lapic_eoi();
uint32_t lapic_read(uint32_t reg);
void lapic_write(uint32_t reg, uint32_t data);

void ioapic_write(uintptr_t base, uint8_t reg, uint32_t data);
uint32_t ioapic_read(uintptr_t base, uint8_t reg);
void ioapic_set_irq(uint8_t irq, uint64_t apic_id, uint8_t vector);
