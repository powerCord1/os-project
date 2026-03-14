#include <ahci.h>
#include <debug.h>
#include <interrupts.h>
#include <pci.h>
#include <pmm.h>
#include <scheduler.h>
#include <string.h>
#include <vmm.h>

#define HBA_PxCMD_ST 0x0001
#define HBA_PxCMD_FRE 0x0010
#define HBA_PxCMD_FR 0x4000
#define HBA_PxCMD_CR 0x8000

#define AHCI_GHC_IE (1 << 1)
#define AHCI_PORT_IS_DHRS (1 << 0)
#define AHCI_PORT_IS_TFES (1 << 30)

hba_mem_t *ahci_abar;
ahci_port_state_t ahci_port_states[32];
volatile bool ahci_irq_enabled = false;

static uint64_t ahci_irq_handler(uint64_t rsp, void *ctx)
{
    (void)ctx;
    uint32_t is = ahci_abar->is;

    for (int i = 0; i < 32; i++) {
        if (!(is & (1 << i)))
            continue;

        hba_port_t *port = &ahci_abar->ports[i];
        uint32_t port_is = port->is;

        if (port_is & AHCI_PORT_IS_TFES)
            ahci_port_states[i].error = true;

        ahci_port_states[i].done = true;
        port->is = port_is;

        if (ahci_port_states[i].waiting_thread)
            scheduler_unblock(ahci_port_states[i].waiting_thread);
    }

    ahci_abar->is = is;
    return rsp;
}

void ahci_reset(hba_mem_t *abar_ptr)
{
    abar_ptr->ghc |= (1 << 0);
    while (abar_ptr->ghc & 1)
        ;

    abar_ptr->ghc |= (1 << 31);

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
    port->cmd &= ~HBA_PxCMD_ST;
    port->cmd &= ~HBA_PxCMD_FRE;

    int spin = 0;
    while ((port->cmd & HBA_PxCMD_FR || port->cmd & HBA_PxCMD_CR) && spin < 1000) {
        spin++;
    }

    void *cl_phys = pmm_alloc_page();
    void *cl_virt = phys_to_virt(cl_phys);
    memset(cl_virt, 0, 4096);

    port->clb = (uint32_t)(uintptr_t)cl_phys;
    port->clbu = (uint32_t)((uintptr_t)cl_phys >> 32);

    void *fb_phys = (void *)((uintptr_t)cl_phys + 1024);
    port->fb = (uint32_t)(uintptr_t)fb_phys;
    port->fbu = (uint32_t)((uintptr_t)fb_phys >> 32);

    hba_cmd_header_t *cmdheader = (hba_cmd_header_t *)cl_virt;
    for (int i = 0; i < 32; i++) {
        void *ct_phys = pmm_alloc_page();
        void *ct_virt = phys_to_virt(ct_phys);
        memset(ct_virt, 0, 4096);

        cmdheader[i].prdtl = 0;
        cmdheader[i].ctba = (uint32_t)(uintptr_t)ct_phys;
        cmdheader[i].ctbau = (uint32_t)((uintptr_t)ct_phys >> 32);
    }

    port->is = (uint32_t)-1;
    port->ie = AHCI_PORT_IS_DHRS | AHCI_PORT_IS_TFES;

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
        (void *)0xFFFFFFFF40000000,
        (void *)abar_phys, sizeof(hba_mem_t), 0x1B);

    if (ahci_abar == NULL) {
        log_err("AHCI: Failed to map controller memory.");
        return;
    }

    log_info("AHCI: Controller found at 0x%016lx", ahci_abar);

    memset((void *)ahci_port_states, 0, sizeof(ahci_port_states));
    ahci_reset(ahci_abar);

    uint8_t irq_line = pci_read_byte(ahci_dev.bus, ahci_dev.device,
                                      ahci_dev.function, 0x3C);
    if (irq_line < 16) {
        irq_install_handler(irq_line, ahci_irq_handler, NULL);
        ahci_abar->ghc |= AHCI_GHC_IE;
        ahci_irq_enabled = true;
        log_info("AHCI: Interrupts enabled on IRQ %d", irq_line);
    } else {
        log_warn("AHCI: Invalid IRQ line %d, using polling", irq_line);
    }
}
