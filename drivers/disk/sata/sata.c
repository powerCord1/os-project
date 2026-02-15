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
    pci_device_t ahci_dev = pci_get_device(0x01, 0x06, 0x01);

    if (ahci_dev.vendor_id == 0xFFFF) {
        log_warn("SATA: No AHCI controller found.");
        return;
    }

    uintptr_t abar_phys = pci_get_bar_address(&ahci_dev, 5);
    abar = (hba_mem_t *)mmap_physical(
        (void *)0xFFFFFFFF40000000, // A safe "MMIO" virtual range
        (void *)abar_phys, sizeof(hba_mem_t), 0x1B);

    if (abar == NULL) {
        log_err("SATA: Failed to map AHCI controller memory.");
        return;
    }

    log_info("SATA: AHCI controller found at 0x%016lx", abar);

    ahci_reset(abar);

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
    log_verbose("sata_read: find_cmdslot(%p): %d", port, slot);
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

// Rebase a port after reset or initialization
void port_rebase(hba_port_t *port, int portno)
{
    (void)portno;
    // Stop command engine
    port->cmd &= ~HBA_PxCMD_ST;
    port->cmd &= ~HBA_PxCMD_FRE;

    // Wait until FR and CR are cleared
    int spin = 0;
    while ((port->cmd & HBA_PxCMD_FR || port->cmd & HBA_PxCMD_CR) &&
           spin < 1000) {
        spin++;
    }

    // Allocate memory for command list and FIS receive area (one page is enough
    // for both)
    void *cl_phys = pmm_alloc_page();
    void *cl_virt = phys_to_virt(cl_phys);
    memset(cl_virt, 0, 4096);

    port->clb = (uint32_t)(uintptr_t)cl_phys;
    port->clbu = (uint32_t)((uintptr_t)cl_phys >> 32);

    void *fb_phys = (void *)((uintptr_t)cl_phys + 1024);
    port->fb = (uint32_t)(uintptr_t)fb_phys;
    port->fbu = (uint32_t)((uintptr_t)fb_phys >> 32);

    // Allocate memory for command tables (32 tables * 256 bytes = 8KB = 2
    // pages)
    void *ct_phys_base1 = pmm_alloc_page();
    void *ct_phys_base2 = pmm_alloc_page();

    hba_cmd_header_t *cmdheader = (hba_cmd_header_t *)cl_virt;
    for (int i = 0; i < 32; i++) {
        cmdheader[i].prdtl = 8;

        void *ct_phys;
        if (i < 16) {
            ct_phys = (void *)((uintptr_t)ct_phys_base1 + (i * 256));
        } else {
            ct_phys = (void *)((uintptr_t)ct_phys_base2 + ((i - 16) * 256));
        }

        cmdheader[i].ctba = (uint32_t)(uintptr_t)ct_phys;
        cmdheader[i].ctbau = (uint32_t)((uintptr_t)ct_phys >> 32);

        void *ct_virt = phys_to_virt(ct_phys);
        memset(ct_virt, 0, 256);
    }

    // Start command engine
    port->cmd |= HBA_PxCMD_FRE;
    port->cmd |= HBA_PxCMD_ST;
}

void ahci_reset(hba_mem_t *abar_ptr)
{
    abar_ptr->ghc |= (1 << 0);
    while (abar_ptr->ghc & 1)
        ;

    // Enable AHCI mode
    abar_ptr->ghc |= (1 << 31);

    // Rebase all implemented ports
    uint32_t pi = abar_ptr->pi;
    for (int i = 0; i < 32; i++) {
        if (pi & (1 << i)) {
            port_rebase(&abar_ptr->ports[i], i);
        }
    }
}
