#include <debug.h>
#include <hda.h>
#include <heap.h>
#include <math.h>
#include <pci.h>
#include <pmm.h>
#include <string.h>
#include <timer.h>
#include <vmm.h>

volatile hda_regs_t *hda_regs = NULL;
volatile uint32_t *hda_corb = NULL;
volatile uint64_t *hda_rirb = NULL;
static uint16_t rirb_rp = 0;

static void hda_reset()
{
    hda_regs->gctl &= ~(1 << 0);
    while (hda_regs->gctl & (1 << 0))
        ;
    wait_ms(1);
    hda_regs->gctl |= (1 << 0);
    while (!(hda_regs->gctl & (1 << 0)))
        ;
    wait_ms(1);
}

static void hda_init_corb_rirb()
{
    hda_regs->corbctl &= ~0x02;
    hda_regs->rirbctl &= ~0x02;

    void *corb_phys = pmm_alloc_page();
    void *corb_virt_base = find_free_virt_pages(1);
    void *mapped = mmap_physical(corb_virt_base, corb_phys, 4096, 0x1B);

    hda_corb = (volatile uint32_t *)mapped;
    hda_rirb = (volatile uint64_t *)((uintptr_t)mapped + 2048);
    memset((void *)hda_corb, 0, 4096);

    hda_regs->corblbase = (uint32_t)(uintptr_t)corb_phys;
    hda_regs->corbubase = (uint32_t)((uintptr_t)corb_phys >> 32);
    hda_regs->corbsize = 0x02;

    hda_regs->corbrp |= (1 << 15);
    while (!(hda_regs->corbrp & (1 << 15)))
        ;
    hda_regs->corbrp &= ~(1 << 15);
    while (hda_regs->corbrp & (1 << 15))
        ;
    hda_regs->corbwp = 0;

    void *rirb_phys = (void *)((uintptr_t)corb_phys + 2048);
    hda_regs->rirblbase = (uint32_t)(uintptr_t)rirb_phys;
    hda_regs->rirbubase = (uint32_t)((uintptr_t)rirb_phys >> 32);
    hda_regs->rirbsize = 0x02;

    hda_regs->rirbwp |= (1 << 15);
    while (hda_regs->rirbwp & (1 << 15))
        ;
    rirb_rp = 0;
    hda_regs->rintcnt = 1;

    hda_regs->corbctl |= 0x02;
    hda_regs->rirbctl |= 0x02;
}

uint64_t hda_send_verb(uint8_t codec, uint8_t node, uint32_t verb)
{
    if (hda_regs == NULL)
        return 0;

    uint32_t command = (codec << 28) | (node << 20) | verb;
    hda_regs->rirbsts = 0x05;

    uint16_t wp = hda_regs->corbwp & 0xFF;
    uint16_t next_wp = (wp + 1) & 0xFF;

    uint32_t timeout = 100000;
    while (next_wp == (hda_regs->corbrp & 0xFF) && timeout--)
        wait_us(1);

    if (timeout == 0) {
        log_err("HDA: CORB timeout");
        return 0;
    }

    hda_corb[next_wp] = command;
    hda_regs->corbwp = next_wp;

    timeout = 100000;
    while (rirb_rp == (hda_regs->rirbwp & 0xFF) && timeout--)
        wait_us(1);

    if (timeout == 0) {
        log_err("HDA: RIRB timeout");
        return 0;
    }

    rirb_rp = (rirb_rp + 1) & 0xFF;
    return hda_rirb[rirb_rp];
}

void hda_play_test_sound()
{
    if (hda_regs == NULL) {
        log_err("HDA: Cannot play sound, controller not initialized.");
        return;
    }
    log_info("HDA: Playing test sound...");

    int num_iss = HDA_GCAP_ISS(hda_regs->gcap);
    volatile hda_stream_regs_t *out_stream =
        (volatile hda_stream_regs_t *)((uintptr_t)hda_regs + 0x80 +
                                       (num_iss * 0x20));

    out_stream->ctl |= HDA_STREAM_CTL_SRST;
    while (!(out_stream->ctl & HDA_STREAM_CTL_SRST))
        ;
    out_stream->ctl &= ~HDA_STREAM_CTL_SRST;
    while (out_stream->ctl & HDA_STREAM_CTL_SRST)
        ;

    out_stream->fmt = 0x0011; // 48kHz, 16-bit, Stereo

    void *bdl_phys = pmm_alloc_page();
    void *bdl_virt = find_free_virt_pages(1);
    hda_bdl_entry_t *bdl = (hda_bdl_entry_t *)mmap_physical(bdl_virt, bdl_phys, 4096, 0x1B);
    memset(bdl, 0, 4096);

    void *buffer_phys = pmm_alloc_page();
    void *buffer_virt = find_free_virt_pages(1);
    int16_t *buffer = (int16_t *)mmap_physical(buffer_virt, buffer_phys, 4096, 0x1B);

    for (int i = 0; i < 1024; i++) {
        buffer[i * 2] = (i % 50 < 25) ? 15000 : -15000;
        buffer[i * 2 + 1] = buffer[i * 2];
    }

    bdl[0].addr_low = (uint32_t)(uintptr_t)buffer_phys;
    bdl[0].addr_high = (uint32_t)((uintptr_t)buffer_phys >> 32);
    bdl[0].length = 4096;
    bdl[0].flags = 0x01;

    out_stream->bdlpl = (uint32_t)(uintptr_t)bdl_phys;
    out_stream->bdlpu = (uint32_t)((uintptr_t)bdl_phys >> 32);
    out_stream->cbl = 4096;
    out_stream->lvi = 0;

    // Probing common DAC nodes (0x02-0x05)
    for (uint8_t node = 0x02; node <= 0x05; node++) {
        hda_send_verb(0, node, 0x70610); // Stream 1, Channel 0
        hda_send_verb(0, node, 0x20011); // Format
    }

    // Probing common Pin nodes (0x14, 0x17, 0x21, 0x03, 0x05)
    uint8_t pins[] = {0x14, 0x17, 0x21, 0x03, 0x05};
    for (int i = 0; i < 5; i++) {
        hda_send_verb(0, pins[i], 0x70740); // Pin Control Out
        hda_send_verb(0, pins[i], 0x3B07F); // Unmute Max Gain
    }

    out_stream->ctl |= (1 << 20); // Stream Tag 1
    out_stream->ctl |= HDA_STREAM_CTL_RUN;

    log_info("HDA: Stream started. Monitoring LPIB...");
    
    uint32_t last_lpib = 0xFFFFFFFF;
    for(int i = 0; i < 20; i++) {
        wait_ms(50);
        uint32_t current_lpib = out_stream->lpib;
        if (current_lpib != last_lpib) {
            log_info("HDA: DMA in progress (LPIB: %d)", current_lpib);
            last_lpib = current_lpib;
        }
    }
}

void hda_init()
{
    log_info("HDA: Scanning for HDA controllers...");

    pci_device_t hda_dev = {0};
    bool found = false;

    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t device = 0; device < 32; device++) {
            for (uint8_t function = 0; function < 8; function++) {
                uint16_t vendor = pci_get_vendor_id(bus, device, function);
                if (vendor == 0xFFFF)
                    continue;

                uint32_t class_reg = pci_read_dword(bus, device, function, 0x08);
                uint8_t base_class = (class_reg >> 24) & 0xFF;
                uint8_t sub_class = (class_reg >> 16) & 0xFF;

                // Log any Multimedia device found
                if (base_class == 0x04) {
                    uint16_t device_id = pci_read_word(bus, device, function, 0x02);
                    log_info("HDA: Found Multimedia device %04x:%04x (Class %02x:%02x)", 
                             vendor, device_id, base_class, sub_class);
                    
                    if (sub_class == 0x03) {
                        hda_dev.vendor_id = vendor;
                        hda_dev.device_id = device_id;
                        hda_dev.bus = bus;
                        hda_dev.device = device;
                        hda_dev.function = function;
                        found = true;
                        break;
                    }
                }
            }
            if (found) break;
        }
        if (found) break;
    }

    if (!found) {
        log_warn("HDA: No controller found.");
        return;
    }

    log_info("HDA: Using controller %04x:%04x at %d:%d:%d", hda_dev.vendor_id,
             hda_dev.device_id, hda_dev.bus, hda_dev.device, hda_dev.function);

    uint16_t command = pci_read_word(hda_dev.bus, hda_dev.device,
                                     hda_dev.function, 0x04);
    command |= (1 << 1) | (1 << 2);
    pci_write_word(hda_dev.bus, hda_dev.device, hda_dev.function, 0x04, command);

    uintptr_t bar_phys = pci_get_bar_address(&hda_dev, 0);
    hda_regs = (volatile hda_regs_t *)mmap_physical(
        (void *)0xFFFFFFFF50000000, (void *)bar_phys, 0x4000, 0x1B);

    if (hda_regs == NULL) {
        log_err("HDA: Failed to map controller memory.");
        return;
    }

    hda_reset();
    hda_init_corb_rirb();

    wait_ms(10);

    uint16_t statests = hda_regs->statests;
    for (int i = 0; i < 15; i++) {
        if (statests & (1 << i)) {
            log_info("HDA: Found codec at address %d", i);
            uint64_t response = hda_send_verb(i, 0, 0xF0000);
            if (response != 0) {
                log_info("HDA: Codec %d Vendor ID: %08x", i, (uint32_t)response);
            }
        }
    }
}
