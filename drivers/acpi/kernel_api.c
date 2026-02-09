#include <acpi.h>
#include <cpu.h>
#include <debug.h>
#include <heap.h>
#include <interrupts.h>
#include <io.h>
#include <limine.h>
#include <panic.h>
#include <pci.h>
#include <pit.h>
#include <string.h>
#include <timer.h>
#include <uacpi/kernel_api.h>

typedef struct {
    uacpi_interrupt_handler handler;
    uacpi_handle ctx;
    uacpi_u32 irq;
} uacpi_interrupt_wrapper_t;

static uint64_t uacpi_interrupt_stub(uint64_t rsp, void *ctx)
{
    uacpi_interrupt_wrapper_t *wrapper = ctx;
    wrapper->handler(wrapper->ctx);
    return rsp;
}

uacpi_status uacpi_kernel_get_rsdp(uacpi_phys_addr *out_rsdp_address)
{
    if (rsdp_request.response == NULL ||
        rsdp_request.response->address == NULL) {
        log_err("RSDP not found");
        return UACPI_STATUS_NOT_FOUND;
    }

    if (hhdm_request.response == NULL) {
        return UACPI_STATUS_INTERNAL_ERROR;
    }

    *out_rsdp_address =
        (uacpi_phys_addr)((uintptr_t)rsdp_request.response->address -
                          hhdm_request.response->offset);
    return UACPI_STATUS_OK;
}

void *uacpi_kernel_map(uacpi_phys_addr addr, uacpi_size len)
{
    (void)len;
    if (hhdm_request.response == NULL) {
        return NULL;
    }
    return (void *)(addr + hhdm_request.response->offset);
}

void uacpi_kernel_unmap(void *addr, uacpi_size len)
{
    (void)addr;
    (void)len;
}

void uacpi_kernel_log(uacpi_log_level level, const uacpi_char *msg)
{
    // Remove trailing newline that uACPI adds
    char *new_msg = strndup(msg, strlen(msg) - 1);

    switch (level) {
    case UACPI_LOG_ERROR:
        log_err(new_msg);
        break;

    case UACPI_LOG_WARN:
        log_warn(new_msg);
        break;

    case UACPI_LOG_INFO:
        log_info(new_msg);
        break;

    case UACPI_LOG_TRACE:
    case UACPI_LOG_DEBUG:
        log_verbose(new_msg);
        break;
    }
}

uacpi_status uacpi_kernel_pci_device_open(uacpi_pci_address address,
                                          uacpi_handle *out_handle)
{
    pci_device_t *dev = (pci_device_t *)malloc(sizeof(pci_device_t));
    if (!dev) {
        return UACPI_STATUS_OUT_OF_MEMORY;
    }

    dev->bus = address.bus;
    dev->device = address.device;
    dev->function = address.function;

    *out_handle = (uacpi_handle)dev;
    return UACPI_STATUS_OK;
}

void uacpi_kernel_pci_device_close(uacpi_handle handle)
{
    free((pci_device_t *)handle);
}

uacpi_status uacpi_kernel_pci_read8(uacpi_handle device, uacpi_size offset,
                                    uacpi_u8 *value)
{
    pci_device_t *dev = (pci_device_t *)device;
    *value = pci_read_byte(dev->bus, dev->device, dev->function, offset);
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_pci_read16(uacpi_handle device, uacpi_size offset,
                                     uacpi_u16 *value)
{
    pci_device_t *dev = (pci_device_t *)device;
    *value = pci_read_word(dev->bus, dev->device, dev->function, offset);
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_pci_read32(uacpi_handle device, uacpi_size offset,
                                     uacpi_u32 *value)
{
    pci_device_t *dev = (pci_device_t *)device;
    *value = pci_read_dword(dev->bus, dev->device, dev->function, offset);
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_pci_write8(uacpi_handle device, uacpi_size offset,
                                     uacpi_u8 value)
{
    pci_device_t *dev = (pci_device_t *)device;
    pci_write_byte(dev->bus, dev->device, dev->function, offset, value);
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_pci_write16(uacpi_handle device, uacpi_size offset,
                                      uacpi_u16 value)
{
    pci_device_t *dev = (pci_device_t *)device;
    pci_write_word(dev->bus, dev->device, dev->function, offset, value);
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_pci_write32(uacpi_handle device, uacpi_size offset,
                                      uacpi_u32 value)
{
    pci_device_t *dev = (pci_device_t *)device;
    pci_write_dword(dev->bus, dev->device, dev->function, offset, value);
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_io_map(uacpi_io_addr base, uacpi_size len,
                                 uacpi_handle *out_handle)
{
    // For x86, I/O ports are accessed directly, so we don't need to map them
    // into memory. The handle can just be the base address.
    (void)len; // len is not strictly needed for direct I/O
    *out_handle = (uacpi_handle)base;
    return UACPI_STATUS_OK;
}

void uacpi_kernel_io_unmap(uacpi_handle handle)
{
    // Nothing to do with direct I/O
    (void)handle;
}

uacpi_status uacpi_kernel_io_read8(uacpi_handle handle, uacpi_size offset,
                                   uacpi_u8 *out_value)
{
    *out_value = inb((uacpi_io_addr)handle + offset);
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_io_read16(uacpi_handle handle, uacpi_size offset,
                                    uacpi_u16 *out_value)
{
    *out_value = inw((uacpi_io_addr)handle + offset);
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_io_read32(uacpi_handle handle, uacpi_size offset,
                                    uacpi_u32 *out_value)
{
    *out_value = inl((uacpi_io_addr)handle + offset);
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_io_write8(uacpi_handle handle, uacpi_size offset,
                                    uacpi_u8 in_value)
{
    outb((uacpi_io_addr)handle + offset, in_value);
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_io_write16(uacpi_handle handle, uacpi_size offset,
                                     uacpi_u16 in_value)
{
    outw((uacpi_io_addr)handle + offset, in_value);
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_io_write32(uacpi_handle handle, uacpi_size offset,
                                     uacpi_u32 in_value)
{
    outl((uacpi_io_addr)handle + offset, in_value);
    return UACPI_STATUS_OK;
}

void *uacpi_kernel_alloc(uacpi_size size)
{
    return malloc(size);
}

void uacpi_kernel_free(void *mem)
{
    free(mem);
}

uacpi_u64 uacpi_kernel_get_nanoseconds_since_boot(void)
{
    return get_ns_since_boot();
}

void uacpi_kernel_stall(uacpi_u8 usec)
{
    uacpi_u64 start = uacpi_kernel_get_nanoseconds_since_boot();
    uacpi_u64 end = start + (usec * 1000);
    while (uacpi_kernel_get_nanoseconds_since_boot() < end) {
        cpu_pause();
    }
}

void uacpi_kernel_sleep(uacpi_u64 msec)
{
    wait_ms(msec);
}

uacpi_handle uacpi_kernel_create_mutex(void)
{
    // This is a simplified mutex implementation. A real OS would have a proper
    // scheduler and synchronization primitives.
    bool *lock = malloc(sizeof(bool));
    *lock = false;
    return (uacpi_handle)lock;
}

void uacpi_kernel_free_mutex(uacpi_handle handle)
{
    free((bool *)handle);
}

uacpi_handle uacpi_kernel_create_event(void)
{
    // This is a simplified event implementation. A real OS would have a proper
    // scheduler and synchronization primitives.
    uint32_t *counter = malloc(sizeof(uint32_t));
    *counter = 0;
    return (uacpi_handle)counter;
}

void uacpi_kernel_free_event(uacpi_handle handle)
{
    free((uint32_t *)handle);
}

uacpi_thread_id uacpi_kernel_get_thread_id(void)
{
    // Single-threaded for now
    return (uacpi_thread_id)1;
}

uacpi_status uacpi_kernel_acquire_mutex(uacpi_handle handle, uacpi_u16 timeout)
{
    bool *lock = (bool *)handle;
    uacpi_u64 start_ticks = pit_ticks;

    while (__atomic_test_and_set(lock, __ATOMIC_ACQUIRE)) {
        if (timeout != 0xFFFF && (pit_ticks - start_ticks) > timeout) {
            return UACPI_STATUS_TIMEOUT;
        }
        cpu_pause();
    }

    return UACPI_STATUS_OK;
}

void uacpi_kernel_release_mutex(uacpi_handle handle)
{
    bool *lock = (bool *)handle;
    __atomic_clear(lock, __ATOMIC_RELEASE);
}

uacpi_bool uacpi_kernel_wait_for_event(uacpi_handle handle, uacpi_u16 timeout)
{
    uint32_t *counter = (uint32_t *)handle;
    uacpi_u64 start_ticks = pit_ticks;

    while (true) {
        uint32_t current_val = __atomic_load_n(counter, __ATOMIC_ACQUIRE);

        if (current_val > 0) {
            if (__atomic_compare_exchange_n(
                    counter, &current_val, current_val - 1, false,
                    __ATOMIC_RELEASE, __ATOMIC_ACQUIRE)) {
                return UACPI_TRUE;
            }
            // Another thread got it, loop again
            continue;
        }

        if (timeout != 0xFFFF && (pit_ticks - start_ticks) > timeout) {
            return UACPI_FALSE;
        }

        cpu_pause();
    }
}

void uacpi_kernel_signal_event(uacpi_handle handle)
{
    uint32_t *counter = (uint32_t *)handle;
    __atomic_add_fetch(counter, 1, __ATOMIC_RELEASE);
}

void uacpi_kernel_reset_event(uacpi_handle handle)
{
    uint32_t *counter = (uint32_t *)handle;
    __atomic_store_n(counter, 0, __ATOMIC_RELEASE);
}

uacpi_status uacpi_kernel_handle_firmware_request(uacpi_firmware_request *req)
{
    switch (req->type) {
    case UACPI_FIRMWARE_REQUEST_TYPE_BREAKPOINT:
        log_warn("ACPI: AML breakpoint hit, execution continues.");
        break;
    case UACPI_FIRMWARE_REQUEST_TYPE_FATAL:
        log_err("ACPI: AML fatal error: type=0x%x code=0x%x arg=0x%llx",
                req->fatal.type, req->fatal.code, req->fatal.arg);
        panic("AML fatal error");
        break;
    default:
        return UACPI_STATUS_INVALID_ARGUMENT;
    }

    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_uninstall_interrupt_handler(uacpi_interrupt_handler handler,
                                                      uacpi_handle irq_handle)
{
    uacpi_interrupt_wrapper_t *wrapper = irq_handle;
    (void)handler;

    irq_uninstall_handler((uint8_t)wrapper->irq, uacpi_interrupt_stub, wrapper);
    free(wrapper);

    return UACPI_STATUS_OK;
}

uacpi_handle uacpi_kernel_create_spinlock(void)
{
    return uacpi_kernel_create_mutex();
}

void uacpi_kernel_free_spinlock(uacpi_handle handle)
{
    uacpi_kernel_free_mutex(handle);
}

uacpi_cpu_flags uacpi_kernel_lock_spinlock(uacpi_handle handle)
{
    uacpi_kernel_acquire_mutex(handle, 0xFFFF);
    return 0;
}

void uacpi_kernel_unlock_spinlock(uacpi_handle handle, uacpi_cpu_flags flags)
{
    uacpi_kernel_release_mutex(handle);
}

uacpi_status uacpi_kernel_schedule_work(uacpi_work_type,
                                        uacpi_work_handler handler,
                                        uacpi_handle ctx)
{
    // No scheduler, execute immediately
    handler(ctx);
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_wait_for_work_completion(void)
{
    // No async work, so it's always complete
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_install_interrupt_handler(
    uacpi_u32 irq, uacpi_interrupt_handler handler, uacpi_handle ctx,
    uacpi_handle *out_irq_handle)
{
    if (irq > 15) {
        return UACPI_STATUS_INVALID_ARGUMENT;
    }

    uacpi_interrupt_wrapper_t *wrapper = malloc(sizeof(uacpi_interrupt_wrapper_t));
    if (!wrapper) {
        return UACPI_STATUS_OUT_OF_MEMORY;
    }

    wrapper->handler = handler;
    wrapper->ctx = ctx;
    wrapper->irq = irq;

    irq_install_handler((uint8_t)irq, uacpi_interrupt_stub, wrapper);

    if (out_irq_handle) {
        *out_irq_handle = (uacpi_handle)wrapper;
    }

    return UACPI_STATUS_OK;
}

void acpi_init()
{
    uacpi_status status;
    const char *err_msg = NULL;

    status = uacpi_initialize(0);
    if (status != UACPI_STATUS_OK) {
        err_msg = "Failed to initialize uACPI";
        goto uacpi_error;
    }

    status = uacpi_namespace_load();
    if (status != UACPI_STATUS_OK) {
        err_msg = "Failed to load uACPI namespace";
        goto uacpi_error;
    }

    status = uacpi_namespace_initialize();
    if (status != UACPI_STATUS_OK) {
        err_msg = "Failed to initialize uACPI namespace";
        goto uacpi_error;
    }

    return;

uacpi_error:
    if (err_msg) {
        log_err("%s: %s", err_msg, uacpi_status_to_string(status));
    }
}