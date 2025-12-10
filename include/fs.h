#pragma once

#include <stdint.h>
#include <string.h>

typedef struct {
    uint8_t jmp_boot[3];
    uint8_t oem_name[8];
    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint16_t reserved_sector_count;
    uint8_t num_fats;
    uint16_t root_entry_count;
    uint16_t total_sectors_16;
    uint8_t media_type;
    uint16_t fat_size_16;
    uint16_t sectors_per_track;
    uint16_t num_heads;
    uint32_t hidden_sectors;
    uint32_t total_sectors_32;
    // extended boot record for FAT32
    uint32_t fat_size_32;
    uint16_t ext_flags;
    uint16_t fs_version;
    uint32_t root_cluster;
    uint16_t fs_info_sector;
    uint16_t backup_boot_sector;
    uint8_t reserved_0[12];
    uint8_t drive_number;
    uint8_t reserved_1;
    uint8_t boot_signature_ext;
    uint32_t volume_id;
    uint8_t volume_label[11];
    uint8_t fs_type[8]; // "FAT32   "
    uint8_t boot_code[420];
    uint16_t vbr_signature; // 0xAA55
} __attribute__((packed)) fat32_vbr_t;

typedef struct {
    uint8_t drive;
    uint32_t lba_start;
    uint32_t num_sectors;
    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint16_t reserved_sector_count;
    uint8_t num_fats;
    uint32_t root_cluster;
    uint32_t total_sectors_32;
    uint32_t fat_size_32;
    uint32_t fat_start;
    uint32_t data_start;
} fat32_fs_t;

typedef struct {
    uint8_t filename[8];
    uint8_t ext[3];
    uint8_t attributes;
    uint8_t nt_res;
    uint8_t create_time_tenth;
    uint16_t create_time;
    uint16_t create_date;
    uint16_t last_access_date;
    uint16_t first_cluster_high;
    uint16_t write_time;
    uint16_t write_date;
    uint16_t first_cluster_low;
    uint32_t file_size;
} __attribute__((packed)) fat32_dir_entry_t;

#define FAT32_ATTRIBUTE_READ_ONLY 0x01
#define FAT32_ATTRIBUTE_HIDDEN 0x02
#define FAT32_ATTRIBUTE_SYSTEM 0x04
#define FAT32_ATTRIBUTE_VOLUME_ID 0x08
#define FAT32_ATTRIBUTE_DIRECTORY 0x10
#define FAT32_ATTRIBUTE_ARCHIVE 0x20
#define FAT32_ATTRIBUTE_LONG_FILE_NAME                                         \
    (FAT32_ATTRIBUTE_READ_ONLY | FAT32_ATTRIBUTE_HIDDEN |                      \
     FAT32_ATTRIBUTE_SYSTEM | FAT32_ATTRIBUTE_VOLUME_ID)

void fs_init();
bool fat32_mount(uint8_t drive, uint32_t lba_start, uint32_t num_sectors);
bool fat32_unmount();
bool fat32_is_mounted();
fat32_fs_t *fat32_get_mounted_fs();
uint32_t fat32_get_cluster_lba(uint32_t cluster);
uint32_t fat32_get_next_cluster(uint32_t current_cluster);
void fat32_set_next_cluster(fat32_fs_t *fs, uint32_t current_cluster,
                            uint32_t next_cluster);
uint32_t fat32_find_free_cluster(fat32_fs_t *fs);
char **fat32_list_directory(uint32_t cluster, int *dir_count);
fat32_dir_entry_t *fat32_find_file(uint32_t cluster, const char *filename);
void fat32_format_filename(const fat32_dir_entry_t *entry, char *buffer,
                           size_t buffer_size);
bool fat32_write_file(fat32_fs_t *fs, uint32_t parent_cluster,
                      const char *filename, const uint8_t *data, uint32_t size);