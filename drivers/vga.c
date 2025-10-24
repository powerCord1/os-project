#include <io.h>
#include <vga.h>

void enable_cursor()
{
    outb(0x3D4, 0x0A);
    outb(0x3D4, 0x0B);
}

void disable_cursor()
{
    outb(0x3D4, 0x0A);
    outb(0x3D5, 0x20);
}