#include <ahci.h>
#include <debug.h>
#include <disk.h>
#include <heap.h>
#include <pci.h>
#include <pmm.h>
#include <sata.h>
#include <string.h>
#include <vmm.h>

#define ATA_CMD_READ_DMA_EX 0x25
#define ATA_CMD_IDENTIFY 0xEC

static int check_type(hba_port_t *port)
{
    uint32_t ssts = port->ssts;

    uint8_t ipm = (ssts >> 8) & 0x0F;
    uint8_t det = ssts & 0x0F;

    if (det != HBA_PORT_DET_PRESENT) {
        return 0;
    }
    if (ipm != HBA_PORT_IPM_ACTIVE) {
        return 0;
    }

    switch (port->sig) {
    case SATA_SIG_ATAPI:
        return 2;
    case SATA_SIG_SEMB:
        return 3;
    case SATA_SIG_PM:
        return 4;
    default:
        return 1;
    }
}

static void probe_port(hba_mem_t *ab, disk_driver_t *driver)
{
    uint32_t pi = ab->pi;
    int i = 0;
    while (i < 32) {
        if (pi & 1) {
            int dt = check_type(&ab->ports[i]);
            if (dt == 1) {
                log_info("SATA drive found at port %d", i);
                char name[6];
                strcpy(name, "sd");
                name[2] = 'a' + disk_count;
                name[3] = '\0';
                register_disk(driver, &ab->ports[i], name, 0);
            } else if (dt == 2) {
                log_info("SATAPI drive found at port %d", i);
            } else if (dt == 3) {
                log_info("SEMB drive found at port %d", i);
            } else if (dt == 4) {
                log_info("Port Multiplier found at port %d", i);
            } else {
                log_verbose("No drive found at port %d", i);
            }
        }
        pi >>= 1;
        i++;
    }
}

void sata_init(disk_driver_t *driver)
{
    ahci_init();

    if (ahci_abar == NULL) {
        return;
    }

    probe_port(ahci_abar, driver);
}

int sata_read(hba_port_t *port, uint64_t start, uint32_t count, uint16_t *buf)
{
    port->is = (uint32_t)-1; // Clear pending interrupt bits
    int slot = ahci_find_cmdslot(port);
    log_verbose("sata_read: ahci_find_cmdslot(%p): %d", port, slot);
    if (slot == -1) {
        return 0;
    }

    uint64_t clb_phys = port->clb;
    clb_phys |= ((uint64_t)port->clbu << 32);

    hba_cmd_header_t *cmdheader =
        (hba_cmd_header_t *)phys_to_virt((void *)(uintptr_t)clb_phys);
    cmdheader += slot;
    cmdheader->cfl = sizeof(fis_reg_h2d_t) / sizeof(uint32_t);
    cmdheader->w = 0; // Read from device
    cmdheader->prdtl = 0;

    uint64_t ctba_phys = cmdheader->ctba;
    ctba_phys |= ((uint64_t)cmdheader->ctbau << 32);

    hba_cmd_tbl_t *cmdtbl =
        (hba_cmd_tbl_t *)phys_to_virt((void *)(uintptr_t)ctba_phys);

    // Calculate total bytes and required PRDT entries
    uint32_t byte_count = count << 9; // count * 512
    int prdtl = 0;
    uintptr_t temp_buf_virt = (uintptr_t)buf;

    // Fill PRDT entries
    while (byte_count > 0) {
        uint32_t size = (byte_count > 0x400000) ? 0x400000 : byte_count;

        void *phys_buf = vmm_get_phys((void *)temp_buf_virt);
        if (!phys_buf) {
            log_err("SATA: Failed to get physical address for buffer 0x%lx",
                    temp_buf_virt);
            return 0;
        }

        cmdtbl->prdt_entry[prdtl].dba = (uint32_t)(uintptr_t)phys_buf;
        cmdtbl->prdt_entry[prdtl].dbau = (uint32_t)((uintptr_t)phys_buf >> 32);
        cmdtbl->prdt_entry[prdtl].dbc = size - 1;
        cmdtbl->prdt_entry[prdtl].i = 0;

        temp_buf_virt += size;
        byte_count -= size;
        prdtl++;
    }

    cmdtbl->prdt_entry[prdtl - 1].i = 1;
    cmdheader->prdtl = prdtl;

    memset(cmdtbl->cfis, 0, sizeof(cmdtbl->cfis));

    fis_reg_h2d_t *cmdfis = (fis_reg_h2d_t *)(&cmdtbl->cfis);
    cmdfis->fis_type = FIS_TYPE_REG_H2D;
    cmdfis->c = 1;
    cmdfis->command = ATA_CMD_READ_DMA_EX;

    cmdfis->lba0 = (uint8_t)start;
    cmdfis->lba1 = (uint8_t)(start >> 8);
    cmdfis->lba2 = (uint8_t)(start >> 16);
    cmdfis->device = 1 << 6; // LBA mode

    cmdfis->lba3 = (uint8_t)(start >> 24);
    cmdfis->lba4 = (uint8_t)(start >> 32);
    cmdfis->lba5 = (uint8_t)(start >> 40);

    cmdfis->countl = count & 0xFF;
    cmdfis->counth = (count >> 8) & 0xFF;

    uint64_t spin = 0;
    while ((port->tfd & (0x80 | 0x08)) && spin < 1000000) {
        spin++;
    }
    if (spin == 1000000) {
        log_err("SATA: port is hung");
        return 0;
    }

    port->ci = 1 << slot;

    while (1) {
        if ((port->ci & (1 << slot)) == 0) {
            break;
        }
        if (port->is & (1 << 30)) {
            log_err("SATA: Read disk error");
            return 0;
        }
    }

    return 1;
}
