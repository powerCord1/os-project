#include <apic.h>
#include <debug.h>
#include <vmm.h>
#include <pic.h>
#include <uacpi/acpi.h>
#include <uacpi/tables.h>
#include <uacpi/uacpi.h>
#include <stdio.h>
#include <string.h>
#include <interrupts.h>
#include <cpu.h>

static uintptr_t lapic_ptr = 0;
static uintptr_t ioapic_ptr = 0;

struct interrupt_override {
    uint8_t bus;
    uint8_t source;
    uint32_t gsi;
    uint16_t flags;
};

static struct interrupt_override overrides[16];
static int num_overrides = 0;

uint32_t lapic_read(uint32_t reg) {
    if (!lapic_ptr) return 0;
    return *(volatile uint32_t*)(lapic_ptr + reg);
}

void lapic_write(uint32_t reg, uint32_t data) {
    if (!lapic_ptr) return;
    *(volatile uint32_t*)(lapic_ptr + reg) = data;
}

void lapic_eoi() {
    lapic_write(LAPIC_EOI, 0);
}

void ioapic_write(uintptr_t base, uint8_t reg, uint32_t data) {
    if (!base) return;
    *(volatile uint32_t*)(base) = reg;
    *(volatile uint32_t*)(base + 0x10) = data;
}

uint32_t ioapic_read(uintptr_t base, uint8_t reg) {
    if (!base) return 0;
    *(volatile uint32_t*)(base) = reg;
    return *(volatile uint32_t*)(base + 0x10);
}

void ioapic_set_gsi(uint32_t gsi, uint64_t apic_id, uint8_t vector, uint16_t flags) {
    uint32_t low = vector; // Delivery Mode: Fixed (000), Vector: vector
    uint32_t high = (uint32_t)(apic_id << 24);

    // Handle flags (polarity/trigger mode)
    if (flags & ACPI_MADT_POLARITY_ACTIVE_LOW) {
        low |= (1 << 13);
    }
    if (flags & ACPI_MADT_TRIGGERING_LEVEL) {
        low |= (1 << 15);
    }

    ioapic_write(ioapic_ptr, IOREDTBL + gsi * 2, low);
    ioapic_write(ioapic_ptr, IOREDTBL + gsi * 2 + 1, high);
}

void apic_init() {
    if (!is_apic_enabled()) {
        log_err("APIC: Hardware does not support APIC");
        return;
    }

    // Enable APIC globally via MSR
    uint64_t apic_base_msr = rdmsr(0x1B);
    wrmsr(0x1B, apic_base_msr | (1 << 11));

    struct uacpi_table tbl;
    uacpi_status ret = uacpi_table_find_by_signature(ACPI_MADT_SIGNATURE, &tbl);
    if (uacpi_unlikely_error(ret)) {
        log_err("APIC: Failed to find MADT table: %s", uacpi_status_to_string(ret));
        return;
    }

    struct acpi_madt *madt = (struct acpi_madt*)tbl.ptr;
    uintptr_t lapic_phys = (uintptr_t)madt->local_interrupt_controller_address;
    uintptr_t ioapic_phys = 0;

    uint8_t *ptr = (uint8_t*)(madt + 1);
    uint8_t *end = (uint8_t*)tbl.ptr + madt->hdr.length;

    while (ptr < end) {
        struct acpi_madt_entry_hdr {
            uint8_t type;
            uint8_t length;
        } *entry = (struct acpi_madt_entry_hdr*)ptr;

        switch (entry->type) {
            case ACPI_MADT_ENTRY_TYPE_LAPIC: {
                struct acpi_madt_lapic *lapic = (struct acpi_madt_lapic*)ptr;
                log_verbose("APIC: Found LAPIC: CPU %d, ID %d, Flags %x", lapic->uid, lapic->id, lapic->flags);
                break;
            }
            case ACPI_MADT_ENTRY_TYPE_IOAPIC: {
                struct acpi_madt_ioapic *ioapic = (struct acpi_madt_ioapic*)ptr;
                log_verbose("APIC: Found I/O APIC: ID %d, Address %x, GSI Base %d", ioapic->id, ioapic->address, ioapic->gsi_base);
                ioapic_phys = (uintptr_t)ioapic->address;
                break;
            }
            case ACPI_MADT_ENTRY_TYPE_INTERRUPT_SOURCE_OVERRIDE: {
                struct acpi_madt_interrupt_source_override *iso = (struct acpi_madt_interrupt_source_override*)ptr;
                log_verbose("APIC: Found ISO: Bus %d, Source %d, GSI %d, Flags %x", iso->bus, iso->source, iso->gsi, iso->flags);
                if (num_overrides < 16) {
                    overrides[num_overrides].bus = iso->bus;
                    overrides[num_overrides].source = iso->source;
                    overrides[num_overrides].gsi = iso->gsi;
                    overrides[num_overrides].flags = iso->flags;
                    num_overrides++;
                }
                break;
            }
            case ACPI_MADT_ENTRY_TYPE_LAPIC_ADDRESS_OVERRIDE: {
                struct acpi_madt_lapic_address_override *lao = (struct acpi_madt_lapic_address_override*)ptr;
                lapic_phys = (uintptr_t)lao->address;
                break;
            }
        }
        ptr += entry->length;
    }

    uacpi_table_unref(&tbl);

    if (!lapic_phys || !ioapic_phys) {
        log_err("APIC: Failed to find LAPIC or I/O APIC address");
        return;
    }

    log_info("APIC: Physical addresses: LAPIC 0x%x, I/O APIC 0x%x", lapic_phys, ioapic_phys);

    // Map the LAPIC and I/O APIC regions
    lapic_ptr = (uintptr_t)mmap_physical(NULL, (void*)lapic_phys, PAGE_SIZE, VMM_PRESENT | VMM_WRITE);
    ioapic_ptr = (uintptr_t)mmap_physical(NULL, (void*)ioapic_phys, PAGE_SIZE, VMM_PRESENT | VMM_WRITE);

    if (!lapic_ptr || !ioapic_ptr) {
        log_err("APIC: Failed to map LAPIC or I/O APIC");
        return;
    }

    log_info("APIC: Initializing LAPIC at %p, I/O APIC at %p", (void*)lapic_ptr, (void*)ioapic_ptr);

    // Disable PIC
    pic_disable();

    // Initialize LAPIC
    // Set Spurious Interrupt Vector Register
    // Vector 0xFF, Enable bit 8
    lapic_write(LAPIC_SIV, lapic_read(LAPIC_SIV) | 0x1FF);

    // Map legacy IRQs to vectors 0x20-0x2F
    uint32_t bsp_id = (lapic_read(LAPIC_ID) >> 24) & 0xFF;
    bool gsi_programmed[24] = {0};

    // 1. Program overrides first
    for (int i = 0; i < num_overrides; i++) {
        if (overrides[i].gsi < 24) {
            log_verbose("APIC: Mapping GSI %d to IRQ %d (vector 0x%x)", overrides[i].gsi, overrides[i].source, 0x20 + overrides[i].source);
            ioapic_set_gsi(overrides[i].gsi, bsp_id, 0x20 + overrides[i].source, overrides[i].flags);
            gsi_programmed[overrides[i].gsi] = true;
        }
    }

    // 2. Map remaining legacy IRQs to default pins
    for (uint8_t i = 0; i < 16; i++) {
        // Check if this IRQ source has an override
        bool has_override = false;
        for (int j = 0; j < num_overrides; j++) {
            if (overrides[j].source == i) {
                has_override = true;
                break;
            }
        }

        if (!has_override && !gsi_programmed[i]) {
            log_verbose("APIC: Mapping GSI %d to IRQ %d (vector 0x%x) [Default]", i, i, 0x20 + i);
            ioapic_set_gsi(i, bsp_id, 0x20 + i, 0); // Polarity conforming, Trigger conforming
        }
    }

    apic_in_use = true;
    log_info("APIC: Initialized");
}


