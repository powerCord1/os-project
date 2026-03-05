#include <uacpi/internal/types.h>
#include <uacpi/uacpi.h>

#define ACPI_ENABLED 1

/*
    ACPI device names
*/

#define ACPI_HID_PCI_ROOT "PNP0C00"
#define ACPI_HID_MOTHERBOARD "PNP0C01"
#define ACPI_HID_PROCESSOR "PNP0C02"
#define ACPI_HID_THERMAL_ZONE "PNP0C03"
#define ACPI_HID_SYSTEM_BATTERY "PNP0C0A"
#define ACPI_HID_FAN "PNP0C0B"
#define ACPI_HID_POWER_BUTTON "PNP0C0C"
#define ACPI_HID_LID_DEVICE "PNP0C0D"
#define ACPI_HID_SLEEP_BUTTON "PNP0C0E"
#define ACPI_HID_INTERRUPT_LINK_DEVICE "PNP0C0F"
#define ACPI_HID_HPET "PNP0103"
#define ACPI_HID_AC_ADAPTER "ACPI0003"
#define ACPI_HID_PROCESSOR_CONTAINER "ACPI0010"

// Run uACPI initialisation functions
void acpi_init();

uacpi_status acpi_poweroff();
uacpi_status acpi_reboot();
uacpi_status acpi_suspend();
static uacpi_iteration_decision battery_callback(void *user_context,
                                                 uacpi_namespace_node *node,
                                                 uacpi_u32 depth);
uacpi_namespace_node *acpi_find_system_battery();
static uacpi_status get_int_from_pkg(uacpi_package *pkg, uacpi_size index,
                                     uacpi_u64 *out);
int acpi_get_battery_percentage(uacpi_namespace_node *battery_node);
static uacpi_iteration_decision
list_device_callback(void *ctx, uacpi_namespace_node *node, uacpi_u32 depth);
void acpi_list_acpi_devices();
float acpi_get_cpu_temp(bool farenheit);