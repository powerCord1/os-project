#include <stddef.h>
#include <stdint.h>

#include <ata.h>
#include <cmos.h>
#include <debug.h>
#include <fs.h>
#include <heap.h>
#include <string.h>

#define NT_RES_LOWER_CASE_BASE 0x08
#define NT_RES_LOWER_CASE_EXT 0x10

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

void fat32_set_next_cluster(fat32_fs_t *fs, uint32_t current_cluster,
                            uint32_t next_cluster)
{
    uint32_t fat_offset = current_cluster * 4;
    uint32_t fat_sector = fs->fat_start + (fat_offset / fs->bytes_per_sector);
    uint32_t entry_offset = fat_offset % fs->bytes_per_sector;

    uint8_t *fat_sector_data = (uint8_t *)malloc(fs->bytes_per_sector);
    if (!fat_sector_data) {
        log_err("Failed to allocate memory for FAT sector data");
        return;
    }

    if (!ata_read_sectors(fs->drive, fat_sector, 1, fat_sector_data)) {
        log_err("Failed to read FAT sector at LBA 0x%x", fat_sector);
        free(fat_sector_data);
        return;
    }

    *((uint32_t *)&fat_sector_data[entry_offset]) = next_cluster;

    if (!ata_write_sectors(fs->drive, fat_sector, 1, fat_sector_data)) {
        log_err("Failed to write FAT sector at LBA 0x%x", fat_sector);
    }

    free(fat_sector_data);
}

uint32_t fat32_find_free_cluster(fat32_fs_t *fs)
{
    uint32_t total_clusters = fs->total_sectors_32 / fs->sectors_per_cluster;
    uint32_t fat_sectors_per_read =
        1; // Read one sector at a time for simplicity

    uint8_t *fat_sector_data = (uint8_t *)malloc(fs->bytes_per_sector);
    if (!fat_sector_data) {
        log_err("Failed to allocate memory for FAT sector data");
        return 0;
    }

    for (uint32_t fat_sector_idx = 0; fat_sector_idx < fs->fat_size_32;
         fat_sector_idx += fat_sectors_per_read) {
        uint32_t current_fat_lba = fs->fat_start + fat_sector_idx;

        if (!ata_read_sectors(fs->drive, current_fat_lba, fat_sectors_per_read,
                              fat_sector_data)) {
            log_err("Failed to read FAT sector at LBA 0x%x", current_fat_lba);
            free(fat_sector_data);
            return 0;
        }

        for (uint32_t i = 0; i < fs->bytes_per_sector / 4; i++) {
            uint32_t cluster_entry =
                *((uint32_t *)&fat_sector_data[i * 4]) & 0x0FFFFFFF;
            if (cluster_entry == 0) { // Found a free cluster
                uint32_t free_cluster =
                    (fat_sector_idx * (fs->bytes_per_sector / 4)) + i;
                if (free_cluster < 2) { // Clusters 0 and 1 are reserved
                    continue;
                }
                free(fat_sector_data);
                return free_cluster;
            }
        }
    }

    free(fat_sector_data);
    log_warn("No free clusters found on FAT32 filesystem.");
    return 0;
}

void fat32_format_filename(const fat32_dir_entry_t *entry, char *buffer,
                           size_t buffer_size)
{
    memset(buffer, 0, buffer_size);
    char *p = buffer;

    // copy and trim filename part
    for (int i = 0; i < 8 && entry->filename[i] != ' '; i++) {
        if (p - buffer < buffer_size - 1) {
            if (entry->nt_res & NT_RES_LOWER_CASE_BASE) {
                *p++ = tolower(entry->filename[i]);
            } else {
                *p++ = entry->filename[i];
            }
        }
    }

    // copy and trim extension part
    if (entry->ext[0] != ' ') {
        if (p - buffer < buffer_size - 1) {
            *p++ = '.';
        }
        for (int i = 0; i < 3 && entry->ext[i] != ' '; i++) {
            if (p - buffer < buffer_size - 1) {
                if (entry->nt_res & NT_RES_LOWER_CASE_EXT) {
                    *p++ = tolower(entry->ext[i]);
                } else {
                    *p++ = entry->ext[i];
                }
            }
        }
    }

    *p = '\0';
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

bool fat32_write_file(fat32_fs_t *fs, uint32_t parent_cluster,
                      const char *filename, const uint8_t *data, uint32_t size)
{
    log_info("FAT32: Writing file '%s' to cluster %d, size %d", filename,
             parent_cluster, size);
    // valid filename? (8.3 format)
    if (strlen(filename) > 12) { // 8 for name, 1 for dot, 3 for extension
        log_err("FAT32: Filename '%s' is too long. Max 8.3 format.", filename);
        return false;
    }

    // find empty directory entry
    uint8_t *cluster_data =
        (uint8_t *)malloc(fs->bytes_per_sector * fs->sectors_per_cluster);
    if (!cluster_data) {
        log_err("Failed to allocate memory for cluster data");
        return false;
    }

    uint32_t current_cluster = parent_cluster;
    fat32_dir_entry_t *new_entry = NULL;
    uint32_t new_entry_lba = 0;
    uint32_t new_entry_offset_in_sector = 0;

    while (current_cluster >= 2 && (current_cluster & 0x0FFFFFFF) < 0x0FFFFF8) {
        uint32_t cluster_lba = fat32_get_cluster_lba(current_cluster);
        if (!ata_read_sectors(fs->drive, cluster_lba, fs->sectors_per_cluster,
                              cluster_data)) {
            log_err("Failed to read cluster %d at LBA 0x%x", current_cluster,
                    cluster_lba);
            free(cluster_data);
            return false;
        }

        fat32_dir_entry_t *entry = (fat32_dir_entry_t *)cluster_data;
        for (uint32_t i = 0;
             i < (fs->bytes_per_sector * fs->sectors_per_cluster) /
                     sizeof(fat32_dir_entry_t);
             i++) {
            if (entry[i].filename[0] == 0x00 ||
                entry[i].filename[0] == 0xE5) { // empty or deleted entry
                new_entry = &entry[i];
                new_entry_lba = cluster_lba + (i * sizeof(fat32_dir_entry_t)) /
                                                  fs->bytes_per_sector;
                new_entry_offset_in_sector =
                    (i * sizeof(fat32_dir_entry_t)) % fs->bytes_per_sector;
                goto found_empty_entry;
            }
        }
        current_cluster = fat32_get_next_cluster(current_cluster);
    }

    // If no empty entry found, we need to extend the directory or find a new
    // cluster. This assumes enough space.
    log_err("FAT32: No empty directory entry found. Directory full or needs "
            "extension.");
    free(cluster_data);
    return false;

found_empty_entry:
    // prepare new directory entry
    memset(new_entry, 0, sizeof(fat32_dir_entry_t));

    // format filename to 8.3
    char name_8_3[12];
    memset(name_8_3, ' ', 11);
    name_8_3[11] = '\0';
    new_entry->nt_res = 0;
    bool base_is_lower = true;
    bool ext_is_lower = true;

    char *dot = strchr(filename, '.');
    if (dot) {
        // copy filename part
        for (int i = 0; i < 8 && &filename[i] < dot; i++) {
            name_8_3[i] = toupper(filename[i]);
        }
        for (size_t i = 0; i < (size_t)(dot - filename); i++) {
            if (filename[i] != tolower(filename[i])) {
                base_is_lower = false;
                break;
            }
        }
        // copy extension part
        for (int i = 0; i < 3 && dot[i + 1] != '\0'; i++) {
            name_8_3[8 + i] = toupper(dot[i + 1]);
        }
        for (size_t i = 0; i < strlen(dot + 1); i++) {
            if (dot[i + 1] != tolower(dot[i + 1])) {
                ext_is_lower = false;
                break;
            }
        }
    } else {
        // no extension
        for (int i = 0; i < 8 && filename[i] != '\0'; i++) {
            name_8_3[i] = toupper(filename[i]);
        }
        for (size_t i = 0; i < strlen(filename); i++) {
            if (filename[i] != tolower(filename[i])) {
                base_is_lower = false;
                break;
            }
        }
        ext_is_lower = false; // No extension
    }
    memcpy(new_entry->filename, name_8_3, 8);
    memcpy(new_entry->ext, name_8_3 + 8, 3);

    datetime_t datetime;
    cmos_get_datetime(&datetime);

    if (base_is_lower) {
        new_entry->nt_res |= NT_RES_LOWER_CASE_BASE;
    }
    if (ext_is_lower) {
        new_entry->nt_res |= NT_RES_LOWER_CASE_EXT;
    }

    new_entry->attributes = FAT32_ATTRIBUTE_ARCHIVE; // regular file
    new_entry->create_time_tenth = 0;
    new_entry->create_time =
        (datetime.hour << 11) | (datetime.minute << 5) | (datetime.second / 2);
    new_entry->create_date =
        ((datetime.year - 1980) << 9) | (datetime.month << 5) | datetime.day;
    new_entry->last_access_date = 0;
    new_entry->write_time =
        (datetime.hour << 11) | (datetime.minute << 5) | (datetime.second / 2);
    new_entry->write_date =
        ((datetime.year - 1980) << 9) | (datetime.month << 5) | datetime.day;
    ;
    new_entry->file_size = size;

    // allocate clusters for the file data
    uint32_t first_data_cluster = 0;
    uint32_t current_data_cluster = 0;
    uint32_t sectors_per_file =
        (size + fs->bytes_per_sector - 1) / fs->bytes_per_sector;
    uint32_t clusters_needed =
        (sectors_per_file + fs->sectors_per_cluster - 1) /
        fs->sectors_per_cluster;

    log_verbose("FAT32: File needs %d clusters", clusters_needed);

    for (uint32_t j = 0; j < clusters_needed; j++) {
        uint32_t new_data_cluster = fat32_find_free_cluster(fs);
        if (new_data_cluster == 0) {
            log_err("FAT32: No free clusters available for file data.");
            // TODO: Clean up partially allocated clusters
            free(cluster_data);
            return false;
        }

        if (j == 0) {
            first_data_cluster = new_data_cluster;
            new_entry->first_cluster_low =
                (uint16_t)(first_data_cluster & 0xFFFF);
            new_entry->first_cluster_high =
                (uint16_t)((first_data_cluster >> 16) & 0xFFFF);
        } else {
            fat32_set_next_cluster(fs, current_data_cluster, new_data_cluster);
        }
        current_data_cluster = new_data_cluster;
    }
    fat32_set_next_cluster(fs, current_data_cluster, 0x0FFFFFFF); // EOF

    // write the file data to clusters
    uint32_t data_offset = 0;
    current_data_cluster = first_data_cluster;
    while (current_data_cluster >= 2 &&
           (current_data_cluster & 0x0FFFFFFF) < 0x0FFFFF8 &&
           data_offset < size) {
        uint32_t cluster_lba = fat32_get_cluster_lba(current_data_cluster);
        uint32_t bytes_to_write_to_cluster =
            fs->bytes_per_sector * fs->sectors_per_cluster;
        if ((data_offset + bytes_to_write_to_cluster) > size) {
            bytes_to_write_to_cluster = size - data_offset;
        }

        // create a buffer for the cluster data, fill with file data, and write
        uint8_t *write_buffer =
            (uint8_t *)malloc(fs->bytes_per_sector * fs->sectors_per_cluster);
        if (!write_buffer) {
            log_err("Failed to allocate buffer for writing cluster data.");
            free(cluster_data);
            return false;
        }
        memset(write_buffer, 0, fs->bytes_per_sector * fs->sectors_per_cluster);
        memcpy(write_buffer, data + data_offset, bytes_to_write_to_cluster);

        if (!ata_write_sectors(fs->drive, cluster_lba, fs->sectors_per_cluster,
                               write_buffer)) {
            log_err("Failed to write cluster %d for file data.",
                    current_data_cluster);
            free(cluster_data);
            free(write_buffer);
            return false;
        }
        free(write_buffer);
        data_offset += bytes_to_write_to_cluster;
        current_data_cluster = fat32_get_next_cluster(current_data_cluster);
    }

    // write the updated directory entry back to disk
    uint32_t sector_offset_in_cluster =
        (new_entry_lba - fat32_get_cluster_lba(current_cluster)) *
        fs->bytes_per_sector;
    if (!ata_write_sectors(fs->drive, new_entry_lba, 1,
                           cluster_data + sector_offset_in_cluster)) {
        log_err("Failed to write directory entry for '%s'", filename);
        free(cluster_data);
        return false;
    }

    free(cluster_data);
    return true;
}

bool fat32_delete_file(uint32_t cluster, const char *filename)
{
    log_info("FAT32: Deleting file '%s' from cluster %d", filename, cluster);

    uint8_t *cluster_data =
        (uint8_t *)malloc(fs->bytes_per_sector * fs->sectors_per_cluster);
    if (!cluster_data) {
        log_err("Failed to allocate memory for cluster data");
        return false;
    }

    uint32_t current_cluster = cluster;
    fat32_dir_entry_t *entry_to_delete = NULL;
    uint32_t entry_lba = 0;
    uint32_t entry_offset_in_sector = 0;

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
            if (entry[i].attributes & FAT32_ATTRIBUTE_LONG_FILE_NAME) {
                continue; // skip LFN entries for now
            }

            char entry_filename[13];
            fat32_format_filename(&entry[i], entry_filename, sizeof(entry[i]));

            if (strcasecmp(entry_filename, filename) == 0) {
                entry_to_delete = &entry[i];
                entry_lba = cluster_lba + (i * sizeof(fat32_dir_entry_t)) /
                                              fs->bytes_per_sector;
                entry_offset_in_sector =
                    (i * sizeof(fat32_dir_entry_t)) % fs->bytes_per_sector;
                goto found_entry_to_delete;
            }
        }
        current_cluster = fat32_get_next_cluster(current_cluster);
    }
end_search:
    free(cluster_data);
    log_warn("FAT32: File '%s' not found for deletion.", filename);
    return false;

found_entry_to_delete:
    // Mark the directory entry as deleted
    entry_to_delete->filename[0] = 0xE5;

    // Write the updated directory entry back to disk
    uint32_t sector_offset_in_cluster =
        (entry_lba - fat32_get_cluster_lba(current_cluster)) *
        fs->bytes_per_sector;
    if (!ata_write_sectors(fs->drive, entry_lba, 1,
                           cluster_data + sector_offset_in_cluster)) {
        log_err("Failed to write updated directory entry for '%s'", filename);
        free(cluster_data);
        return false;
    }

    // Free the clusters associated with the file
    uint32_t first_data_cluster = (entry_to_delete->first_cluster_high << 16) |
                                  entry_to_delete->first_cluster_low;
    uint32_t data_cluster_to_free = first_data_cluster;
    while (data_cluster_to_free >= 2 &&
           (data_cluster_to_free & 0x0FFFFFFF) < 0x0FFFFF8) {
        uint32_t next_data_cluster =
            fat32_get_next_cluster(data_cluster_to_free);
        fat32_set_next_cluster(fs, data_cluster_to_free, 0); // Mark as free
        data_cluster_to_free = next_data_cluster;
    }

    free(cluster_data);
    log_info("FAT32: File '%s' deleted successfully.", filename);
    return true;
}