#include <uacpi/uacpi.h>

#define ACPI_ENABLED 1

// Run uACPI initialisation functions
void acpi_init();

uacpi_status acpi_poweroff();
uacpi_status acpi_reboot();
uacpi_status acpi_suspend();