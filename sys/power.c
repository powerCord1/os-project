#include <stdint.h>

#include <cpu.h>
#include <debug.h>
#include <framebuffer.h>
#include <interrupts.h>
#include <io.h>
#include <power.h>
#include <stdio.h>
#include <string.h>

#define SHUTDOWN_PORT_QEMU 0x604
#define SHUTDOWN_PORT_BOCHS 0xB004
#define SHUTDOWN_PORT_VBOX 0x4004
#define SHUTDOWN_PORT_CLOUD_HYPERVISOR 0x600

#define SHUTDOWN_SIG_QEMU 0x2000
#define SHUTDOWN_SIG_BOCHS 0x2000
#define SHUTDOWN_SIG_VBOX 0x3400
#define SHUTDOWN_SIG_CLOUD_HYPERVISOR 0x34

__attribute__((noreturn)) void reboot()
{
    fb_clear();
    printf("Rebooting...");
    log_info("Rebooting...");
    disable_interrupts();

    sys_reset();

    // should have rebooted by now
    fb_clear();
    printf("System was unable to reboot\n"
           "You can manually power off the system by pressing the power"
           "button");
    halt_catch_fire();
}

void sys_reset()
{
    outb(0x64, 0xFE);
}

__attribute__((noreturn)) void shutdown()
{
    fb_clear();
    printf("Shutting down...");
    log_info("Shutting down...");
    disable_interrupts();

    sys_poweroff();

    // system should have powered off by now, in case it hasn't, show a message
    fb_clear();
    printf("System was unable to shut down\n"
           "Please manually power off the system by pressing the power"
           "button.");
    halt_catch_fire();
}

void sys_poweroff()
{
    outw(SHUTDOWN_PORT_BOCHS, SHUTDOWN_SIG_BOCHS); // bochs
    outw(SHUTDOWN_PORT_QEMU, SHUTDOWN_SIG_QEMU);   // qemu
    outw(SHUTDOWN_PORT_VBOX, SHUTDOWN_SIG_VBOX);   // vbox
    outw(SHUTDOWN_PORT_CLOUD_HYPERVISOR,
         SHUTDOWN_SIG_CLOUD_HYPERVISOR); // cloud hypervisor
}