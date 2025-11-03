#include <stdint.h>

#include <debug.h>
#include <io.h>
#include <keyboard.h>
#include <pit.h>
#include <scancode.h>
#include <tty.h>

uint8_t get_key()
{
    return inb(0x60);
}

void keyboard_handler(void)
{
    uint8_t key = get_key();

    if (key & 0x80) { // key release
    } else {
        log_info("key pressed: 0x%x", key);
        if (key == KEY_ESC) {
            pit_request_beep(2000);
        } else {
            term_putchar(scancode_map[key]);
        }
    }
}