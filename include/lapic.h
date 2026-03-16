#pragma once

#include <stdint.h>

#define LAPIC_REG_ID        0x020
#define LAPIC_REG_VERSION   0x030
#define LAPIC_REG_TPR       0x080
#define LAPIC_REG_EOI       0x0B0
#define LAPIC_REG_SVR       0x0F0
#define LAPIC_REG_ICR_LOW   0x300
#define LAPIC_REG_ICR_HIGH  0x310
#define LAPIC_REG_LVT_LINT0 0x350
#define LAPIC_REG_LVT_LINT1 0x360
#define LAPIC_REG_LVT_TIMER 0x320
#define LAPIC_REG_TIMER_ICR 0x380
#define LAPIC_REG_TIMER_CCR 0x390
#define LAPIC_REG_TIMER_DCR 0x3E0

#define LAPIC_SVR_ENABLE    (1 << 8)
#define LAPIC_TIMER_PERIODIC (1 << 17)
#define LAPIC_TIMER_MASKED   (1 << 16)
#define LAPIC_LVT_MASKED     (1 << 16)

#define LAPIC_ICR_DEST_ALL_EX_SELF  (3 << 18)
#define LAPIC_ICR_LEVEL_ASSERT      (1 << 14)

#define LAPIC_TIMER_VECTOR  0x20
#define LAPIC_SPURIOUS_VEC  0xFF
#define LAPIC_IPI_SCHED_VEC 0xFD

void lapic_init(void);
void lapic_eoi(void);
uint32_t lapic_id(void);
void lapic_timer_init(uint32_t freq_hz);
void lapic_send_ipi(uint32_t target_lapic_id, uint8_t vector);
void lapic_send_ipi_all(uint8_t vector);
void lapic_timer_start(void);
void lapic_write(uint32_t reg, uint32_t val);
uint32_t lapic_read(uint32_t reg);
