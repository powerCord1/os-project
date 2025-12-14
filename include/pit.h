#include <stddef.h>
#include <stdint.h>

#define PIT_BASE_FREQUENCY 1193182
#define PIT_CMD_PORT 0x43
#define PIT_CHANNEL0_DATA_PORT 0x40
#define PIT_CHANNEL2_DATA_PORT 0x42
#define SPEAKER_PORT 0x61

extern volatile uint64_t pit_ticks;

void pit_init();
void pit_handler();
void pit_wait_ms(size_t ticks);