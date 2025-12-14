#include <debug.h>
#include <disk.h>
#include <heap.h>
#include <pci.h>
#include <sata.h>
#include <string.h>

#define ATA_CMD_READ_DMA_EX 0x25
#define ATA_CMD_IDENTIFY 0xEC

#define HBA_PxCMD_ST 0x0001
#define HBA_PxCMD_FRE 0x0010
#define HBA_PxCMD_FR 0x4000
#define HBA_PxCMD_CR 0x8000

static hba_mem_t *abar;

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

void probe_port(hba_mem_t *ab, disk_driver_t *driver)
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
    pci_device_t ahci_dev = pci_get_device(0x01, 0x06, 0x01);

    if (ahci_dev.vendor_id == 0xFFFF) {
        log_warn("SATA: No AHCI controller found.");
        return;
    }

    abar = (hba_mem_t *)((uintptr_t)pci_get_bar_address(&ahci_dev, 5));
    log_info("SATA: AHCI controller found at 0x%x", abar);

    probe_port(abar, driver);
}

static int find_cmdslot(hba_port_t *port)
{
    uint32_t slots = (port->sact | port->ci);
    for (int i = 0; i < 32; i++) {
        if ((slots & 1) == 0) {
            return i;
        }
        slots >>= 1;
    }
    log_warn("SATA: Cannot find free command list slot");
    return -1;
}

int sata_read(hba_port_t *port, uint64_t start, uint32_t count, uint16_t *buf)
{
    port->is = (uint32_t)-1; // Clear pending interrupt bits
    int slot = find_cmdslot(port);
    if (slot == -1) {
        return 0;
    }

    hba_cmd_header_t *cmdheader = (hba_cmd_header_t *)((uintptr_t)port->clb);
    cmdheader += slot;
    cmdheader->cfl = sizeof(fis_reg_h2d_t) / sizeof(uint32_t);
    cmdheader->w = 0;
    cmdheader->prdtl = 1;

    hba_cmd_tbl_t *cmdtbl = (hba_cmd_tbl_t *)((uintptr_t)cmdheader->ctba);
    memset(cmdtbl, 0,
           sizeof(hba_cmd_tbl_t) +
               (cmdheader->prdtl * sizeof(hba_prdt_entry_t)));

    // 8K bytes (16 sectors) per PRDT
    // A single PRDT entry can point to a buffer up to 4MB
    // We are reading 'count' sectors. 1 sector = 512 bytes.
    // So, total bytes = count * 512
    // For simplicity, this implementation assumes the read fits in one PRD.
    // A more robust implementation would loop and create multiple PRDs if
    // needed.
    cmdtbl->prdt_entry[0].dba = (uint32_t)(uintptr_t)buf;
    cmdtbl->prdt_entry[0].dbau = (uint32_t)(((uintptr_t)buf) >> 32);
    cmdtbl->prdt_entry[0].dbc = (count << 9) - 1;
    cmdtbl->prdt_entry[0].i = 1; // Interrupt on completion

    // Setup command
    fis_reg_h2d_t *cmdfis = (fis_reg_h2d_t *)(&cmdtbl->cfis);

    cmdfis->fis_type = FIS_TYPE_REG_H2D;
    cmdfis->c = 1; // Command
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
        if (port->is & (1 << 30)) // Task File Error
        {
            log_err("SATA: Read disk error");
            return 0;
        }
    }

    if (port->is & (1 << 30)) {
        log_err("SATA: Read disk error after command completion");
        return 0;
    }

    return 1;
}

/*
 * The following are placeholders for a more complete driver.
 * For now, they are not implemented.
 */

// Rebase a port after reset or initialization
void port_rebase(hba_port_t *port, int portno)
{
    // Stop command engine
    port->cmd &= ~HBA_PxCMD_ST;
    port->cmd &= ~HBA_PxCMD_FRE;

    // Wait until FR (FIS Receive Running) and CR (Command List Running) are
    // cleared
    while (port->cmd & HBA_PxCMD_FR || port->cmd & HBA_PxCMD_CR)
        ;

    // Allocate memory for command list and FIS receive area
    // For a real OS, you'd use a physical memory allocator.
    // We'll assume malloc gives us a suitable physical address for now.
    void *cmdlist_base = malloc(1024); // 32 cmd headers * 32 bytes = 1KB
    void *fis_base = malloc(256);

    memset(cmdlist_base, 0, 1024);
    memset(fis_base, 0, 256);

    port->clb = (uint32_t)(uintptr_t)cmdlist_base;
    port->clbu = (uint32_t)(((uintptr_t)cmdlist_base) >> 32);
    port->fb = (uint32_t)(uintptr_t)fis_base;
    port->fbu = (uint32_t)(((uintptr_t)fis_base) >> 32);

    // Point each command header to its command table
    hba_cmd_header_t *cmdheader = (hba_cmd_header_t *)((uintptr_t)port->clb);
    for (int i = 0; i < 32; i++) {
        cmdheader[i].prdtl = 8;

        // Allocate 256 bytes for each command table.
        // 8 PRDTs * 16 bytes/PRDT + command FIS + ACMD = 128 + 64 + 16 = 208
        // bytes
        void *cmdtbl_base = malloc(256);
        memset(cmdtbl_base, 0, 256);
        cmdheader[i].ctba = (uint32_t)(uintptr_t)cmdtbl_base;
        cmdheader[i].ctbau = (uint32_t)(((uintptr_t)cmdtbl_base) >> 32);
    }

    // Start command engine
    port->cmd |= HBA_PxCMD_FRE;
    port->cmd |= HBA_PxCMD_ST;
}

void ahci_reset(hba_mem_t *abar)
{
    abar->ghc |= (1 << 0);
    while (abar->ghc & 1)
        ;

    // Enable AHCI mode
    abar->ghc |= (1 << 31);

    // Rebase all implemented ports
    uint32_t pi = abar->pi;
    for (int i = 0; i < 32; i++) {
        if (pi & 1) {
            port_rebase(&abar->ports[i], i);
        }
        pi >>= 1;
    }
}
