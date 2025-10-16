#include <stdint.h>

#include <power.h>
#include <x86/io.h>

void reboot(void) {
    uint8_t good = 0x02;
    while (good & 0x02)
        good = inb(0x64);
    outb(0x64, 0xFE);
}

void shutdown(void) {
    
}