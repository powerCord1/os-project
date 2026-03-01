#include <debug.h>

#include <uacpi/sleep.h>
#include <uacpi/status.h>

uacpi_status acpi_poweroff()
{
    uacpi_status ret = uacpi_prepare_for_sleep_state(UACPI_SLEEP_STATE_S5);
    if (uacpi_unlikely_error(ret)) {
        log_err("Failed to prepare for ACPI sleep state");
        return ret;
    }
    ret = uacpi_enter_sleep_state(UACPI_SLEEP_STATE_S5);
    if (uacpi_unlikely_error(ret)) {
        log_err("Failed to enter ACPI sleep state");
        return ret;
    }
}

uacpi_status acpi_reboot()
{
    return uacpi_reboot();
}

uacpi_status acpi_suspend()
{
    uacpi_status ret = uacpi_prepare_for_sleep_state(UACPI_SLEEP_STATE_S3);
    if (uacpi_unlikely_error(ret)) {
        log_err("Failed to prepare for ACPI sleep state");
        return ret;
    }
    ret = uacpi_enter_sleep_state(UACPI_SLEEP_STATE_S3);
    if (uacpi_unlikely_error(ret)) {
        log_err("Failed to enter ACPI sleep state");
        return ret;
    }
}