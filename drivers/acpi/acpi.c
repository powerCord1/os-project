#include <debug.h>

#include <acpi.h>
#include <stdio.h>
#include <uacpi/internal/types.h>
#include <uacpi/namespace.h>
#include <uacpi/sleep.h>
#include <uacpi/status.h>
#include <uacpi/utilities.h>

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

static uacpi_iteration_decision battery_callback(void *user_context,
                                                 uacpi_namespace_node *node,
                                                 uacpi_u32 depth)
{
    // Save the node to the battery management structure
    uacpi_namespace_node **out_node = (uacpi_namespace_node **)user_context;
    *out_node = node;

    // Return 'break' to find the first battery, 'continue' to find all
    return UACPI_ITERATION_DECISION_BREAK;
}

uacpi_namespace_node *acpi_find_system_battery()
{
    uacpi_namespace_node *battery_node = NULL;

    // Search for the standard PNP ID for a Control Method Battery
    uacpi_status ret = uacpi_find_devices(ACPI_HID_SYSTEM_BATTERY,
                                          battery_callback, &battery_node);

    if (ret != UACPI_STATUS_OK || battery_node == NULL) {
        // No battery found or search failed
        return NULL;
    }

    return battery_node;
}

static uacpi_status get_int_from_pkg(uacpi_package *pkg, uacpi_size index,
                                     uacpi_u64 *out)
{
    if (index >= pkg->count) {
        return UACPI_STATUS_INVALID_ARGUMENT;
    }
    if (pkg->objects[index]->type != UACPI_OBJECT_INTEGER) {
        return UACPI_STATUS_INVALID_ARGUMENT;
    }

    *out = pkg->objects[index]->integer;
    return UACPI_STATUS_OK;
}

int acpi_get_battery_percentage(uacpi_namespace_node *battery_node)
{
    uacpi_object *bix_obj = NULL;
    uacpi_object *bst_obj = NULL;
    uacpi_status ret;
    int percentage = -1;

    // _BIX (Battery Information Extended)
    ret = uacpi_eval(battery_node, "_BIX", NULL, &bix_obj);
    if (ret != UACPI_STATUS_OK) {
        log_err(
            "acpi_get_battery_percentage: Failed to evaluate _BIX, return: %s",
            uacpi_status_to_string(ret));

        // Fallback to the older _BIF method
        ret = uacpi_eval(battery_node, "_BIF", NULL, &bix_obj);
        if (ret != UACPI_STATUS_OK) {
            log_err("acpi_get_battery_percentage: Failed to evaluate _BIF, "
                    "return: %s",
                    uacpi_status_to_string(ret));
            log_err(
                "acpi_get_battery_percentage: Failed to evaluate _BIX or _BIF");
            return -1;
        }
    }

    // Evaluate _BST (Battery Status)
    ret = uacpi_eval(battery_node, "_BST", NULL, &bst_obj);
    if (uacpi_unlikely(ret != UACPI_STATUS_OK)) {
        log_err("acpi_get_battery_percentage: Failed to evaluate _BST");
        uacpi_object_unref(bix_obj);
        return -1;
    }

    uacpi_u64 last_full_capacity, remaining_capacity;
    uacpi_status s1 =
        get_int_from_pkg(bix_obj->package, 2, &last_full_capacity);
    uacpi_status s2 =
        get_int_from_pkg(bst_obj->package, 2, &remaining_capacity);

    if (s1 == UACPI_STATUS_OK && s2 == UACPI_STATUS_OK &&
        last_full_capacity > 0) {
        // Calculate as percentage
        percentage = (int)((remaining_capacity * 100) / last_full_capacity);
    }

    // Clean up uACPI objects to prevent memory leaks
    uacpi_object_unref(bix_obj);
    uacpi_object_unref(bst_obj);

    return percentage;
}

float acpi_get_cpu_temp(bool farenheit)
{
    // uacpi_handle tz_handle;
    // uacpi_status status;
    // uint64_t raw_kelvin;

    // // Get a handle to the thermal zone object
    // status = uacpi_namespace_node_for_path(NULL, "\\_TZ.THM0", &tz_handle);
    // if (uacpi_unlikely_error(status)) {
    //     log_err("Failed to get thermal zone handle: %s",
    //             uacpi_status_to_string(status));
    //     return 0.0f;
    // }

    // // Evaluate the _TMP method (returns tenths of Kelvin)
    // status = uacpi_eval_integer(tz_handle, "_TMP", NULL, &raw_kelvin);
    // if (uacpi_unlikely_error(status)) {
    //     log_err("Failed to evaluate _TMP: %s",
    //     uacpi_status_to_string(status)); return 0.0f;
    // }

    // // Unit conversion
    // if (farenheit) {
    //     // TODO
    //     return 0.0f;
    // }
    // return ((float)raw_kelvin / 10.0f) - 273.15f;
    return 0.0f;
}

/*
    DEBUGGING FUNCTIONS
*/

static uacpi_iteration_decision
list_device_callback(void *ctx, uacpi_namespace_node *node, uacpi_u32 depth)
{
    uacpi_namespace_node_info *info;
    uacpi_status ret = uacpi_get_namespace_node_info(node, &info);

    if (uacpi_unlikely_error(ret)) {
        return UACPI_ITERATION_DECISION_CONTINUE;
    }

    // Get the absolute path (e.g., \_SB.PCI0.LPCB.BAT0)
    const char *path = uacpi_namespace_node_generate_absolute_path(node);

    printf("Path: %s", path);

    // Check if the device has a Hardware ID (HID)
    if (info->flags & UACPI_NS_NODE_INFO_HAS_HID) {
        printf(" | HID: %s", info->hid.value);
    }

    // Check if the device has a Compatible ID (CID)
    if (info->flags & UACPI_NS_NODE_INFO_HAS_CID) {
        // CIDs are stored in a list; you can iterate info->cid_list if needed
        printf(" | Has CID(s)");
    }

    printf("\n");

    uacpi_free_absolute_path(path);
    uacpi_free_namespace_node_info(info);

    return UACPI_ITERATION_DECISION_CONTINUE;
}

void acpi_list_acpi_devices()
{
    // Search for all objects of type 'Device' (UACPI_OBJECT_DEVICE_BIT)
    // Starting from the root, at any depth.
    uacpi_namespace_for_each_child(uacpi_namespace_root(), list_device_callback,
                                   NULL, UACPI_OBJECT_DEVICE_BIT,
                                   UACPI_MAX_DEPTH_ANY, NULL);
}