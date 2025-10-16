#include <cpu.h>

void halt(void) {
    asm volatile("hlt");
}