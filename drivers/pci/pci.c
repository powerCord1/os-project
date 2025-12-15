#include <debug.h>
#include <io.h>
#include <pci.h>

uint32_t pci_read_dword(uint8_t bus, uint8_t device, uint8_t function,
                        uint8_t offset)
{
    uint32_t address;
    uint32_t lbus = (uint32_t)bus;
    uint32_t ldevice = (uint32_t)device;
    uint32_t lfunction = (uint32_t)function;

    address = (uint32_t)((lbus << 16) | (ldevice << 11) | (lfunction << 8) |
                         (offset & 0xfc) | 0x80000000);

    outl(PCI_CONFIG_ADDRESS, address);
    return inl(PCI_CONFIG_DATA);
}

void pci_write_dword(uint8_t bus, uint8_t device, uint8_t function,
                     uint8_t offset, uint32_t value)
{
    uint32_t address;
    uint32_t lbus = (uint32_t)bus;
    uint32_t ldevice = (uint32_t)device;
    uint32_t lfunction = (uint32_t)function;

    address = (uint32_t)((lbus << 16) | (ldevice << 11) | (lfunction << 8) |
                         (offset & 0xfc) | 0x80000000);

    outl(PCI_CONFIG_ADDRESS, address);
    outl(PCI_CONFIG_DATA, value);
}

uint16_t pci_get_vendor_id(uint8_t bus, uint8_t device, uint8_t function)
{
    uint32_t r = pci_read_dword(bus, device, function, 0);
    return r & 0xFFFF;
}

pci_device_t pci_get_device(uint8_t class_code, uint8_t subclass,
                            uint8_t prog_if)
{
    log_verbose(
        "PCI: Searching for device with class=%x, subclass=%x, prog_if=%x",
        class_code, subclass, prog_if);

    pci_device_t found_dev = {0};
    found_dev.vendor_id = 0xFFFF; // Not found

    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t device = 0; device < 32; device++) {
            for (uint8_t function = 0; function < 8; function++) {
                if (pci_get_vendor_id(bus, device, function) == 0xFFFF) {
                    continue;
                }

                uint32_t class_reg =
                    pci_read_dword(bus, device, function, 0x08);
                uint8_t base_class = (class_reg >> 24) & 0xFF;
                uint8_t sub_class = (class_reg >> 16) & 0xFF;
                uint8_t prog_if_val = (class_reg >> 8) & 0xFF;

                if (base_class == class_code && sub_class == subclass &&
                    prog_if_val == prog_if) {
                    uint32_t dev_vendor =
                        pci_read_dword(bus, device, function, 0x00);
                    found_dev.vendor_id = dev_vendor & 0xFFFF;
                    found_dev.device_id = dev_vendor >> 16;
                    found_dev.class_code = base_class;
                    found_dev.subclass = sub_class;
                    found_dev.prog_if = prog_if_val;
                    found_dev.bus = bus;
                    found_dev.device = device;
                    found_dev.function = function;
                    return found_dev;
                }
            }
        }
    }

    return found_dev;
}

uint32_t pci_get_bar_address(pci_device_t *dev, uint8_t bar_num)
{
    if (bar_num >= 6) {
        return 0;
    }

    uint8_t offset = 0x10 + (bar_num * 4);
    uint32_t bar = pci_read_dword(dev->bus, dev->device, dev->function, offset);

    if (bar & 0x1) { // I/O space BAR
        return bar & 0xFFFFFFFC;
    } else { // Memory space BAR
        return bar & 0xFFFFFFF0;
    }
}

void pci_scan_bus()
{
    log_info("PCI: Scanning bus...");
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t device = 0; device < 32; device++) {
            for (uint8_t function = 0; function < 8; function++) {
                uint16_t vendor = pci_get_vendor_id(bus, device, function);
                if (vendor != 0xFFFF) {
                    uint32_t dev_vendor =
                        pci_read_dword(bus, device, function, 0x00);
                    log_verbose("  - Found device %04x:%04x at %d:%d:%d",
                                vendor, dev_vendor >> 16, bus, device,
                                function);
                }
            }
        }
    }
}
