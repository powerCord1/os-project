#include <stddef.h>
#include <stdint.h>

#define PIT_BASE_FREQUENCY 1193182
#define PIT_CMD_PORT 0x43
#define PIT_CHANNEL0_DATA_PORT 0x40
#define PIT_CHANNEL2_DATA_PORT 0x42
#define SPEAKER_PORT 0x61

extern volatile uint64_t pit_ticks;

void pit_init();

// This gets called when a PIT interrupt is received
void pit_handler();