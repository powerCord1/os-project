#include <stddef.h>
#include <stdint.h>

extern volatile uint64_t pit_ticks;

void pit_init(uint32_t frequency);
void pit_handler();
void pit_play_sound(uint32_t frequency);
void pit_nosound();
void pit_beep(uint32_t freq);
void pit_check_beep();
void pit_request_beep(uint32_t freq);
void pit_sound_test();