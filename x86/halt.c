#include <x86/cpu.h>

void halt(void) {
    asm volatile("hlt");
}