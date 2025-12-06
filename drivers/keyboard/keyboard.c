#include <stdbool.h>
#include <stdint.h>

#include <cpu.h>
#include <debug.h>
#include <framebuffer.h>
#include <io.h>
#include <keyboard.h>
#include <panic.h>
#include <pit.h>
#include <power.h>

#define KBD_DATA_PORT 0x60
#define KBD_STATUS_PORT 0x64
#define KBD_CMD_PORT 0x64

#define KBD_LED_CMD 0xED

#define KBD_DEFAULT_TYPM_RATE 0
#define KBD_DEFAULT_TYPM_DELAY 0

static volatile char last_char = 0;
static volatile char last_scancode = 0;

kbd_modifiers_t kbd_modifiers = {false, false, false, false, false, false};
key_t last_key = {0, 0};

void kbd_init()
{
    log_verbose("Setting keyboard LED state");
    kbd_set_leds();
    log_verbose("Setting typematic rate, rate: %d, delay: %d",
                KBD_DEFAULT_TYPM_RATE, KBD_DEFAULT_TYPM_DELAY);
    kbd_set_typematic_rate(KBD_DEFAULT_TYPM_RATE, KBD_DEFAULT_TYPM_DELAY);
}

uint8_t get_key()
{
    return inb(0x60);
}

void kbd_set_leds()
{
    uint8_t data = 0;
    if (kbd_modifiers.scroll_lock) {
        data |= 1;
    }
    if (kbd_modifiers.num_lock) {
        data |= 2;
    }
    if (kbd_modifiers.caps_lock) {
        data |= 4;
    }

    wait_for_kbd();
    outb(KBD_DATA_PORT, KBD_LED_CMD);
    wait_for_kbd();
    outb(KBD_DATA_PORT, data);
}

void kbd_set_typematic_rate(uint8_t rate, uint8_t delay)
{
    if (rate > 0x1F) {
        rate = 0x1F;
    }
    if (delay > 0x03) {
        delay = 0x03;
    }

    uint8_t data = (delay << 5) | rate;

    wait_for_kbd();
    outb(KBD_DATA_PORT, 0xF3); // Set typematic rate command
    wait_for_kbd();
    outb(KBD_DATA_PORT, data);
}

void wait_for_kbd()
{
    while ((inb(KBD_STATUS_PORT) & 2) != 0)
        ;
}

void keyboard_handler()
{
    uint8_t key = get_key();

    if (key & 0x80) {
        // key release
        key -= 0x80; // convert to press scancode
        switch (key) {
        case KEY_SHIFT_LEFT:
        case KEY_SHIFT_RIGHT:
            kbd_modifiers.shift = false;
            break;
        case KEY_CTRL_LEFT:
            kbd_modifiers.ctrl = false;
            break;
        case KEY_ALT_LEFT:
            kbd_modifiers.alt = false;
            break;
        }
    } else {
        // key press
        switch (key) {
        case KEY_CAPSLOCK:
            kbd_modifiers.caps_lock = !kbd_modifiers.caps_lock;
            kbd_set_leds();
            break;
        case KEY_SHIFT_LEFT:
        case KEY_SHIFT_RIGHT:
            kbd_modifiers.shift = true;
            break;
        case KEY_CTRL_LEFT:
            kbd_modifiers.ctrl = true;
            break;
        case KEY_ALT_LEFT:
            kbd_modifiers.alt = true;
            break;
        }

        last_char = scancode_map[key];

        last_scancode = key;

        log_debug("key pressed: 0x%x", key);

        switch (key) {
        case KEY_F1:
            break;
        case KEY_F2:
            break;
        case KEY_F3:
            break;
        case KEY_F4:
            fb_clear();
            break;
        case KEY_F5:
            fb_char_test();
            break;
        case KEY_F6:
            reboot();
            break;
        case KEY_F7:
            shutdown();
            break;
        case KEY_F8:
            panic("manually triggered panic");
            break;
        }
    }
}

key_t kbd_get_key(bool wait)
{
    if (wait) {
        fb_show_cursor(); // should always show cursor when waiting for input
        last_scancode = 0;
        while (last_scancode == 0) {
            idle();
        }
        fb_hide_cursor();
    }
    last_key.key = scancode_map[last_scancode];
    last_key.scancode = last_scancode;
    return last_key;
}

char kbd_capitalise(char c)
{
    c -= ('a' - 'A');
    return c;
}

void kbd_dump_modifiers()
{
    log_info("shift: %x", kbd_modifiers.shift);
    log_info("caps_lock: %x", kbd_modifiers.caps_lock);
    log_info("ctrl: %x", kbd_modifiers.ctrl);
    log_info("alt: %x", kbd_modifiers.alt);
    log_info("num_lock: %x", kbd_modifiers.num_lock);
    log_info("scroll_lock: %x", kbd_modifiers.scroll_lock);
}

uint8_t kbd_poll_key()
{
    while (1) {
        if (inb(KBD_STATUS_PORT) & 1) {
            uint8_t scancode = inb(KBD_DATA_PORT);
            if ((scancode & 0x80) == 0) { // no releases
                return scancode;
            }
        }
    }
}