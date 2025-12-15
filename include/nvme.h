#pragma once

#include <disk.h>
#include <pci.h>
#include <stdbool.h>
#include <stdint.h>

// PCI Class Codes for NVMe Controller
#define NVME_CLASS_CODE 0x01
#define NVME_SUBCLASS 0x08
#define NVME_PROG_IF 0x02

// NVMe Controller Registers (offsets from BAR0)
typedef volatile struct {
    uint64_t cap;    // Controller Capabilities
    uint32_t vs;     // Version
    uint32_t intms;  // Interrupt Mask Set
    uint32_t intmc;  // Interrupt Mask Clear
    uint32_t cc;     // Controller Configuration
    uint32_t rsvd1;  // Reserved
    uint32_t csts;   // Controller Status
    uint32_t nssr;   // NVM Subsystem Reset (Optional)
    uint32_t aqa;    // Admin Queue Attributes
    uint64_t asq;    // Admin Submission Queue Base Address
    uint64_t acq;    // Admin Completion Queue Base Address
    uint32_t cmbloc; // Controller Memory Buffer Location (Optional)
    uint32_t cmbsz;  // Controller Memory Buffer Size (Optional)
    uint32_t bpinfo; // Boot Partition Information (Optional)
    uint32_t bprsel; // Boot Partition Read Select (Optional)
    uint64_t bpmbl;  // Boot Partition Memory Buffer Location (Optional)
    uint64_t cmbms;  // Controller Memory Buffer Memory Space Control (Optional)
    uint32_t rsvd2[948];
    uint32_t doorbell[]; // Doorbell registers start at 0x1000
} __attribute__((packed)) nvme_regs_t;

// Controller Configuration (CC) fields
#define CC_EN (1 << 0)      // Enable
#define CC_CSS_NVM (0 << 4) // NVM Command Set
#define CC_MPS_SHIFT 7      // Memory Page Size
#define CC_IOSQES_SHIFT 16  // I/O Submission Queue Entry Size
#define CC_IOCQES_SHIFT 20  // I/O Completion Queue Entry Size

// Controller Status (CSTS) fields
#define CSTS_RDY (1 << 0) // Ready

// Admin Commands
typedef enum {
    NVME_ADMIN_CMD_CREATE_IO_SQ = 0x01,
    NVME_ADMIN_CMD_CREATE_IO_CQ = 0x05,
    NVME_ADMIN_CMD_IDENTIFY = 0x06,
} nvme_admin_cmd_opcode_t;

// Identify Command CNS values
typedef enum {
    NVME_IDENTIFY_NAMESPACE = 0x00,
    NVME_IDENTIFY_CONTROLLER = 0x01,
    NVME_IDENTIFY_NS_LIST = 0x02,
} nvme_identify_cns_t;

// NVM Command Set Opcodes
typedef enum {
    NVME_NVM_CMD_WRITE = 0x01,
    NVME_NVM_CMD_READ = 0x02,
} nvme_nvm_cmd_opcode_t;

// Generic Command Structure
typedef struct {
    // DW0
    uint16_t opcode : 8;
    uint16_t fuse : 2;
    uint16_t rsvd1 : 5;
    uint16_t psel : 1;
    uint16_t cid;

    // DW1
    uint32_t nsid;

    // DW2-3
    uint64_t rsvd2;

    // DW4-5
    uint64_t mptr;

    // DW6-9
    uint64_t dptr[2];

    // DW10-15
    uint32_t cdw10;
    uint32_t cdw11;
    uint32_t cdw12;
    uint32_t cdw13;
    uint32_t cdw14;
    uint32_t cdw15;
} __attribute__((packed)) nvme_cmd_t;

// Completion Queue Entry
typedef struct {
    // DW0-1
    uint32_t cdw0;
    uint32_t rsvd1;

    // DW2
    uint16_t sq_head;
    uint16_t sq_id;

    // DW3
    uint16_t cid;
    uint16_t status;
} __attribute__((packed)) nvme_cqe_t;

#define NVME_STATUS_P_MASK (1 << 0)

// Identify Controller Data Structure
typedef struct {
    uint16_t vid;   // PCI Vendor ID
    uint16_t ssvid; // PCI Subsystem Vendor ID
    char sn[20];    // Serial Number
    char mn[40];    // Model Number
    char fr[8];     // Firmware Revision
    uint8_t rab;
    uint8_t ieee[3];
    uint8_t cmic;
    uint8_t mdts;
    uint16_t cntlid;
    uint32_t ver;
    uint8_t rsvd1[172];
    uint16_t oacs; // Optional Admin Command Support
    uint8_t rsvd2[254];
    uint8_t sqes; // Submission Queue Entry Size
    uint8_t cqes; // Completion Queue Entry Size
    uint8_t rsvd3[2];
    uint32_t nn;   // Number of Namespaces
    uint16_t oncs; // Optional NVM Command Support
    uint8_t rsvd4[3574];
} __attribute__((packed)) nvme_identify_controller_t;

// Identify Namespace Data Structure
typedef struct {
    uint64_t nsze;  // Namespace Size
    uint64_t ncap;  // Namespace Capacity
    uint64_t nuse;  // Namespace Utilization
    uint8_t nsfeat; // Namespace Features
    uint8_t nlbaf;  // Number of LBA Formats
    uint8_t flbas;  // Formatted LBA Size
    uint8_t mc;     // Metadata Capabilities
    uint8_t dpc;    // End-to-end Data Protection Capabilities
    uint8_t dps;    // End-to-end Data Protection Type Settings
    uint8_t nmic;   // Namespace Multi-path I/O and Namespace Sharing
    uint8_t resv[4065];
} __attribute__((packed)) nvme_identify_ns_t;

typedef struct {
    pci_device_t pci_dev;
    nvme_regs_t *regs;

    // Admin Queues
    nvme_cmd_t *admin_sq;
    nvme_cqe_t *admin_cq;
    uint16_t admin_sq_tail;
    uint16_t admin_cq_head;
    bool admin_cq_phase;
    uint16_t admin_queue_size;

    // I/O Queues
    nvme_cmd_t *io_sq;
    nvme_cqe_t *io_cq;
    uint16_t io_queue_size;
    uint16_t io_queue_id;
    uint16_t io_sq_tail;
    uint16_t io_cq_head;
    bool io_cq_phase;
} nvme_controller_t;

void nvme_init(disk_driver_t *driver);
bool nvme_read(struct disk *d, uint64_t lba, uint32_t count, void *buf);
bool nvme_write(struct disk *d, uint64_t lba, uint32_t count, const void *buf);