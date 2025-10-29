#include <stdint.h>

#include <io.h>
#include <keyboard.h>
#include <stdio.h>
#include <string.h>

uint8_t get_key()
{
    return inb(0x60);
}

void handle_keypress()
{
    char key_hex[3];
    uint8_t key = get_key();

    itohexa(key_hex, key);
    printf("Key: 0x%s", key_hex);
}