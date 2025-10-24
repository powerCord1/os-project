#include <stdint.h>

#include <io.h>
#include <keyboard.h>
#include <stdio.h>

uint8_t get_key()
{
    return inb(0x60);
}

void get_keypress()
{
    // uint8_t last_key = get_key();
    // uint8_t new_key = last_key;

    // while (new_key == last_key) {
    //     new_key = get_key();
    // }
    // last_key = new_key;
    // printf("key pressed");
}