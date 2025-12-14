#include <stdint.h>

#include <ata.h>
#include <debug.h>
#include <fs.h>
#include <heap.h>
#include <io.h>
#include <string.h>

// ATA PIO registers
#define ATA_PRIMARY_DATA 0x1F0
#define ATA_PRIMARY_ERROR 0x1F1
#define ATA_PRIMARY_SECTOR_COUNT 0x1F2
#define ATA_PRIMARY_LBA_LOW 0x1F3
#define ATA_PRIMARY_LBA_MID 0x1F4
#define ATA_PRIMARY_LBA_HIGH 0x1F5
#define ATA_PRIMARY_DRIVE_SELECT 0x1F6
#define ATA_PRIMARY_COMMAND 0x1F7
#define ATA_PRIMARY_STATUS 0x1F7

#define ATA_SECONDARY_DATA 0x170
#define ATA_SECONDARY_ERROR 0x171
#define ATA_SECONDARY_SECTOR_COUNT 0x172
#define ATA_SECONDARY_LBA_LOW 0x173
#define ATA_SECONDARY_LBA_MID 0x174
#define ATA_SECONDARY_LBA_HIGH 0x175
#define ATA_SECONDARY_DRIVE_SELECT 0x176
#define ATA_SECONDARY_COMMAND 0x177
#define ATA_SECONDARY_STATUS 0x177

// status register bits
#define ATA_SR_BSY 0x80
#define ATA_SR_DRDY 0x40
#define ATA_SR_DF 0x20
#define ATA_SR_SRV 0x10
#define ATA_SR_DRQ 0x08
#define ATA_SR_CORR 0x04
#define ATA_SR_IDX 0x02
#define ATA_SR_ERR 0x01

// commands
#define ATA_CMD_READ_PIO 0x20
#define ATA_CMD_WRITE_PIO 0x30
#define ATA_CMD_IDENTIFY 0xEC

static uint16_t ata_data_port[2] = {ATA_PRIMARY_DATA, ATA_SECONDARY_DATA};
static uint16_t ata_error_port[2] = {ATA_PRIMARY_ERROR, ATA_SECONDARY_ERROR};
static uint16_t ata_sector_count_port[2] = {ATA_PRIMARY_SECTOR_COUNT,
                                            ATA_SECONDARY_SECTOR_COUNT};
static uint16_t ata_lba_low_port[2] = {ATA_PRIMARY_LBA_LOW,
                                       ATA_SECONDARY_LBA_LOW};
static uint16_t ata_lba_mid_port[2] = {ATA_PRIMARY_LBA_MID,
                                       ATA_SECONDARY_LBA_MID};
static uint16_t ata_lba_high_port[2] = {ATA_PRIMARY_LBA_HIGH,
                                        ATA_SECONDARY_LBA_HIGH};
static uint16_t ata_drive_select_port[2] = {ATA_PRIMARY_DRIVE_SELECT,
                                            ATA_SECONDARY_DRIVE_SELECT};
static uint16_t ata_command_port[2] = {ATA_PRIMARY_COMMAND,
                                       ATA_SECONDARY_COMMAND};
static uint16_t ata_status_port[2] = {ATA_PRIMARY_STATUS, ATA_SECONDARY_STATUS};

static bool ata_drives_present[4] = {false};

static void ata_wait_busy(uint8_t drive)
{
    while (inb(ata_status_port[drive]) & ATA_SR_BSY)
        ;
}

static void ata_wait_drq(uint8_t drive)
{
    while (!(inb(ata_status_port[drive]) & ATA_SR_DRQ))
        ;
}

void ata_init()
{
    log_info("ATA: Initializing ATA driver");
    // probe for ATA devices
    for (int i = 0; i < 2; i++) {
        for (int j = 0; j < 2; j++) {
            uint8_t drive_id = (i * 2) + j;
            log_verbose("ATA: Probing drive %d (Bus %d, Drive %d)", i, j);

            // select drive
            outb(ata_drive_select_port[i], 0xA0 | (j << 4));
            io_wait();
            outb(ata_sector_count_port[i], 0);
            outb(ata_lba_low_port[i], 0);
            outb(ata_lba_mid_port[i], 0);
            outb(ata_lba_high_port[i], 0);
            outb(ata_command_port[i], ATA_CMD_IDENTIFY);
            io_wait();

            if (inb(ata_status_port[i]) == 0) {
                log_verbose("ATA: Drive %d does not exist", drive_id);
                continue;
            }

            ata_wait_busy(i);

            if (inb(ata_lba_mid_port[i]) || inb(ata_lba_high_port[i])) {
                log_warn("ATA: Drive %d is not an ATA device (likely ATAPI)",
                         drive_id);
                continue;
            }

            ata_wait_drq(i);

            uint16_t *buffer = (uint16_t *)malloc(256 * sizeof(uint16_t));
            if (!buffer) {
                log_err("ATA: Failed to allocate buffer for IDENTIFY data");
                continue;
            }

            for (int k = 0; k < 256; k++) {
                buffer[k] = inw(ata_data_port[i]);
            }

            // extract model name
            char model[41];
            for (int k = 0; k < 20; k++) {
                model[k * 2] = buffer[27 + k] >> 8;
                model[k * 2 + 1] = buffer[27 + k] & 0xFF;
            }
            model[40] = '\0';
            // trim leading/trailing spaces
            char *start = model;
            while (*start == ' ') {
                start++;
            }
            char *end = start + strlen(start) - 1;
            while (end > start && *end == ' ') {
                *end-- = '\0';
            }

            log_info("ATA: Found drive %d: %s", drive_id, start);
            ata_drives_present[drive_id] = true;
            free(buffer);
        }
    }
}

bool ata_read_sectors(uint8_t drive, uint32_t lba, uint8_t num_sectors,
                      uint8_t *buffer)
{
    uint8_t bus = drive / 2;
    uint8_t drive_select = drive % 2;

    ata_wait_busy(bus);

    outb(ata_drive_select_port[bus],
         0xE0 | (drive_select << 4) | ((lba >> 24) & 0x0F)); // LBA mode,
                                                             // master/slave
                                                             // bit, LBA
                                                             // bits 24-27
    outb(ata_sector_count_port[bus], num_sectors);
    outb(ata_lba_low_port[bus], (uint8_t)lba);
    outb(ata_lba_mid_port[bus], (uint8_t)(lba >> 8));
    outb(ata_lba_high_port[bus], (uint8_t)(lba >> 16));
    outb(ata_command_port[bus], ATA_CMD_READ_PIO);

    for (int i = 0; i < num_sectors; i++) {
        ata_wait_busy(bus);
        ata_wait_drq(bus);

        if (inb(ata_status_port[bus]) & ATA_SR_ERR) {
            log_err("ATA: Read error on drive %d, LBA 0x%x", drive, lba + i);
            return false;
        }

        for (int j = 0; j < 256; j++) { // 256 words per sector
            ((uint16_t *)buffer)[j + (i * 256)] = inw(ata_data_port[bus]);
        }
    }
    return true;
}

bool ata_write_sectors(uint8_t drive, uint32_t lba, uint8_t num_sectors,
                       const uint8_t *buffer)
{
    uint8_t bus = drive / 2;
    uint8_t drive_select = drive % 2;

    ata_wait_busy(bus);

    outb(ata_drive_select_port[bus],
         0xE0 | (drive_select << 4) | ((lba >> 24) & 0x0F)); // LBA mode,
                                                             // master/slave
                                                             // bit, LBA
                                                             // bits 24-27
    outb(ata_sector_count_port[bus], num_sectors);
    outb(ata_lba_low_port[bus], (uint8_t)lba);
    outb(ata_lba_mid_port[bus], (uint8_t)(lba >> 8));
    outb(ata_lba_high_port[bus], (uint8_t)(lba >> 16));
    outb(ata_command_port[bus], ATA_CMD_WRITE_PIO);

    for (int i = 0; i < num_sectors; i++) {
        ata_wait_busy(bus);
        ata_wait_drq(bus);

        if (inb(ata_status_port[bus]) & ATA_SR_ERR) {
            log_err("ATA: Write error on drive %d, LBA 0x%x", drive, lba + i);
            return false;
        }

        for (int j = 0; j < 256; j++) { // 256 words per sector
            outw(ata_data_port[bus], ((uint16_t *)buffer)[j + (i * 256)]);
        }
        io_wait();
    }
    return true;
}

void ata_read_partition_table(uint8_t drive, partition_entry_t *partitions)
{
    uint8_t *mbr_data = (uint8_t *)malloc(512);
    if (!mbr_data) {
        log_err("ATA: Failed to allocate memory for MBR data");
        return;
    }

    if (!ata_read_sectors(drive, 0, 1, mbr_data)) {
        log_err("ATA: Failed to read MBR from drive %d", drive);
        free(mbr_data);
        return;
    }

    memcpy(partitions, mbr_data + 0x1BE, 4 * sizeof(partition_entry_t));

    free(mbr_data);
}

void ata_list_partitions(uint8_t drive)
{
    log_info("ATA: Listing partitions for drive %d", drive);
    partition_entry_t partitions[4];
    ata_read_partition_table(drive, partitions);

    for (int i = 0; i < 4; i++) {
        if (partitions[i].num_sectors == 0) {
            continue;
        }
        log_info("  Partition %d: Type 0x%x, LBA Start 0x%x, Sectors %d", i,
                 partitions[i].type, partitions[i].lba_start,
                 partitions[i].num_sectors);
    }
}

bool ata_is_drive_present(uint8_t drive)
{
    return drive < 4 ? ata_drives_present[drive] : false;
}