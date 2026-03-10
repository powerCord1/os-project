#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <disk.h>

static void ata_wait_busy(uint8_t drive);
static void ata_wait_drq(uint8_t drive);
void ata_init();
bool ata_read_sectors(uint8_t drive, uint32_t lba, uint8_t num_sectors,
                      uint8_t *buffer);
bool ata_write_sectors(uint8_t drive, uint32_t lba, uint8_t num_sectors,
                       const uint8_t *buffer);
void ata_read_partition_table(uint8_t drive, partition_entry_t *partitions);
bool ata_is_drive_present(uint8_t drive);
void ata_list_partitions(uint8_t drive);