#pragma once

#include <stdint.h>

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA 0xCFC

typedef struct {
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;
    uint8_t revision_id;
    uint8_t bus;
    uint8_t device;
    uint8_t function;
} pci_device_t;
uint8_t pci_read_byte(uint8_t bus, uint8_t device, uint8_t function,
                      uint8_t offset);
uint16_t pci_read_word(uint8_t bus, uint8_t device, uint8_t function,
                       uint8_t offset);
uint32_t pci_read_dword(uint8_t bus, uint8_t device, uint8_t function,
                        uint8_t offset);
void pci_write_byte(uint8_t bus, uint8_t device, uint8_t function,
                    uint8_t offset, uint8_t value);
void pci_write_word(uint8_t bus, uint8_t device, uint8_t function,
                    uint8_t offset, uint16_t value);
void pci_write_dword(uint8_t bus, uint8_t device, uint8_t function,
                     uint8_t offset, uint32_t value);

uint16_t pci_get_vendor_id(uint8_t bus, uint8_t device, uint8_t function);

pci_device_t pci_get_device(uint8_t class_code, uint8_t subclass,
                            uint8_t prog_if);

uint32_t pci_get_bar_address(pci_device_t *dev, uint8_t bar_num);

void pci_scan_bus();