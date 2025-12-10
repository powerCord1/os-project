#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint8_t boot_flags;
    uint8_t start_head;
    uint8_t start_sector : 6;
    uint8_t start_cylinder_high : 2;
    uint8_t start_cylinder;
    uint8_t type;
    uint8_t end_head;
    uint8_t end_sector : 6;
    uint8_t end_cylinder_high : 2;
    uint8_t end_cylinder;
    uint32_t lba_start;
    uint32_t num_sectors;
} __attribute__((packed)) partition_entry_t;

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