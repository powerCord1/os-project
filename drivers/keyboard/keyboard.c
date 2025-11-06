#include <stdbool.h>
#include <stdint.h>

#include <cpu.h>
#include <debug.h>
#include <io.h>
#include <keyboard.h>
#include <panic.h>
#include <pit.h>
#include <power.h>
#include <tty.h>

#define KBD_DATA_PORT 0x60
#define KBD_STATUS_PORT 0x64
#define KBD_CMD_PORT 0x64

#define KBD_LED_CMD 0xED

static volatile char last_char = 0;
static volatile char last_scancode = 0;

kbd_modifiers_t kbd_modifiers = {false, false, false, false, false, false};

uint8_t get_key()
{
    return inb(0x60);
}

void kbd_set_leds(void)
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
        log_info("key pressed: 0x%x", key);
        switch (key) {
        case KEY_F1:
            break;
        case KEY_F2:
            break;
        case KEY_F3:
            break;
        case KEY_F4:
            term_clear();
            break;
        case KEY_F5:
            term_chartest();
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

char kbd_get_last_char(bool wait)
{
    if (wait) {
        last_char = 0;
        while (last_char == 0) {
            idle();
        }
    }
    char c = last_char;
    return c;
}

char kbd_get_scancode(bool wait)
{
    if (wait) {
        last_scancode = 0;
        while (last_scancode == 0) {
            idle();
        }
    }
    char sc = last_scancode;
    last_scancode = 0;
    return sc;
}

char kbd_get_last_scancode()
{
    return last_scancode;
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