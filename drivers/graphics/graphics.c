#include <graphics.h>

// I gave up trying to implement graphics, because with a UEFI BIOS, GRUB was
// giving a page fault as soon as it started the OS (not my code, tried removing
// everything under _start) and I couldn't figure out what the hell was
// happening