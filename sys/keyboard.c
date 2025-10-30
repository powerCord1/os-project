#include <stdint.h>

#include <io.h>
#include <keyboard.h>
#include <scancode.h>
#include <tty.h>

uint8_t get_key()
{
    return inb(0x60);
}

void handle_keypress(void)
{
    uint8_t key = get_key();

    if (key & 0x80) { // key release
    } else {
        term_putchar(scancode_map[key]);
    }
}