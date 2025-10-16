#include <stddef.h>
#include <stdint.h>

struct idt {
    uint16_t offset_1;
    uint16_t selector;
    uint8_t  zero;
    uint8_t  type_attributes;
    uint16_t offset_2;
};