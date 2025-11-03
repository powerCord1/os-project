#include <stdbool.h>

#include <debug.h>
#include <io.h>
#include <pit.h>
#include <stdio.h>

#define PIT_BASE_FREQUENCY 1193182
#define PIT_CMD_PORT 0x43
#define PIT_CHANNEL0_DATA_PORT 0x40
#define PIT_CHANNEL2_DATA_PORT 0x42
#define SPEAKER_PORT 0x61

volatile size_t pit_ticks = 0;
static volatile bool pit_beep_requested = false;
static volatile uint32_t pit_beep_request_freq;

void pit_init(uint32_t frequency)
{
    if (frequency == 0) {
        log_warn("PIT frequency is zero");
        return;
    }

    uint16_t divisor = PIT_BASE_FREQUENCY / frequency;

    outb(PIT_CMD_PORT, 0x36);

    outb(PIT_CHANNEL0_DATA_PORT, (uint8_t)(divisor & 0xFF));
    outb(PIT_CHANNEL0_DATA_PORT, (uint8_t)((divisor >> 8) & 0xFF));
}

void pit_handler()
{
    pit_ticks++;
    if (pit_ticks % 1000 == 0) {
        log_info("PIT ticks: %u", pit_ticks);
    }
}

static void pit_wait_ms(size_t ticks)
{
    size_t eticks = pit_ticks + ticks;
    while (pit_ticks < eticks) {
        __asm__ volatile("hlt");
    }
}

void pit_play_sound(uint32_t freq)
{
    if (freq == 0) {
        pit_nosound();
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

void pit_nosound(void)
{
    outb(SPEAKER_PORT, inb(SPEAKER_PORT) & 0xFC);
}

void pit_beep(uint32_t freq)
{
    pit_play_sound(freq);
    pit_wait_ms(20);
    pit_nosound();
}

void pit_sound_test()
{
    for (uint16_t i = 0; i < 10000; i++) {
        pit_play_sound(i);
        pit_wait_ms(1);
    }
    pit_nosound();
}

void pit_check_beep()
{
    if (pit_beep_requested) {
        pit_beep(pit_beep_request_freq);
        pit_beep_requested = false;
    }
}

void pit_request_beep(uint32_t freq)
{
    pit_beep_request_freq = freq;
    pit_beep_requested = true;
}