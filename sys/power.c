#include <stdint.h>

#include <power.h>
#include <io.h>

void reboot(void) {
    uint8_t good = 0x02;
    while (good & 0x02)
        good = inb(0x64);
    outb(0x64, 0xFE);
}

void shutdown(void) {
    outw(0x604, 0x2000);
}