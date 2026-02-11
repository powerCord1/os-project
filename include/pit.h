#include <stddef.h>
#include <stdint.h>

#define PIT_BASE_FREQUENCY 1193182
#define PIT_CMD_PORT 0x43
#define PIT_CHANNEL0_DATA_PORT 0x40
#define PIT_CHANNEL2_DATA_PORT 0x42
#define SPEAKER_PORT 0x61

extern volatile uint64_t pit_ticks;
extern bool pit_initialised;

void pit_init();

// PIT interrupt handler
uint64_t pit_handler(uint64_t rsp);

// Verify the PIT is sending interrupts, and the CPU is
// correctly handling them
void pit_check();