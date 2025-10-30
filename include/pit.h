#include <stddef.h>
#include <stdint.h>

extern size_t pit_ticks;

void pit_init(uint32_t frequency);
void pit_handler(void);