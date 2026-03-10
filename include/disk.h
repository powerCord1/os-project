#pragma once

#include <stdbool.h>
#include <stdint.h>

struct disk;

typedef struct disk_driver {
    const char *name;
    bool (*read_sectors)(struct disk *d, uint64_t lba, uint32_t count,
                         void *buf);
    bool (*write_sectors)(struct disk *d, uint64_t lba, uint32_t count,
                          const void *buf);
} disk_driver_t;

typedef struct disk {
    int id;
    disk_driver_t *driver;
    void *driver_data; // e.g., port number for ATA, hba_port_t* for SATA
    char name[32];
    uint64_t num_sectors;
} disk_t;

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

#define MAX_DISKS 16
extern disk_t disks[MAX_DISKS];
extern int disk_count;

void disk_init();
disk_t *disk_get(int id);
int disk_get_count();

bool disk_read(int disk_id, uint64_t lba, uint32_t count, void *buf);
bool disk_write(int disk_id, uint64_t lba, uint32_t count, const void *buf);

void register_disk(disk_driver_t *driver, void *driver_data, const char *name,
                   uint64_t num_sectors);

bool disk_read_partition_table(int disk_id, partition_entry_t *partitions);

// Forward declare driver init functions
void nvme_init(disk_driver_t *driver);