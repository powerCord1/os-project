#include <stdint.h>

#include <panic.h>
#include <security.h>

// TODO: add randomisation for the stack canary on boot
uintptr_t __stack_chk_guard = 0x42042067;

__attribute__((noreturn)) void __stack_chk_fail()
{
    panic("stack smashing detected");
}