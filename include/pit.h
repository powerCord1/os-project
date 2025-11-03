#include <stddef.h>
#include <stdint.h>

extern volatile size_t pit_ticks;

void pit_init(uint32_t frequency);
void pit_handler(void);
void pit_play_sound(uint32_t frequency);
void pit_nosound(void);
void pit_beep(uint32_t freq);
void pit_check_beep(void);
void pit_request_beep(uint32_t freq);
void pit_sound_test(void);