#include <stdint.h>

#include <cpu.h>
#include <interrupts.h>
#include <io.h>
#include <power.h>
#include <string.h>
#include <tty.h>

#define SHUTDOWN_PORT_QEMU 0x604
#define SHUTDOWN_PORT_BOCHS 0xB004
#define SHUTDOWN_PORT_VBOX 0x4004
#define SHUTDOWN_PORT_CLOUD_HYPERVISOR 0x600

#define SHUTDOWN_SIG_QEMU 0x2000
#define SHUTDOWN_SIG_BOCHS 0x2000
#define SHUTDOWN_SIG_VBOX 0x3400
#define SHUTDOWN_SIG_CLOUD_HYPERVISOR 0x34

void reboot(void)
{
    // Add a timeout to prevent an infinite loop
    for (int i = 0; i < 10000; i++) {
        // Wait for the keyboard controller's input buffer to be clear
        if ((inb(0x64) & 0x02) == 0) {
            outb(0x64, 0xFE); // Send the reboot command
            break;
        }
    }
    // If reboot fails, you might want to log an error or halt.
}

__attribute__((noreturn)) void shutdown(void)
{
    term_clear();
    term_writestringln("Shutting down...");
    disable_interrupts();
    outw(SHUTDOWN_PORT_BOCHS, SHUTDOWN_SIG_BOCHS); // bochs
    outw(SHUTDOWN_PORT_QEMU, SHUTDOWN_SIG_QEMU);   // qemu
    outw(SHUTDOWN_PORT_VBOX, SHUTDOWN_SIG_VBOX);   // vbox
    outw(SHUTDOWN_PORT_CLOUD_HYPERVISOR,
         SHUTDOWN_SIG_CLOUD_HYPERVISOR); // cloud hypervisor

    // system should have powered off by now, in case it hasn't, show a message
    term_clear();
    term_writestringln("System was unable to shut down.");
    term_writestringln(
        "Please manually power off the system by pressing the power button.");
    halt_catch_fire();
}