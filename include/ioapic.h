#pragma once

#include <stdbool.h>
#include <stdint.h>

extern bool ioapic_active;

#define IOAPIC_REG_ID         0x00
#define IOAPIC_REG_VER        0x01
#define IOAPIC_REG_REDTBL     0x10

#define IOAPIC_REDIR_MASKED      (1 << 16)
#define IOAPIC_REDIR_LEVEL       (1 << 15)
#define IOAPIC_REDIR_ACTIVE_LOW  (1 << 13)

typedef struct {
    uint8_t source_irq;
    uint32_t gsi;
    uint16_t flags;
    bool active;
} irq_override_t;

void ioapic_set_base(uint32_t phys_addr, uint32_t gsi);
void ioapic_set_irq_override(uint8_t source, uint32_t gsi, uint16_t flags);
void ioapic_init(void);
void ioapic_route_irq(uint8_t irq, uint8_t vector, uint32_t dest_lapic);
void ioapic_route_irq_full(uint8_t gsi, uint8_t vector, uint32_t dest_lapic,
                            uint32_t flags);
void ioapic_mask_irq(uint8_t irq);
void ioapic_unmask_irq(uint8_t irq);
void ioapic_setup(void);
