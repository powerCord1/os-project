#include <stdbool.h>
#include <stdint.h>

#include <cpu.h>
#include <debug.h>
#include <io.h>
#include <keyboard.h>
#include <pit.h>
#include <power.h>
#include <tty.h>

uint8_t get_key()
{
    return inb(0x60);
}

static volatile char last_char = 0;

void keyboard_handler(void)
{
    uint8_t key = get_key();

    if (key & 0x80) { // key release
    } else {
        last_char = scancode_map[key];

        char c = scancode_map[key];
        log_info("key pressed: 0x%x", key);
        switch (key) {
        case KEY_F1:
            pit_request_beep(1000);
            break;
        case KEY_F2:
            pit_request_beep(2000);
            break;
        case KEY_F3:
            pit_request_beep(4000);
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
            pit_check_beep();
            halt();
        }
    }
    char c = last_char;
    last_char = 0;
    return c;
}