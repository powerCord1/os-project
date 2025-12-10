#include <ata.h>
#include <debug.h>
#include <fs.h>

void fs_init()
{
    for (uint8_t drive = 0; drive < 4; drive++) {
        if (ata_is_drive_present(drive)) {
            partition_entry_t partitions[4];
            ata_read_partition_table(drive, partitions);

            for (int i = 0; i < 4; i++) {
                if (partitions[i].num_sectors > 0) {
                    // assume FAT32 for now
                    if (partitions[i].type == 0x0B ||
                        partitions[i].type == 0x0C) {
                        if (fat32_mount(drive, partitions[i].lba_start,
                                        partitions[i].num_sectors)) {
                            log_info(
                                "Init: Mounted FAT32 on drive %d, partition %d",
                                drive, i);
                            return; // exit after first filesystem is mounted
                        } else {
                            log_warn("Init: Failed to mount FAT32 on drive %d, "
                                     "partition %d",
                                     drive, i);
                        }
                    }
                }
            }
        }
    }
}