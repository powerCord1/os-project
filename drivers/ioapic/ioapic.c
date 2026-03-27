#include <stdbool.h>

#include <debug.h>
#include <interrupts.h>
#include <ioapic.h>
#include <lapic.h>
#include <pic.h>
#include <vmm.h>

static volatile uint32_t *ioapic_base;
static uint32_t ioapic_gsi_base;
bool ioapic_active = false;

static irq_override_t irq_overrides[16];

static void ioapic_write(uint8_t reg, uint32_t val)
{
    ioapic_base[0] = reg;           // IOREGSEL
    ioapic_base[4] = val;           // IOWIN (offset 0x10 / sizeof(uint32_t))
}

static uint32_t ioapic_read(uint8_t reg)
{
    ioapic_base[0] = reg;
    return ioapic_base[4];
}

static uint32_t ioapic_phys_addr = 0xFEC00000;

void ioapic_set_base(uint32_t phys_addr, uint32_t gsi)
{
    ioapic_phys_addr = phys_addr;
    ioapic_gsi_base = gsi;
}

void ioapic_init(void)
{
    ioapic_base = (volatile uint32_t *)mmap_physical(
        NULL, (void *)(uintptr_t)ioapic_phys_addr, PAGE_SIZE,
        VMM_PRESENT | VMM_WRITE | VMM_CACHE_DISABLE);
    if (!ioapic_base)
        return;

    uint32_t ver = ioapic_read(IOAPIC_REG_VER);
    uint32_t max_redir = (ver >> 16) & 0xFF;

    // Mask all entries
    for (uint32_t i = 0; i <= max_redir; i++)
        ioapic_mask_irq(i);

    log_info("IOAPIC initialized, %d entries", max_redir + 1);
}

void ioapic_set_irq_override(uint8_t source, uint32_t gsi, uint16_t flags)
{
    if (source < 16)
        irq_overrides[source] =
            (irq_override_t){.source_irq = source,
                             .gsi = gsi,
                             .flags = flags,
                             .active = true};
}

void ioapic_route_irq(uint8_t irq, uint8_t vector, uint32_t dest_lapic)
{
    uint32_t redir_lo = vector | IOAPIC_REDIR_MASKED;
    uint32_t redir_hi = dest_lapic << 24;

    uint8_t reg = IOAPIC_REG_REDTBL + irq * 2;
    ioapic_write(reg, redir_lo);
    ioapic_write(reg + 1, redir_hi);
}

void ioapic_route_irq_full(uint8_t gsi, uint8_t vector, uint32_t dest_lapic,
                            uint32_t flags)
{
    uint32_t redir_lo = vector | IOAPIC_REDIR_MASKED | flags;
    uint32_t redir_hi = dest_lapic << 24;

    uint8_t reg = IOAPIC_REG_REDTBL + gsi * 2;
    ioapic_write(reg, redir_lo);
    ioapic_write(reg + 1, redir_hi);
}

static uint8_t ioapic_resolve_gsi(uint8_t irq)
{
    if (irq < 16 && irq_overrides[irq].active)
        return irq_overrides[irq].gsi;
    return irq;
}

void ioapic_mask_irq(uint8_t irq)
{
    uint8_t gsi = ioapic_resolve_gsi(irq);
    uint8_t reg = IOAPIC_REG_REDTBL + gsi * 2;
    uint32_t val = ioapic_read(reg);
    ioapic_write(reg, val | IOAPIC_REDIR_MASKED);
}

void ioapic_unmask_irq(uint8_t irq)
{
    uint8_t gsi = ioapic_resolve_gsi(irq);
    uint8_t reg = IOAPIC_REG_REDTBL + gsi * 2;
    uint32_t val = ioapic_read(reg);
    ioapic_write(reg, val & ~IOAPIC_REDIR_MASKED);
}

void ioapic_setup(void)
{
    idt_install_lapic_vectors();
    pic_disable();
    lapic_write(LAPIC_REG_LVT_LINT0, LAPIC_LVT_MASKED);
    ioapic_init();
    if (!ioapic_base)
        return;

    uint32_t bsp_lapic = lapic_id();

    for (uint8_t i = 1; i < 16; i++) {
        uint32_t gsi = i;
        uint32_t redir_flags = 0;

        if (irq_overrides[i].active) {
            gsi = irq_overrides[i].gsi;
            uint16_t mps_flags = irq_overrides[i].flags;
            uint8_t polarity = mps_flags & 0x03;
            uint8_t trigger = (mps_flags >> 2) & 0x03;
            if (polarity == 3)
                redir_flags |= IOAPIC_REDIR_ACTIVE_LOW;
            if (trigger == 3)
                redir_flags |= IOAPIC_REDIR_LEVEL;
        }

        ioapic_route_irq_full(gsi, 0x20 + i, bsp_lapic, redir_flags);
    }

    ioapic_active = true;
    log_info("IOAPIC IRQ routing configured");
}
