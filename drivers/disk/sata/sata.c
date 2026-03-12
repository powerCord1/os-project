#include <ahci.h>
#include <debug.h>
#include <disk.h>
#include <pci.h>
#include <sata.h>
#include <scheduler.h>
#include <string.h>
#include <vmm.h>

#define ATA_CMD_READ_DMA_EX 0x25
#define ATA_CMD_WRITE_DMA_EX 0x35
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

static int sata_wait_cmd(hba_port_t *port, int slot)
{
    port->ci = 1 << slot;
    while (1) {
        if ((port->ci & (1 << slot)) == 0)
            break;
        if (port->is & (1 << 30)) {
            log_err("SATA: disk error");
            return 0;
        }
    }
    return 1;
}

static int sata_fill_prdt(hba_cmd_tbl_t *cmdtbl, uintptr_t virt_ptr,
                           uint32_t byte_count)
{
    int prdtl = 0;
    while (byte_count > 0) {
        void *phys = vmm_get_phys((void *)virt_ptr);
        if (!phys) {
            log_err("SATA: Failed to get physical address for 0x%lx", virt_ptr);
            return -1;
        }
        uint32_t page_off = virt_ptr & 0xFFF;
        uint32_t contig = 4096 - page_off;
        if (contig > byte_count)
            contig = byte_count;

        cmdtbl->prdt_entry[prdtl].dba = (uint32_t)(uintptr_t)phys;
        cmdtbl->prdt_entry[prdtl].dbau = (uint32_t)((uintptr_t)phys >> 32);
        cmdtbl->prdt_entry[prdtl].dbc = contig - 1;
        cmdtbl->prdt_entry[prdtl].i = 0;

        virt_ptr += contig;
        byte_count -= contig;
        prdtl++;
    }
    return prdtl;
}

static void sata_setup_fis(hba_cmd_tbl_t *cmdtbl, uint8_t command,
                            uint64_t start, uint32_t count)
{
    memset(cmdtbl->cfis, 0, sizeof(cmdtbl->cfis));
    fis_reg_h2d_t *cmdfis = (fis_reg_h2d_t *)(&cmdtbl->cfis);
    cmdfis->fis_type = FIS_TYPE_REG_H2D;
    cmdfis->c = 1;
    cmdfis->command = command;

    cmdfis->lba0 = (uint8_t)start;
    cmdfis->lba1 = (uint8_t)(start >> 8);
    cmdfis->lba2 = (uint8_t)(start >> 16);
    cmdfis->device = 1 << 6;

    cmdfis->lba3 = (uint8_t)(start >> 24);
    cmdfis->lba4 = (uint8_t)(start >> 32);
    cmdfis->lba5 = (uint8_t)(start >> 40);

    cmdfis->countl = count & 0xFF;
    cmdfis->counth = (count >> 8) & 0xFF;
}

int sata_read(hba_port_t *port, uint64_t start, uint32_t count, uint16_t *buf)
{
    port->is = (uint32_t)-1;
    int slot = ahci_find_cmdslot(port);
    if (slot == -1)
        return 0;

    uint64_t clb_phys = port->clb | ((uint64_t)port->clbu << 32);
    hba_cmd_header_t *cmdheader =
        (hba_cmd_header_t *)phys_to_virt((void *)(uintptr_t)clb_phys) + slot;
    cmdheader->cfl = sizeof(fis_reg_h2d_t) / sizeof(uint32_t);
    cmdheader->w = 0;

    uint64_t ctba_phys = cmdheader->ctba | ((uint64_t)cmdheader->ctbau << 32);
    hba_cmd_tbl_t *cmdtbl =
        (hba_cmd_tbl_t *)phys_to_virt((void *)(uintptr_t)ctba_phys);

    int prdtl = sata_fill_prdt(cmdtbl, (uintptr_t)buf, count << 9);
    if (prdtl < 0)
        return 0;

    cmdtbl->prdt_entry[prdtl - 1].i = 1;
    cmdheader->prdtl = prdtl;

    sata_setup_fis(cmdtbl, ATA_CMD_READ_DMA_EX, start, count);

    uint64_t spin = 0;
    while ((port->tfd & (0x80 | 0x08)) && spin < 1000000)
        spin++;
    if (spin == 1000000) {
        log_err("SATA: port is hung");
        return 0;
    }

    return sata_wait_cmd(port, slot);
}

int sata_write(hba_port_t *port, uint64_t start, uint32_t count,
               const uint16_t *buf)
{
    port->is = (uint32_t)-1;
    int slot = ahci_find_cmdslot(port);
    if (slot == -1)
        return 0;

    uint64_t clb_phys = port->clb | ((uint64_t)port->clbu << 32);
    hba_cmd_header_t *cmdheader =
        (hba_cmd_header_t *)phys_to_virt((void *)(uintptr_t)clb_phys) + slot;
    cmdheader->cfl = sizeof(fis_reg_h2d_t) / sizeof(uint32_t);
    cmdheader->w = 1;

    uint64_t ctba_phys = cmdheader->ctba | ((uint64_t)cmdheader->ctbau << 32);
    hba_cmd_tbl_t *cmdtbl =
        (hba_cmd_tbl_t *)phys_to_virt((void *)(uintptr_t)ctba_phys);

    int prdtl = sata_fill_prdt(cmdtbl, (uintptr_t)buf, count << 9);
    if (prdtl < 0)
        return 0;

    cmdtbl->prdt_entry[prdtl - 1].i = 1;
    cmdheader->prdtl = prdtl;

    sata_setup_fis(cmdtbl, ATA_CMD_WRITE_DMA_EX, start, count);

    uint64_t spin = 0;
    while ((port->tfd & (0x80 | 0x08)) && spin < 1000000)
        spin++;
    if (spin == 1000000) {
        log_err("SATA: port is hung");
        return 0;
    }

    return sata_wait_cmd(port, slot);
}

static int sata_cmd_pages(hba_port_t *port, uint8_t command, uint64_t start,
                          uint32_t count, void *phys_pages[], int num_pages)
{
    port->is = (uint32_t)-1;
    int slot = ahci_find_cmdslot(port);
    if (slot == -1)
        return 0;

    uint64_t clb_phys = port->clb | ((uint64_t)port->clbu << 32);
    hba_cmd_header_t *cmdheader =
        (hba_cmd_header_t *)phys_to_virt((void *)(uintptr_t)clb_phys) + slot;
    cmdheader->cfl = sizeof(fis_reg_h2d_t) / sizeof(uint32_t);
    cmdheader->w = (command == ATA_CMD_WRITE_DMA_EX) ? 1 : 0;

    uint64_t ctba_phys = cmdheader->ctba | ((uint64_t)cmdheader->ctbau << 32);
    hba_cmd_tbl_t *cmdtbl =
        (hba_cmd_tbl_t *)phys_to_virt((void *)(uintptr_t)ctba_phys);

    uint32_t byte_count = count << 9;
    int prdtl = 0;
    for (int i = 0; i < num_pages && byte_count > 0; i++) {
        uint32_t chunk = byte_count > 4096 ? 4096 : byte_count;
        cmdtbl->prdt_entry[prdtl].dba = (uint32_t)(uintptr_t)phys_pages[i];
        cmdtbl->prdt_entry[prdtl].dbau =
            (uint32_t)((uintptr_t)phys_pages[i] >> 32);
        cmdtbl->prdt_entry[prdtl].dbc = chunk - 1;
        cmdtbl->prdt_entry[prdtl].i = 0;
        byte_count -= chunk;
        prdtl++;
    }

    cmdtbl->prdt_entry[prdtl - 1].i = 1;
    cmdheader->prdtl = prdtl;

    sata_setup_fis(cmdtbl, command, start, count);

    uint64_t spin = 0;
    while ((port->tfd & (0x80 | 0x08)) && spin < 1000000)
        spin++;
    if (spin == 1000000) {
        log_err("SATA: port is hung");
        return 0;
    }

    return sata_wait_cmd(port, slot);
}

int sata_read_pages(hba_port_t *port, uint64_t start, uint32_t count,
                    void *phys_pages[], int num_pages)
{
    return sata_cmd_pages(port, ATA_CMD_READ_DMA_EX, start, count,
                          phys_pages, num_pages);
}

int sata_write_pages(hba_port_t *port, uint64_t start, uint32_t count,
                     void *phys_pages[], int num_pages)
{
    return sata_cmd_pages(port, ATA_CMD_WRITE_DMA_EX, start, count,
                          phys_pages, num_pages);
}
