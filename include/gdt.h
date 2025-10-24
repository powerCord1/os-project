#include <stdint.h>

#define i686_GDT_CODE_SEGMENT 0x08
#define i686_GDT_DATA_SEGMENT 0x10

typedef struct {
    uint64_t null;
    uint16_t kernel_mode_code;
    uint64_t kernel_mode_data;
    uint64_t task_state;
} __attribute__((packed)) gdt_entry_t;

typedef struct {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) gdtr_t;

void init_gdt(void);