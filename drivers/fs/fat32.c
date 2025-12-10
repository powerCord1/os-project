#include <stddef.h>
#include <stdint.h>

#include <ata.h>
#include <debug.h>
#include <fs.h>
#include <heap.h>
#include <string.h>

static fat32_fs_t *fs = NULL;

bool fat32_mount(uint8_t drive, uint32_t lba_start, uint32_t num_sectors)
{
    fs = (fat32_fs_t *)malloc(sizeof(fat32_fs_t));
    if (!fs) {
        log_err("Failed to allocate memory for FAT32 filesystem structure");
        return NULL;
    }

    fs->drive = drive;
    fs->lba_start = lba_start;
    fs->num_sectors = num_sectors;

    uint8_t *vbr_data = (uint8_t *)malloc(512);
    if (!vbr_data) {
        log_err("Failed to allocate memory for VBR");
        free(fs);
        return false;
    }

    if (!ata_read_sectors(drive, lba_start, 1, vbr_data)) {
        log_err("Failed to read FAT32 VBR");
        free(vbr_data);
        free(fs);
        return false;
    }

    fat32_vbr_t *bs = (fat32_vbr_t *)vbr_data;

    if (bs->vbr_signature != 0xAA55) {
        log_err("Invalid FAT32 VBR signature: 0x%x", bs->vbr_signature);
        free(vbr_data);
        free(fs);
        return false;
    }

    fs->bytes_per_sector = bs->bytes_per_sector;
    fs->sectors_per_cluster = bs->sectors_per_cluster;
    fs->reserved_sector_count = bs->reserved_sector_count;
    fs->num_fats = bs->num_fats;
    fs->root_cluster = bs->root_cluster;
    fs->total_sectors_32 = bs->total_sectors_32;
    fs->fat_size_32 = bs->fat_size_32;

    fs->fat_start = fs->lba_start + fs->reserved_sector_count;
    fs->data_start = fs->fat_start + (fs->num_fats * fs->fat_size_32);

    log_info("FAT32: Mounted filesystem on drive %d, LBA start 0x%x", fs->drive,
             fs->lba_start);
    log_verbose("FAT32: Bytes per sector: %d", fs->bytes_per_sector);
    log_verbose("FAT32: Sectors per cluster: %d", fs->sectors_per_cluster);
    log_verbose("FAT32: Reserved sector count: %d", fs->reserved_sector_count);
    log_verbose("FAT32: Number of FATs: %d", fs->num_fats);
    log_verbose("FAT32: Root directory cluster: %d", fs->root_cluster);
    log_verbose("FAT32: Total sectors: %d", fs->total_sectors_32);
    log_verbose("FAT32: FAT size (sectors): %d", fs->fat_size_32);
    log_verbose("FAT32: FAT start LBA: 0x%x", fs->fat_start);
    log_verbose("FAT32: Data start LBA: 0x%x", fs->data_start);

    free(vbr_data);
    return true;
}

bool fat32_unmount()
{
    if (fs) {
        log_info("FAT32: Unmounted filesystem on drive %d", fs->drive);
        free(fs);
        fs = NULL;
        return true;
    } else {
        log_warn("FAT32: No filesystem to unmount");
        return false;
    }
}

bool fat32_is_mounted()
{
    return fs != NULL;
}

fat32_fs_t *fat32_get_mounted_fs()
{
    return fs;
}

uint32_t fat32_get_cluster_lba(uint32_t cluster)
{
    return fs->data_start + (cluster - 2) * fs->sectors_per_cluster;
}

uint32_t fat32_get_next_cluster(uint32_t current_cluster)
{
    uint32_t fat_offset = current_cluster * 4;
    uint32_t fat_sector = fs->fat_start + (fat_offset / fs->bytes_per_sector);
    uint32_t entry_offset = fat_offset % fs->bytes_per_sector;

    uint8_t *fat_sector_data = (uint8_t *)malloc(fs->bytes_per_sector);
    if (!fat_sector_data) {
        log_err("Failed to allocate memory for FAT sector data");
        return 0;
    }

    if (!ata_read_sectors(fs->drive, fat_sector, 1, fat_sector_data)) {
        log_err("Failed to read FAT sector at LBA 0x%x", fat_sector);
        free(fat_sector_data);
        return 0;
    }

    uint32_t next_cluster =
        *((uint32_t *)&fat_sector_data[entry_offset]) & 0x0FFFFFFF;
    free(fat_sector_data);

    return next_cluster;
}

void fat32_format_filename(const fat32_dir_entry_t *entry, char *buffer,
                           size_t buffer_size)
{
    memset(buffer, 0, buffer_size);
    char *p = buffer;

    // copy and trim filename part
    for (int i = 0; i < 8 && entry->filename[i] != ' '; i++) {
        if (p - buffer < buffer_size - 1) {
            *p++ = entry->filename[i];
        }
    }

    // copy and trim extension part
    if (entry->ext[0] != ' ') {
        if (p - buffer < buffer_size - 1) {
            *p++ = '.';
        }
        for (int i = 0; i < 3 && entry->ext[i] != ' '; i++) {
            if (p - buffer < buffer_size - 1) {
                *p++ = entry->ext[i];
            }
        }
    }

    *p = '\0';

    // convert to lowercase
    for (int i = 0; buffer[i]; i++) {
        if (buffer[i] >= 'A' && buffer[i] <= 'Z') {
            buffer[i] = buffer[i] - 'A' + 'a';
        }
    }
}

char **fat32_list_directory(uint32_t cluster, int *dir_count)
{
    log_info("FAT32: Listing directory for cluster %d", cluster);
    *dir_count = 0;

    int capacity = 16;
    char **dir_entries = malloc(capacity * sizeof(char *));
    if (!dir_entries) {
        log_err("Failed to allocate memory for directory entries");
        return NULL;
    }

    uint8_t *cluster_data =
        (uint8_t *)malloc(fs->bytes_per_sector * fs->sectors_per_cluster);
    if (!cluster_data) {
        log_err("Failed to allocate memory for cluster data");
        free(dir_entries);
        return NULL;
    }

    uint32_t current_cluster = cluster;
    while (current_cluster >= 2 && (current_cluster & 0x0FFFFFFF) < 0x0FFFFF8) {
        uint32_t cluster_lba = fat32_get_cluster_lba(current_cluster);
        if (!ata_read_sectors(fs->drive, cluster_lba, fs->sectors_per_cluster,
                              cluster_data)) {
            log_err("Failed to read cluster %d at LBA 0x%x", current_cluster,
                    cluster_lba);
            break;
        }

        fat32_dir_entry_t *entry = (fat32_dir_entry_t *)cluster_data;
        for (uint32_t i = 0;
             i < (fs->bytes_per_sector * fs->sectors_per_cluster) /
                     sizeof(fat32_dir_entry_t);
             i++) {
            if (entry[i].filename[0] == 0x00) { // end of directory
                goto end_list;
            }
            if (entry[i].filename[0] == 0xE5) { // deleted entry
                continue;
            }
            if (entry[i].attributes & FAT32_ATTRIBUTE_VOLUME_ID) {
                continue;
            }
            if (entry[i].attributes & FAT32_ATTRIBUTE_LONG_FILE_NAME) {
                continue; // skip LFN entries for now
            }

            char filename[13];
            fat32_format_filename(&entry[i], filename, sizeof(filename));

            if (*dir_count >= capacity) {
                capacity *= 2;
                char **new_entries =
                    realloc(dir_entries, capacity * sizeof(char *));
                if (!new_entries) {
                    log_err("Failed to reallocate directory entries");
                    for (int k = 0; k < *dir_count; k++) {
                        free(dir_entries[k]);
                    }
                    free(dir_entries);
                    free(cluster_data);
                    return NULL;
                }
                dir_entries = new_entries;
            }
            dir_entries[*dir_count] = strdup(filename);
            (*dir_count)++;
        }

        current_cluster = fat32_get_next_cluster(current_cluster);
    }
end_list:
    free(cluster_data);
    return dir_entries;
}

fat32_dir_entry_t *fat32_find_file(uint32_t cluster, const char *filename)
{
    log_info("FAT32: Searching for file '%s' in cluster %d", filename, cluster);

    uint8_t *cluster_data =
        (uint8_t *)malloc(fs->bytes_per_sector * fs->sectors_per_cluster);
    if (!cluster_data) {
        log_err("Failed to allocate memory for cluster data");
        return NULL;
    }

    uint32_t current_cluster = cluster;
    while (current_cluster >= 2 && (current_cluster & 0x0FFFFFFF) < 0x0FFFFF8) {
        uint32_t cluster_lba = fat32_get_cluster_lba(current_cluster);
        if (!ata_read_sectors(fs->drive, cluster_lba, fs->sectors_per_cluster,
                              cluster_data)) {
            log_err("Failed to read cluster %d at LBA 0x%x", current_cluster,
                    cluster_lba);
            break;
        }

        fat32_dir_entry_t *entry = (fat32_dir_entry_t *)cluster_data;
        for (uint32_t i = 0;
             i < (fs->bytes_per_sector * fs->sectors_per_cluster) /
                     sizeof(fat32_dir_entry_t);
             i++) {
            if (entry[i].filename[0] == 0x00) { // end of directory
                goto end_search;
            }
            if (entry[i].filename[0] == 0xE5) { // deleted entry
                continue;
            }
            if (entry[i].attributes & FAT32_ATTRIBUTE_VOLUME_ID) {
                continue;
            }
            if (entry[i].attributes & FAT32_ATTRIBUTE_LONG_FILE_NAME) {
                continue; // skip LFN entries for now
            }

            char entry_filename[13];
            fat32_format_filename(&entry[i], entry_filename,
                                  sizeof(entry_filename));

            if (strcasecmp(entry_filename, filename) == 0) {
                fat32_dir_entry_t *found_entry =
                    (fat32_dir_entry_t *)malloc(sizeof(fat32_dir_entry_t));
                if (found_entry) {
                    memcpy(found_entry, &entry[i], sizeof(fat32_dir_entry_t));
                }
                free(cluster_data);
                log_info("FAT32: Found file '%s'", filename);
                return found_entry;
            }
        }

        current_cluster = fat32_get_next_cluster(current_cluster);
    }
end_search:
    free(cluster_data);
    log_info("FAT32: File '%s' not found", filename);
    return NULL;
}
