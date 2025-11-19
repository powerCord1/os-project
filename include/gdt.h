#pragma once

#include <stdint.h>

#define GDT_CODE_SEGMENT 0x08
#define GDT_DATA_SEGMENT 0x10
#define GDT_ENTRIES 3

typedef struct {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_mid;
    uint8_t access;
    uint8_t flags_limit_high;
    uint8_t base_high;
} __attribute__((packed)) gdt_entry_t;

typedef struct {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) gdtr_t;

extern gdt_entry_t gdt[GDT_ENTRIES];
extern gdtr_t gdtr;

void gdt_init();
extern void gdt_flush();