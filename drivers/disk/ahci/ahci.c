#include <ahci.h>
#include <debug.h>
#include <pci.h>
#include <pmm.h>
#include <string.h>
#include <vmm.h>

#define HBA_PxCMD_ST 0x0001
#define HBA_PxCMD_FRE 0x0010
#define HBA_PxCMD_FR 0x4000
#define HBA_PxCMD_CR 0x8000

hba_mem_t *ahci_abar;

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

void port_rebase(hba_port_t *port, int portno)
{
    (void)portno;
    // Stop command engine
    port->cmd &= ~HBA_PxCMD_ST;
    port->cmd &= ~HBA_PxCMD_FRE;

    // Wait until FR and CR are cleared
    int spin = 0;
    while ((port->cmd & HBA_PxCMD_FR || port->cmd & HBA_PxCMD_CR) && spin < 1000) {
        spin++;
    }

    // Allocate memory for command list and FIS receive area (one page is enough for both)
    void *cl_phys = pmm_alloc_page();
    void *cl_virt = phys_to_virt(cl_phys);
    memset(cl_virt, 0, 4096);

    port->clb = (uint32_t)(uintptr_t)cl_phys;
    port->clbu = (uint32_t)((uintptr_t)cl_phys >> 32);

    void *fb_phys = (void *)((uintptr_t)cl_phys + 1024);
    port->fb = (uint32_t)(uintptr_t)fb_phys;
    port->fbu = (uint32_t)((uintptr_t)fb_phys >> 32);

    // Allocate memory for command tables (32 tables * 256 bytes = 8KB = 2 pages)
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

int ahci_find_cmdslot(hba_port_t *port)
{
    uint32_t slots = (port->sact | port->ci);
    for (int i = 0; i < 32; i++) {
        if ((slots & 1) == 0) {
            return i;
        }
        slots >>= 1;
    }
    log_warn("AHCI: Cannot find free command list slot");
    return -1;
}

void ahci_init()
{
    pci_device_t ahci_dev = pci_get_device(0x01, 0x06, 0x01);

    if (ahci_dev.vendor_id == 0xFFFF) {
        log_warn("AHCI: No controller found.");
        return;
    }

    uintptr_t abar_phys = pci_get_bar_address(&ahci_dev, 5);
    ahci_abar = (hba_mem_t *)mmap_physical(
        (void *)0xFFFFFFFF40000000, // A safe "MMIO" virtual range
        (void *)abar_phys, sizeof(hba_mem_t), 0x1B);

    if (ahci_abar == NULL) {
        log_err("AHCI: Failed to map controller memory.");
        return;
    }

    log_info("AHCI: Controller found at 0x%016lx", ahci_abar);

    ahci_reset(ahci_abar);
}
