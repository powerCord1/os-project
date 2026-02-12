#include <debug.h>
#include <io.h>
#include <pci.h>

static uint32_t pci_get_config_address(uint8_t bus, uint8_t device,
                                       uint8_t function, uint8_t offset)
{
    return (uint32_t)(((uint32_t)bus << 16) | ((uint32_t)device << 11) |
                      ((uint32_t)function << 8) | (offset & 0xfc) | 0x80000000);
}

uint32_t pci_read_dword(uint8_t bus, uint8_t device, uint8_t function,
                        uint8_t offset)
{
    outl(PCI_CONFIG_ADDRESS,
         pci_get_config_address(bus, device, function, offset));
    return inl(PCI_CONFIG_DATA);
}

void pci_write_dword(uint8_t bus, uint8_t device, uint8_t function,
                     uint8_t offset, uint32_t value)
{
    outl(PCI_CONFIG_ADDRESS,
         pci_get_config_address(bus, device, function, offset));
    outl(PCI_CONFIG_DATA, value);
}

uint16_t pci_read_word(uint8_t bus, uint8_t device, uint8_t function,
                       uint8_t offset)
{
    outl(PCI_CONFIG_ADDRESS,
         pci_get_config_address(bus, device, function, offset));
    return inw(PCI_CONFIG_DATA + (offset & 2));
}

uint8_t pci_read_byte(uint8_t bus, uint8_t device, uint8_t function,
                      uint8_t offset)
{
    outl(PCI_CONFIG_ADDRESS,
         pci_get_config_address(bus, device, function, offset));
    return inb(PCI_CONFIG_DATA + (offset & 3));
}

void pci_write_word(uint8_t bus, uint8_t device, uint8_t function,
                    uint8_t offset, uint16_t value)
{
    outl(PCI_CONFIG_ADDRESS,
         pci_get_config_address(bus, device, function, offset));
    outw(PCI_CONFIG_DATA + (offset & 2), value);
}

void pci_write_byte(uint8_t bus, uint8_t device, uint8_t function,
                    uint8_t offset, uint8_t value)
{
    outl(PCI_CONFIG_ADDRESS,
         pci_get_config_address(bus, device, function, offset));
    outb(PCI_CONFIG_DATA + (offset & 3), value);
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

uint64_t pci_get_bar_address(pci_device_t *dev, uint8_t bar_num)
{
    if (bar_num >= 6) {
        return 0;
    }

    uint8_t offset = 0x10 + (bar_num * 4);
    uint32_t bar_low =
        pci_read_dword(dev->bus, dev->device, dev->function, offset);

    if (bar_low & 0x1) { // I/O Space
        return bar_low & 0xFFFFFFFC;
    } else { // Memory Space
        uint8_t type = (bar_low >> 1) & 0x03;
        if (type == 0x02) { // 64-bit BAR
            uint32_t bar_high = pci_read_dword(dev->bus, dev->device,
                                               dev->function, offset + 4);
            return ((uint64_t)bar_high << 32) | (bar_low & 0xFFFFFFF0);
        }
        return bar_low & 0xFFFFFFF0; // 32-bit
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
