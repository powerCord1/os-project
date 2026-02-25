#include <ata.h>
#include <debug.h>
#include <disk.h>
#include <fs.h>

void fs_init()
{
    disk_init();

    for (int disk_id = 0; disk_id < disk_count; disk_id++) {
        partition_entry_t partitions[4];
        uint8_t mbr[512];
        if (!disk_read(disk_id, 0, 1, mbr)) {
            log_warn("fs_init: Failed to read MBR from disk %d", disk_id);
            continue;
        }
        memcpy(partitions, mbr + 446, sizeof(partition_entry_t) * 4);

        for (int i = 0; i < 4; i++) {
            if (partitions[i].num_sectors > 0) {
                // assume FAT32 for now
                if (partitions[i].type == 0x0B || partitions[i].type == 0x0C) {
                    if (fat32_mount(disk_id, partitions[i].lba_start,
                                    partitions[i].num_sectors)) {
                        log_info(
                            "fs_init: Mounted FAT32 on disk %d, partition %d",
                            disk_id, i);
                        return; // exit after first filesystem is mounted
                    }
                }
            }
        }
    }
}