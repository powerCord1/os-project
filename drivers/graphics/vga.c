#include <stdbool.h>

#include <interrupts.h>
#include <io.h>
#include <vga.h>

#define CURSOR_BLOCK true

void enable_cursor()
{
    //     outb(0x3D4, 0x0A);
    // #if CURSOR_BLOCK
    //     outb(0x3D5, (inb(0x3D5) & 0xC0) | 0);
    // #endif
    //     outb(0x3D4, 0x0B);
    // #if CURSOR_BLOCK
    //     outb(0x3D5, (inb(0x3D5) & 0xE0) | 15);
    // #endif
}

void disable_cursor()
{
    // outb(0x3D4, 0x0A);
    // outb(0x3D5, 0x20);
}

void vga_set_cursor(int x, int y)
{
    return;
    int pos = y * VGA_WIDTH + x;

    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}