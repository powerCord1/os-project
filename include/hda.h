#pragma once

#include <pci.h>
#include <stdint.h>

typedef struct {
    uint16_t gcap;
    uint8_t vmin;
    uint8_t vmaj;
    uint16_t outpay;
    uint16_t inpay;
    uint32_t gctl;
    uint16_t wakeen;
    uint16_t statests;
    uint16_t gsts;
    uint8_t rsvd0[6];
    uint16_t outstrmpay;
    uint16_t instrmpay;
    uint8_t rsvd1[4];
    uint32_t intctl;
    uint32_t intsts;
    uint8_t rsvd2[8];
    uint32_t walclk;
    uint8_t rsvd3[4];
    uint32_t ssync;
    uint8_t rsvd4[4];
    uint32_t corblbase;
    uint32_t corbubase;
    uint16_t corbwp;
    uint16_t corbrp;
    uint8_t corbctl;
    uint8_t corbsts;
    uint8_t corbsize;
    uint8_t rsvd5;
    uint32_t rirblbase;
    uint32_t rirbubase;
    uint16_t rirbwp;
    uint16_t rintcnt;
    uint8_t rirbctl;
    uint8_t rirbsts;
    uint8_t rirbsize;
    uint8_t rsvd6;
    uint32_t icoi;
    uint32_t icii;
    uint16_t icis;
    uint8_t rsvd7[6];
    uint32_t dpiblbase;
    uint32_t dpibubase;
} __attribute__((packed)) hda_regs_t;

typedef struct {
    uint32_t ctl;       // 0x00
    uint32_t sts;       // 0x04
    uint32_t lpib;      // 0x08
    uint32_t cbl;       // 0x0C
    uint16_t lvi;       // 0x10
    uint16_t fifos;     // 0x12
    uint16_t fmt;       // 0x14
    uint16_t rsvd0;     // 0x16
    uint32_t bdlpl;     // 0x18
    uint32_t bdlpu;     // 0x1C
} __attribute__((packed)) hda_stream_regs_t;

typedef struct {
    uint32_t addr_low;
    uint32_t addr_high;
    uint32_t length;
    uint32_t flags;
} __attribute__((packed)) hda_bdl_entry_t;

#define HDA_GCAP_OSS(gcap) (((gcap) >> 12) & 0x0F)
#define HDA_GCAP_ISS(gcap) (((gcap) >> 8) & 0x0F)
#define HDA_GCAP_BSS(gcap) (((gcap) >> 3) & 0x07)

#define HDA_STREAM_CTL_RUN (1 << 1)
#define HDA_STREAM_CTL_SRST (1 << 0)

void hda_init();
void hda_play_test_sound();
