#include <app.h>
#include <keyboard.h>
#include <stdio.h>

void typewriter_init()
{
    while (1) {
        printf("%c", kbd_get_last_char(true));
    }
}