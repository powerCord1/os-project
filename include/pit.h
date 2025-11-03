#include <stddef.h>
#include <stdint.h>

extern volatile size_t pit_ticks;

void pit_init(uint32_t frequency);
void pit_handler(void);
void pit_play_sound(uint32_t frequency);
void pit_nosound(void);
void pit_beep(void);