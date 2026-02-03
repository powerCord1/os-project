#include <debug.h>
#include <heap.h>
#include <nvme.h>
#include <pci.h>
#include <stdio.h>
#include <string.h>
#include <timer.h>

static nvme_controller_t controller;

static bool nvme_submit_admin_command(nvme_cmd_t *cmd)
{
    memcpy(&controller.admin_sq[controller.admin_sq_tail], cmd,
           sizeof(nvme_cmd_t));

    controller.admin_sq_tail =
        (controller.admin_sq_tail + 1) % controller.admin_queue_size;
    controller.regs->doorbell[0] = controller.admin_sq_tail;

    // TODO: use interrupts instead of polling to wait for completion
    for (int i = 0; i < 500; i++) {
        nvme_cqe_t *cqe = &controller.admin_cq[controller.admin_cq_head];
        bool cqe_phase = (cqe->status & NVME_STATUS_P_MASK) ? 1 : 0;

        if (cqe_phase == controller.admin_cq_phase) {
            if ((cqe->status >> 1) != 0) {
                log_err("NVMe admin command failed with status 0x%x",
                        cqe->status >> 1);
                return false;
            }

            // Advance CQ head
            controller.admin_cq_head =
                (controller.admin_cq_head + 1) % controller.admin_queue_size;
            if (controller.admin_cq_head == 0) {
                controller.admin_cq_phase = !controller.admin_cq_phase;
            }

            // Update CQ head doorbell for the controller
            controller.regs->doorbell[1] = controller.admin_cq_head;

            return true;
        }
        wait_ms(1);
    }

    log_err("NVMe admin command timed out");
    return false;
}

static bool nvme_identify_controller()
{
    log_verbose("NVMe: Identifying controller...");

    nvme_identify_controller_t *id_controller_data =
        (nvme_identify_controller_t *)malloc(4096);
    if (!id_controller_data) {
        log_err("Failed to allocate memory for NVMe identify data");
        return false;
    }

    nvme_cmd_t cmd = {0};
    cmd.opcode = NVME_ADMIN_CMD_IDENTIFY;
    cmd.nsid = 0;
    cmd.dptr[0] = (uint64_t)id_controller_data; // Assumes physical == virtual
    cmd.cdw10 = NVME_IDENTIFY_CONTROLLER;

    if (!nvme_submit_admin_command(&cmd)) {
        free(id_controller_data);
        return false;
    }

    char model_num[41] = {0};
    char serial_num[21] = {0};
    memcpy(model_num, id_controller_data->mn, 40);
    memcpy(serial_num, id_controller_data->sn, 20);

    log_info("NVMe: Found controller: %s (SN: %s)", model_num, serial_num);

    free(id_controller_data);
    return true;
}

static bool nvme_submit_io_command(nvme_cmd_t *cmd)
{
    // For simplicity, we'll use queue 1 for I/O
    uint16_t queue_id = controller.io_queue_id;

    memcpy(&controller.io_sq[controller.io_sq_tail], cmd, sizeof(nvme_cmd_t));

    controller.io_sq_tail =
        (controller.io_sq_tail + 1) % controller.io_queue_size;
    controller.regs->doorbell[queue_id * 2] = controller.io_sq_tail;

    // TODO: use interrupts instead of polling to wait for completion
    for (int i = 0; i < 500; i++) {
        nvme_cqe_t *cqe = &controller.io_cq[controller.io_cq_head];
        bool cqe_phase = (cqe->status & NVME_STATUS_P_MASK) ? 1 : 0;

        if (cqe_phase == controller.io_cq_phase) {
            if ((cqe->status >> 1) != 0) {
                log_err("NVMe I/O command failed with status 0x%x",
                        cqe->status >> 1);
                return false;
            }

            // Advance CQ head
            controller.io_cq_head =
                (controller.io_cq_head + 1) % controller.io_queue_size;
            if (controller.io_cq_head == 0) {
                controller.io_cq_phase = !controller.io_cq_phase;
            }

            // Update CQ head doorbell for the controller
            controller.regs->doorbell[queue_id * 2 + 1] = controller.io_cq_head;

            return true;
        }
        wait_ms(1);
    }

    log_err("NVMe I/O command timed out");
    return false;
}

static bool nvme_setup_io_queues()
{
    log_verbose("NVMe: Setting up I/O queues...");

    controller.io_queue_size = 64;
    controller.io_queue_id = 1; // Use queue ID 1
    size_t sq_size = controller.io_queue_size * sizeof(nvme_cmd_t);
    size_t cq_size = controller.io_queue_size * sizeof(nvme_cqe_t);

    // NOTE: This memory MUST be physically contiguous.
    controller.io_sq = (nvme_cmd_t *)malloc(sq_size);
    controller.io_cq = (nvme_cqe_t *)malloc(cq_size);

    if (!controller.io_sq || !controller.io_cq) {
        log_err("NVMe: Failed to allocate I/O queues");
        // Cleanup allocated memory
        if (controller.io_sq) {
            free(controller.io_sq);
        }
        if (controller.io_cq) {
            free(controller.io_cq);
        }
        return false;
    }

    memset(controller.io_sq, 0, sq_size);
    memset(controller.io_cq, 0, cq_size);

    // Create I/O Completion Queue
    nvme_cmd_t cq_cmd = {0};
    cq_cmd.opcode = NVME_ADMIN_CMD_CREATE_IO_CQ;
    cq_cmd.dptr[0] = (uint64_t)controller.io_cq; // Assumes physical == virtual
    cq_cmd.cdw10 =
        ((controller.io_queue_size - 1) << 16) | controller.io_queue_id;
    cq_cmd.cdw11 = (1 << 0); // Physically contiguous

    if (!nvme_submit_admin_command(&cq_cmd)) {
        log_err("NVMe: Failed to create I/O completion queue");
        free(controller.io_sq);
        free(controller.io_cq);
        return false;
    }

    // Create I/O Submission Queue
    nvme_cmd_t sq_cmd = {0};
    sq_cmd.opcode = NVME_ADMIN_CMD_CREATE_IO_SQ;
    sq_cmd.dptr[0] = (uint64_t)controller.io_sq; // Assumes physical == virtual
    sq_cmd.cdw10 =
        ((controller.io_queue_size - 1) << 16) | controller.io_queue_id;
    sq_cmd.cdw11 = (controller.io_queue_id << 16) |
                   (1 << 0); // CQID | Physically contiguous

    if (!nvme_submit_admin_command(&sq_cmd)) {
        log_err("NVMe: Failed to create I/O submission queue");
        free(controller.io_sq);
        free(controller.io_cq);
        return false;
    }

    return true;
}

static void nvme_identify_namespaces(disk_driver_t *driver)
{
    log_verbose("NVMe: Identifying namespaces...");

    uint32_t *ns_list = (uint32_t *)malloc(4096);
    if (!ns_list) {
        log_err("Failed to allocate memory for NVMe namespace list");
        return;
    }

    nvme_cmd_t cmd = {0};
    cmd.opcode = NVME_ADMIN_CMD_IDENTIFY;
    cmd.nsid = 0;
    cmd.dptr[0] = (uint64_t)ns_list; // Assumes physical == virtual
    cmd.cdw10 = NVME_IDENTIFY_NS_LIST;

    if (!nvme_submit_admin_command(&cmd)) {
        free(ns_list);
        return;
    }

    for (int i = 0; i < 1024; i++) {
        if (ns_list[i] == 0) {
            break;
        }
        uint32_t nsid = ns_list[i];
        nvme_identify_ns_t *id_ns_data =
            (nvme_identify_ns_t *)malloc(sizeof(nvme_identify_ns_t));

        cmd.opcode = NVME_ADMIN_CMD_IDENTIFY;
        cmd.nsid = nsid;
        cmd.dptr[0] = (uint64_t)id_ns_data;
        cmd.cdw10 = NVME_IDENTIFY_NAMESPACE;

        if (nvme_submit_admin_command(&cmd)) {
            log_info("NVMe: Found namespace %d with size %ld sectors", nsid,
                     id_ns_data->nsze);
            char name[6];
            snprintf(name, sizeof(name), "nvme%d", disk_get_count());
            register_disk(driver, (void *)(uintptr_t)nsid, name,
                          id_ns_data->nsze);
        }
        free(id_ns_data);
    }
    free(ns_list);
}

void nvme_init(disk_driver_t *driver)
{
    log_info("NVMe: Initializing driver...");

    // Find NVMe controller on PCI bus
    controller.pci_dev =
        pci_get_device(NVME_CLASS_CODE, NVME_SUBCLASS, NVME_PROG_IF);
    if (controller.pci_dev.vendor_id == 0xFFFF) {
        log_info("NVMe: No controller found.");
        return;
    }

    log_verbose("NVMe: Found controller at PCI %d:%d:%d",
                controller.pci_dev.bus, controller.pci_dev.device,
                controller.pci_dev.function);

    // Get BAR0 and map registers
    uint32_t bar0 = pci_get_bar_address(&controller.pci_dev, 0);
    controller.regs = (nvme_regs_t *)(uint64_t)bar0; // Assumes identity mapping

    // Enable PCI bus mastering
    uint32_t pci_cmd_reg =
        pci_read_dword(controller.pci_dev.bus, controller.pci_dev.device,
                       controller.pci_dev.function, 0x04);
    pci_cmd_reg |= (1 << 2); // Bus Master Enable
    pci_write_dword(controller.pci_dev.bus, controller.pci_dev.device,
                    controller.pci_dev.function, 0x04, pci_cmd_reg);

    // Reset and initialize controller
    // Disable controller
    controller.regs->cc = 0;
    while (controller.regs->csts & CSTS_RDY) {
        wait_ms(1);
    }

    // Setup Admin Queues
    controller.admin_queue_size = 64;
    size_t sq_size = controller.admin_queue_size * sizeof(nvme_cmd_t);
    size_t cq_size = controller.admin_queue_size * sizeof(nvme_cqe_t);

    // NOTE: This memory MUST be physically contiguous.
    controller.admin_sq = (nvme_cmd_t *)malloc(sq_size);
    controller.admin_cq = (nvme_cqe_t *)malloc(cq_size);

    if (!controller.admin_sq || !controller.admin_cq) {
        log_err("NVMe: Failed to allocate admin queues");
        if (controller.admin_sq) {
            free(controller.admin_sq);
        }
        if (controller.admin_cq) {
            free(controller.admin_cq);
        }
        return;
    }

    memset(controller.admin_sq, 0, sq_size);
    memset(controller.admin_cq, 0, cq_size);

    controller.regs->asq =
        (uint64_t)controller.admin_sq; // Assumes physical == virtual
    controller.regs->acq =
        (uint64_t)controller.admin_cq; // Assumes physical == virtual

    controller.regs->aqa = ((controller.admin_queue_size - 1) << 16) |
                           (controller.admin_queue_size - 1);

    // Configure and enable controller
    uint32_t cc_val = 0;
    cc_val |= CC_EN;
    cc_val |= CC_CSS_NVM;
    cc_val |= (6 << CC_IOSQES_SHIFT); // sizeof(nvme_cmd_t) is 64 bytes (2^6)
    cc_val |= (4 << CC_IOCQES_SHIFT); // sizeof(nvme_cqe_t) is 16 bytes (2^4)
    cc_val |= (0 << CC_MPS_SHIFT);    // Page size 4K (ignored if MPS is 0)

    controller.regs->cc = cc_val;

    // Wait until ready
    for (int i = 0; i < 500; i++) {
        if (controller.regs->csts & CSTS_RDY) {
            break;
        }
        wait_ms(1);
    }

    if (!(controller.regs->csts & CSTS_RDY)) {
        log_err("NVMe controller failed to become ready.");
        free(controller.admin_sq);
        free(controller.admin_cq);
        return;
    }

    log_info("NVMe controller is ready.");

    // Identify controller
    if (!nvme_identify_controller()) {
        log_err("Failed to identify NVMe controller.");
        // TODO: cleanup
        return;
    }

    // Setup I/O Queues
    if (!nvme_setup_io_queues()) {
        log_err("Failed to setup NVMe I/O queues.");
        // TODO: cleanup admin queues
        return;
    }

    // Identify namespaces and register disks
    nvme_identify_namespaces(driver);
}

bool nvme_read(struct disk *d, uint64_t lba, uint32_t count, void *buf)
{
    uint32_t nsid = (uint32_t)(uintptr_t)d->driver_data;

    // NOTE: buffer must be physically contiguous
    nvme_cmd_t cmd = {0};
    cmd.opcode = NVME_NVM_CMD_READ;
    cmd.nsid = nsid;
    cmd.dptr[0] = (uint64_t)buf; // Assumes physical == virtual

    // Starting LBA (lower 32 bits)
    cmd.cdw10 = (uint32_t)(lba & 0xFFFFFFFF);
    // Starting LBA (upper 32 bits)
    cmd.cdw11 = (uint32_t)(lba >> 32);
    // Number of logical blocks (0's based, so count - 1)
    cmd.cdw12 = (uint32_t)count - 1;

    return nvme_submit_io_command(&cmd);
}

bool nvme_write(struct disk *d, uint64_t lba, uint32_t count, const void *buf)
{
    uint32_t nsid = (uint32_t)(uintptr_t)d->driver_data;

    // NOTE: buffer must be physically contiguous
    nvme_cmd_t cmd = {0};
    cmd.opcode = NVME_NVM_CMD_WRITE;
    cmd.nsid = nsid;
    cmd.dptr[0] = (uint64_t)buf; // Assumes physical == virtual

    // Starting LBA (lower 32 bits)
    cmd.cdw10 = (uint32_t)(lba & 0xFFFFFFFF);
    // Starting LBA (upper 32 bits)
    cmd.cdw11 = (uint32_t)(lba >> 32);
    // Number of logical blocks (0's based, so count - 1)
    cmd.cdw12 = (uint32_t)count - 1;

    return nvme_submit_io_command(&cmd);
}
