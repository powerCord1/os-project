#pragma once

#include <stdint.h>

#define GDT_CODE_SEGMENT 0x08
#define GDT_DATA_SEGMENT 0x10
#define GDT_BIOS_DATA_SEGMENT 0x18
#define GDT_ENTRIES 4

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
    gdt_entry_t *base;
} __attribute__((packed)) gdtr_t;

extern gdt_entry_t gdt[GDT_ENTRIES];
extern gdtr_t gdtr;

void gdt_init(void);
extern void gdt_flush(void);

void gdt_set_gate(int num, unsigned long base, unsigned long limit,
                  unsigned char access, unsigned char flags_limit_high);