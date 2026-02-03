#include <stdbool.h>
#include <stdint.h>

#include <debug.h>
#include <interrupts.h>
#include <io.h>
#include <keyboard.h>
#include <pit.h>
#include <sound.h>
#include <timer.h>

static volatile bool pit_beep_requested = false;
static volatile uint32_t pit_beep_request_freq;

void play_sound(uint32_t freq)
{
    log_verbose("Playing sound at %d Hz", freq);

    if (freq == 0) {
        nosound();
        return;
    }

    uint16_t divisor = PIT_BASE_FREQUENCY / freq;

    // square wave
    outb(PIT_CMD_PORT, 0xB6);

    outb(PIT_CHANNEL2_DATA_PORT, (uint8_t)(divisor & 0xFF));
    outb(PIT_CHANNEL2_DATA_PORT, (uint8_t)((divisor >> 8) & 0xFF));

    // enable speaker
    uint8_t speaker_state = inb(SPEAKER_PORT);
    outb(SPEAKER_PORT, speaker_state | 3);
}

void nosound()
{
    outb(SPEAKER_PORT, inb(SPEAKER_PORT) & 0xFC);
}

void beep(uint32_t freq)
{
    play_sound(freq);
    wait_ms(20);
    nosound();
}

void sound_test()
{
    for (uint16_t i = 0; i < 20000; i++) {
        if (kbd_get_key(false).scancode == KEY_ESC) {
            break;
        }
        play_sound(i);
        wait_ms(1);
    }
    nosound();
}

void check_beep()
{
    if (pit_beep_requested) {
        beep(pit_beep_request_freq);
        pit_beep_requested = false;
    }
    enable_interrupts(); // after sounding speaker, interrupts get disabled
                         // somehow
}

void request_beep(uint32_t freq)
{
    pit_beep_request_freq = freq;
    pit_beep_requested = true;
}