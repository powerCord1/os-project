#include <stdint.h>

#include <acpi.h>
#include <cpu.h>
#include <debug.h>
#include <framebuffer.h>
#include <fs.h>
#include <interrupts.h>
#include <io.h>
#include <panic.h>
#include <pit.h>
#include <power.h>
#include <stdio.h>
#include <string.h>

#define USE_VM_SHUTDOWN false

#define SHUTDOWN_PORT_QEMU 0x604
#define SHUTDOWN_PORT_BOCHS 0xB004
#define SHUTDOWN_PORT_VBOX 0x4004
#define SHUTDOWN_PORT_CLOUD_HYPERVISOR 0x600

#define SHUTDOWN_SIG_QEMU 0x2000
#define SHUTDOWN_SIG_BOCHS 0x2000
#define SHUTDOWN_SIG_VBOX 0x3400
#define SHUTDOWN_SIG_CLOUD_HYPERVISOR 0x34

__attribute__((noreturn)) void sys_reboot()
{
    fb_clear();
    printf("Rebooting...\n");
    log_info("Rebooting...");
    wait_for_render();
    do_shutdown_calls();

    sys_do_reboot();

    // System should have rebooted by now. in case it hasn't, show a message
    fb_clear();
    printf("System was unable to reboot\n"
           "You can manually power off the system by pressing the power"
           "button");
    halt_cf();
}

__attribute__((noreturn)) void sys_shutdown()
{
    fb_clear();
    printf("Shutting down...\n");
    log_info("Shutting down...");
    wait_for_render();
    do_shutdown_calls();

    sys_do_poweroff();

    // System should have powered off by now. in case it hasn't, show a message
    fb_clear();
    printf("System was unable to shut down\n"
           "Please manually power off the system by pressing the power "
           "button.");
    halt_cf();
}

void sys_suspend()
{
    sys_do_suspend();
}

void do_shutdown_calls()
{
    disable_interrupts();
    if (!vfs_unmount()) {
        log_err("Failed to unmount filesystems");
    }
}

static void sys_do_poweroff()
{
#if USE_VM_SHUTDOWN
    if (cpu_vendor_id == CPUID_VENDOR_KVM ||
        cpu_vendor_id == CPUID_VENDOR_QEMU) {
        outw(SHUTDOWN_PORT_BOCHS, SHUTDOWN_SIG_BOCHS);
        outw(SHUTDOWN_PORT_QEMU, SHUTDOWN_SIG_QEMU);
    } else if (cpu_vendor_id == CPUID_VENDOR_VBOX) {
        outw(SHUTDOWN_PORT_VBOX, SHUTDOWN_SIG_VBOX);
    } else if (cpu_vendor_id == CPUID_VENDOR_HYPERV) {
        outw(SHUTDOWN_PORT_CLOUD_HYPERVISOR, SHUTDOWN_SIG_CLOUD_HYPERVISOR);
    }
#endif

    acpi_poweroff();
}

static void sys_do_reboot()
{
    uacpi_status ret = acpi_reboot();
    if (uacpi_unlikely_error(ret)) {
        log_err("Failed to invoke ACPI reboot, resetting the CPU instead");
        sys_reset();
    }
}

static void sys_do_suspend()
{
    // THIS IS NOT COMPLETED
    // The kernel will need to save and resume context to RAM first

    acpi_suspend();
}

__attribute__((noreturn)) void sys_reset()
{
    outb(0x64, 0xFE);
}

int get_battery_percentage()
{
    uacpi_namespace_node *battery_node = acpi_find_system_battery();
    if (battery_node == NULL) {
        log_err("Failed to find system battery node");
        return -1;
    }
    return acpi_get_battery_percentage(battery_node);
}