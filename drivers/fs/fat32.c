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
    uint32_t fat_sectors_per_read = 1; // simplified

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
            if (cluster_entry == 0) {
                uint32_t free_cluster =
                    (fat_sector_idx * (fs->bytes_per_sector / 4)) + i;
                if (free_cluster < 2) { // clusters 0 and 1 are reserved
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
    for (int i = 0;
         i < 8 && entry->filename[i] != ' ' && entry->filename[i] != '\0';
         i++) {
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

typedef bool (*dir_entry_callback_t)(fat32_dir_entry_t *entry,
                                     uint32_t cluster_lba, uint32_t entry_idx,
                                     void *user_data);

static bool fat32_iterate_directory(uint32_t start_cluster,
                                    dir_entry_callback_t callback,
                                    void *user_data)
{
    uint8_t *cluster_data =
        (uint8_t *)malloc(fs->bytes_per_sector * fs->sectors_per_cluster);
    if (!cluster_data) {
        log_err("Failed to allocate memory for cluster data");
        return false;
    }

    uint32_t current_cluster = start_cluster;
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
            if (entry[i].filename[0] == 0x00) { // end of directory
                if (callback(&entry[i], cluster_lba, i, user_data)) {
                    free(cluster_data);
                    return true;
                }
                goto end_iteration;
            }

            if (callback(&entry[i], cluster_lba, i, user_data)) {
                free(cluster_data);
                return true;
            }
        }

        current_cluster = fat32_get_next_cluster(current_cluster);
    }

end_iteration:
    free(cluster_data);
    return false;
}

typedef struct {
    char **dir_entries;
    int *dir_count;
    int capacity;
    bool error;
} list_dir_data_t;

static bool list_directory_callback(fat32_dir_entry_t *entry,
                                    uint32_t cluster_lba, uint32_t entry_idx,
                                    void *user_data)
{
    (void)cluster_lba;
    (void)entry_idx;
    list_dir_data_t *data = (list_dir_data_t *)user_data;

    if (entry->filename[0] == 0x00 || // end of directory
        entry->filename[0] == 0xE5 || // deleted entry
        (entry->attributes & FAT32_ATTRIBUTE_VOLUME_ID) ||
        (entry->attributes & FAT32_ATTRIBUTE_LONG_FILE_NAME)) {
        return false;
    }

    char filename[13];
    fat32_format_filename(entry, filename, sizeof(filename));

    if (*data->dir_count >= data->capacity) {
        data->capacity *= 2;
        char **new_entries = (char **)realloc(data->dir_entries,
                                              data->capacity * sizeof(char *));
        if (!new_entries) {
            log_err("Failed to reallocate directory entries");
            data->error = true;
            return true;
        }
        data->dir_entries = new_entries;
    }
    data->dir_entries[*data->dir_count] = strdup(filename);
    (*data->dir_count)++;

    return false;
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

    list_dir_data_t list_data = {dir_entries, dir_count, capacity, false};
    fat32_iterate_directory(cluster, list_directory_callback, &list_data);

    if (list_data.error) {
        for (int i = 0; i < *dir_count; i++) {
            free(list_data.dir_entries[i]);
        }
        free(list_data.dir_entries);
        return NULL;
    }

    // realloc could have changed the pointer
    dir_entries = list_data.dir_entries;
    return dir_entries;
}

typedef struct {
    const char *filename;
    fat32_dir_entry_t *found_entry;
} find_file_data_t;

static bool find_file_callback(fat32_dir_entry_t *entry, uint32_t cluster_lba,
                               uint32_t entry_idx, void *user_data)
{
    (void)cluster_lba;
    (void)entry_idx;
    find_file_data_t *data = (find_file_data_t *)user_data;

    if (entry->filename[0] == 0xE5 ||
        (entry->attributes & FAT32_ATTRIBUTE_VOLUME_ID) ||
        (entry->attributes & FAT32_ATTRIBUTE_LONG_FILE_NAME)) {
        return false;
    }

    char entry_filename[13];
    fat32_format_filename(entry, entry_filename, sizeof(entry_filename));

    if (strcasecmp(entry_filename, data->filename) == 0) {
        data->found_entry =
            (fat32_dir_entry_t *)malloc(sizeof(fat32_dir_entry_t));
        if (data->found_entry) {
            memcpy(data->found_entry, entry, sizeof(fat32_dir_entry_t));
        }
        return true;
    }

    return false;
}

fat32_dir_entry_t *fat32_find_file(uint32_t cluster, const char *filename)
{
    log_info("FAT32: Searching for path '%s' starting from cluster %d",
             filename, cluster);

    char *path_copy = strdup(filename);
    if (!path_copy) {
        log_err("Failed to allocate memory for path copy.");
        return NULL;
    }

    char *token = strtok(path_copy, "/");
    uint32_t current_cluster = cluster;
    fat32_dir_entry_t *current_entry = NULL;

    while (token != NULL) {
        log_verbose("FAT32: Traversing segment '%s' in cluster %d", token,
                    current_cluster);
        find_file_data_t find_data = {token, NULL};
        fat32_iterate_directory(current_cluster, find_file_callback,
                                &find_data);

        if (current_entry) {
            free(current_entry);
            current_entry = NULL;
        }

        if (find_data.found_entry) {
            current_entry = find_data.found_entry;
            current_cluster = (current_entry->first_cluster_high << 16) |
                              current_entry->first_cluster_low;
            // '..' in root directory's direct children
            if (current_cluster == 0) {
                current_cluster = fs->root_cluster;
            }
        } else {
            log_warn("FAT32: Path segment '%s' not found.", token);
            free(path_copy);
            return NULL;
        }
        token = strtok(NULL, "/");
    }

    free(path_copy);
    return current_entry;
}

bool fat32_resolve_path(const char *path, uint32_t *parent_cluster,
                        char **filename)
{
    if (!fs) {
        log_err("FAT32: No filesystem mounted.");
        return false;
    }

    char *path_copy = strdup(path);
    if (!path_copy) {
        log_err("FAT32: strdup(path) failed.");
        return false;
    }

    char *last_slash = strrchr(path_copy, '/');

    if (last_slash) {
        *last_slash = '\0';
        char *dir_path = path_copy;

        if (strlen(dir_path) == 0) {
            *parent_cluster = fs->root_cluster;
        } else {
            fat32_dir_entry_t *parent_entry =
                fat32_find_file(fs->root_cluster, dir_path);
            if (!parent_entry) {
                log_warn("FAT32: Directory not found: %s", dir_path);
                free(path_copy);
                return false;
            }
            if (!(parent_entry->attributes & FAT32_ATTRIBUTE_DIRECTORY)) {
                log_warn("FAT32: %s is not a directory.", dir_path);
                free(parent_entry);
                free(path_copy);
                return false;
            }
            *parent_cluster = (parent_entry->first_cluster_high << 16) |
                              parent_entry->first_cluster_low;
            free(parent_entry);
        }
        *filename = strdup(last_slash + 1);
    } else {
        *parent_cluster = fs->root_cluster;
        *filename = strdup(path);
    }

    free(path_copy);
    return *filename != NULL;
}

typedef struct {
    fat32_dir_entry_t *entry;
    uint32_t cluster_lba;
    uint32_t entry_idx;
} find_empty_entry_data_t;

static bool find_empty_entry_callback(fat32_dir_entry_t *entry,
                                      uint32_t cluster_lba, uint32_t entry_idx,
                                      void *user_data)
{
    find_empty_entry_data_t *data = (find_empty_entry_data_t *)user_data;
    if (entry->filename[0] == 0x00 || entry->filename[0] == 0xE5) {
        data->entry = entry;
        data->cluster_lba = cluster_lba;
        data->entry_idx = entry_idx;
        return true;
    }
    return false;
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
    find_empty_entry_data_t find_data = {NULL, 0, 0};
    if (!fat32_iterate_directory(parent_cluster, find_empty_entry_callback,
                                 &find_data)) {
        log_err("FAT32: No empty directory entry found. Directory full or "
                "needs extension.");
        return false;
    }

    fat32_dir_entry_t new_entry;

    // prepare new directory entry
    memset(&new_entry, 0, sizeof(fat32_dir_entry_t));

    // format filename to 8.3
    char name_8_3[12];
    memset(name_8_3, ' ', 11);
    name_8_3[11] = '\0';
    new_entry.nt_res = 0;

    const char *dot = strrchr(filename, '.');
    int name_len = dot ? (dot - filename) : strlen(filename);
    if (name_len > 8) {
        name_len = 8;
    }

    bool base_is_lower = true;
    for (int i = 0; i < name_len; i++) {
        if (filename[i] >= 'A' && filename[i] <= 'Z') {
            base_is_lower = false;
        }
        name_8_3[i] = toupper(filename[i]);
    }

    bool ext_is_lower = true;
    if (dot) {
        const char *ext = dot + 1;
        int ext_len = strlen(ext);
        if (ext_len > 3) {
            ext_len = 3;
        }

        for (int i = 0; i < ext_len; i++) {
            if (ext[i] >= 'A' && ext[i] <= 'Z') {
                ext_is_lower = false;
            }
            name_8_3[8 + i] = toupper(ext[i]);
        }
        if (ext_len == 0) {
            ext_is_lower = false;
        }
    } else {
        ext_is_lower = false;
    }

    memcpy(new_entry.filename, name_8_3, 8);
    memcpy(new_entry.ext, name_8_3 + 8, 3);

    datetime_t datetime;
    cmos_get_datetime(&datetime);

    if (base_is_lower) {
        new_entry.nt_res |= NT_RES_LOWER_CASE_BASE;
    }
    if (ext_is_lower) {
        new_entry.nt_res |= NT_RES_LOWER_CASE_EXT;
    }

    new_entry.attributes = FAT32_ATTRIBUTE_ARCHIVE; // regular file
    new_entry.create_time_tenth = 0;
    new_entry.create_time =
        (datetime.hour << 11) | (datetime.minute << 5) | (datetime.second / 2);
    new_entry.create_date =
        ((datetime.year - 1980) << 9) | (datetime.month << 5) | datetime.day;
    new_entry.last_access_date = 0;
    new_entry.write_time =
        (datetime.hour << 11) | (datetime.minute << 5) | (datetime.second / 2);
    new_entry.write_date =
        ((datetime.year - 1980) << 9) | (datetime.month << 5) | datetime.day;
    new_entry.file_size = size;

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
            return false;
        }

        if (j == 0) {
            first_data_cluster = new_data_cluster;
            new_entry.first_cluster_low =
                (uint16_t)(first_data_cluster & 0xFFFF);
            new_entry.first_cluster_high =
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
            return false;
        }
        memset(write_buffer, 0, fs->bytes_per_sector * fs->sectors_per_cluster);
        memcpy(write_buffer, data + data_offset, bytes_to_write_to_cluster);

        if (!ata_write_sectors(fs->drive, cluster_lba, fs->sectors_per_cluster,
                               write_buffer)) {
            log_err("Failed to write cluster %d for file data.",
                    current_data_cluster);
            free(write_buffer);
            return false;
        }
        free(write_buffer);
        data_offset += bytes_to_write_to_cluster;
        current_data_cluster = fat32_get_next_cluster(current_data_cluster);
    }

    // write to disk
    uint8_t *sector_buffer = (uint8_t *)malloc(fs->bytes_per_sector);
    if (!sector_buffer) {
        log_err("Failed to allocate memory for sector buffer");
        return false;
    }
    uint32_t new_entry_lba = find_data.cluster_lba +
                             (find_data.entry_idx * sizeof(fat32_dir_entry_t)) /
                                 fs->bytes_per_sector;
    if (!ata_read_sectors(fs->drive, new_entry_lba, 1, sector_buffer)) {
        log_err("Failed to read directory sector for update");
        free(sector_buffer);
        return false;
    }
    memcpy(sector_buffer + (find_data.entry_idx * sizeof(fat32_dir_entry_t)) %
                               fs->bytes_per_sector,
           &new_entry, sizeof(fat32_dir_entry_t));

    if (!ata_write_sectors(fs->drive, new_entry_lba, 1, sector_buffer)) {
        log_err("Failed to write directory entry for '%s'", filename);
        free(sector_buffer);
        return false;
    }

    free(sector_buffer);
    return true;
}

typedef struct {
    const char *filename;
    fat32_dir_entry_t *entry;
    uint32_t cluster_lba;
    uint32_t entry_idx;
    uint32_t parent_dir_cluster;
} find_entry_for_delete_data_t;

static bool find_entry_for_delete_callback(fat32_dir_entry_t *entry,
                                           uint32_t cluster_lba,
                                           uint32_t entry_idx, void *user_data)
{
    find_entry_for_delete_data_t *data =
        (find_entry_for_delete_data_t *)user_data;

    if (entry->filename[0] == 0xE5 ||
        (entry->attributes & FAT32_ATTRIBUTE_LONG_FILE_NAME)) {
        return false;
    }

    char entry_filename[13];
    fat32_format_filename(entry, entry_filename, sizeof(entry_filename));

    if (strcasecmp(entry_filename, data->filename) == 0) {
        data->entry = (fat32_dir_entry_t *)malloc(sizeof(fat32_dir_entry_t));
        if (data->entry) {
            memcpy(data->entry, entry, sizeof(fat32_dir_entry_t));
        }
        data->cluster_lba = cluster_lba;
        data->entry_idx = entry_idx;
        return data->entry != NULL;
    }

    return false;
}

bool fat32_delete_file(uint32_t cluster, const char *filename)
{
    log_info("FAT32: Deleting file '%s' from cluster %d", filename, cluster);

    find_entry_for_delete_data_t find_data = {filename, NULL, 0, 0, cluster};
    if (!fat32_iterate_directory(cluster, find_entry_for_delete_callback,
                                 &find_data)) {
        log_warn("FAT32: File '%s' not found for deletion.", filename);
        free(find_data.entry); // free if allocated but callback returned false
        return false;
    }

    fat32_dir_entry_t *entry_to_delete = find_data.entry;

    uint32_t entry_lba = find_data.cluster_lba +
                         (find_data.entry_idx * sizeof(fat32_dir_entry_t)) /
                             fs->bytes_per_sector;

    // mark as deleted
    entry_to_delete->filename[0] = 0xE5;

    // write to disk
    uint8_t *sector_buffer = (uint8_t *)malloc(fs->bytes_per_sector);
    if (!sector_buffer) {
        log_err("Failed to allocate memory for sector buffer");
        return false;
    }
    if (!ata_read_sectors(fs->drive, entry_lba, 1, sector_buffer)) {
        log_err("Failed to read directory sector for update");
        free(sector_buffer);
        return false;
    }
    memcpy(sector_buffer + (find_data.entry_idx * sizeof(fat32_dir_entry_t)) %
                               fs->bytes_per_sector,
           entry_to_delete, sizeof(fat32_dir_entry_t));

    if (!ata_write_sectors(fs->drive, entry_lba, 1, sector_buffer)) {
        log_err("Failed to write updated directory entry for '%s'", filename);
        free(sector_buffer);
        return false;
    }
    free(sector_buffer);

    // free clusters associated with the file
    uint32_t first_data_cluster = (entry_to_delete->first_cluster_high << 16) |
                                  entry_to_delete->first_cluster_low;
    uint32_t data_cluster_to_free = first_data_cluster;
    while (data_cluster_to_free >= 2 &&
           (data_cluster_to_free & 0x0FFFFFFF) < 0x0FFFFF8) {
        uint32_t next_data_cluster =
            fat32_get_next_cluster(data_cluster_to_free);
        fat32_set_next_cluster(fs, data_cluster_to_free, 0); // mark free
        data_cluster_to_free = next_data_cluster;
    }
    free(entry_to_delete);

    log_info("FAT32: File '%s' deleted successfully.", filename);
    return true;
}

char *fat32_read_file(uint32_t cluster, const char *filename)
{
    log_info("FAT32: Reading file '%s' from cluster %d", filename, cluster);

    fat32_dir_entry_t *file_entry = fat32_find_file(cluster, filename);
    if (!file_entry) {
        log_warn("FAT32: File '%s' not found for reading.", filename);
        return NULL;
    }

    if (file_entry->attributes & FAT32_ATTRIBUTE_DIRECTORY) {
        log_warn("FAT32: '%s' is a directory, not a file.", filename);
        free(file_entry);
        return NULL;
    }

    uint32_t file_size = file_entry->file_size;
    char *file_content =
        (char *)malloc(file_size + 1); // +1 for null terminator
    if (!file_content) {
        log_err("Failed to allocate memory for file content.");
        free(file_entry);
        return NULL;
    }

    uint32_t current_data_cluster =
        (file_entry->first_cluster_high << 16) | file_entry->first_cluster_low;
    uint32_t bytes_read = 0;

    uint8_t *cluster_data_buffer =
        (uint8_t *)malloc(fs->bytes_per_sector * fs->sectors_per_cluster);
    if (!cluster_data_buffer) {
        log_err("Failed to allocate memory for cluster data buffer.");
        free(file_entry);
        free(file_content);
        return NULL;
    }

    while (current_data_cluster >= 2 &&
           (current_data_cluster & 0x0FFFFFFF) < 0x0FFFFF8 &&
           bytes_read < file_size) {
        uint32_t cluster_lba = fat32_get_cluster_lba(current_data_cluster);

        if (!ata_read_sectors(fs->drive, cluster_lba, fs->sectors_per_cluster,
                              cluster_data_buffer)) {
            log_err("Failed to read cluster %d for file data.",
                    current_data_cluster);
            free(file_entry);
            free(file_content);
            free(cluster_data_buffer);
            return NULL;
        }

        uint32_t bytes_to_copy = fs->bytes_per_sector * fs->sectors_per_cluster;
        if ((bytes_read + bytes_to_copy) > file_size) {
            bytes_to_copy = file_size - bytes_read;
        }

        memcpy(file_content + bytes_read, cluster_data_buffer, bytes_to_copy);
        bytes_read += bytes_to_copy;

        current_data_cluster = fat32_get_next_cluster(current_data_cluster);
    }

    file_content[file_size] = '\0';
    free(file_entry);
    free(cluster_data_buffer);
    log_info("FAT32: Successfully read file '%s'.", filename);
    return file_content;
}

bool fat32_create_directory(uint32_t cluster, const char *dirname)

{
    log_info("FAT32: Creating directory '%s' in cluster %d", dirname, cluster);

    find_empty_entry_data_t find_data = {NULL, 0, 0};
    if (!fat32_iterate_directory(cluster, find_empty_entry_callback,
                                 &find_data)) {
        log_err("FAT32: No empty directory entry found for new directory.");
        return false;
    }

    fat32_dir_entry_t new_entry;

    // allocate new cluster for the directory's data
    uint32_t new_dir_cluster = fat32_find_free_cluster(fs);
    if (new_dir_cluster == 0) {
        log_err("FAT32: No free clusters available for new directory.");
        return false;
    }
    fat32_set_next_cluster(fs, new_dir_cluster, 0x0FFFFFFF); // EOF

    memset(&new_entry, 0, sizeof(fat32_dir_entry_t));

    // format directory name to 8.3
    char name_8_3[12];
    memset(name_8_3, ' ', 11);
    name_8_3[11] = '\0';
    new_entry.nt_res = 0;
    bool base_is_lower = true;
    for (int i = 0; i < 8 && dirname[i] != '\0'; i++) {
        name_8_3[i] = toupper(dirname[i]);
        if (dirname[i] != tolower(dirname[i])) {
            base_is_lower = false;
        }
    }
    memcpy(new_entry.filename, name_8_3, 8);
    memcpy(new_entry.ext, name_8_3 + 8, 3);

    datetime_t datetime;
    cmos_get_datetime(&datetime);

    if (base_is_lower) {
        new_entry.nt_res |= NT_RES_LOWER_CASE_BASE;
    }

    new_entry.attributes = FAT32_ATTRIBUTE_DIRECTORY;
    new_entry.create_time_tenth = 0;
    new_entry.create_time =
        (datetime.hour << 11) | (datetime.minute << 5) | (datetime.second / 2);
    new_entry.create_date =
        ((datetime.year - 1980) << 9) | (datetime.month << 5) | datetime.day;
    new_entry.last_access_date = 0;
    new_entry.write_time =
        (datetime.hour << 11) | (datetime.minute << 5) | (datetime.second / 2);
    new_entry.write_date =
        ((datetime.year - 1980) << 9) | (datetime.month << 5) | datetime.day;
    new_entry.first_cluster_low = (uint16_t)(new_dir_cluster & 0xFFFF);
    new_entry.first_cluster_high = (uint16_t)((new_dir_cluster >> 16) & 0xFFFF);
    new_entry.file_size = 0;

    // write the updated directory entry back to disk
    uint8_t *sector_buffer = (uint8_t *)malloc(fs->bytes_per_sector);
    if (!sector_buffer) {
        log_err("Failed to allocate memory for sector buffer");
        return false;
    }
    uint32_t new_entry_lba = find_data.cluster_lba +
                             (find_data.entry_idx * sizeof(fat32_dir_entry_t)) /
                                 fs->bytes_per_sector;
    if (!ata_read_sectors(fs->drive, new_entry_lba, 1, sector_buffer)) {
        log_err("Failed to read directory sector for update");
        free(sector_buffer);
        return false;
    }
    memcpy(sector_buffer + (find_data.entry_idx * sizeof(fat32_dir_entry_t)) %
                               fs->bytes_per_sector,
           &new_entry, sizeof(fat32_dir_entry_t));

    if (!ata_write_sectors(fs->drive, new_entry_lba, 1, sector_buffer)) {
        log_err("Failed to write directory entry for '%s'", dirname);
        free(sector_buffer);
        return false;
    }
    free(sector_buffer);

    // initialize the new directory's cluster with '.' and '..' entries
    uint8_t *new_dir_cluster_data =
        (uint8_t *)malloc(fs->bytes_per_sector * fs->sectors_per_cluster);
    if (!new_dir_cluster_data) {
        log_err("Failed to allocate memory for new directory cluster data");
        return false;
    }
    memset(new_dir_cluster_data, 0,
           fs->bytes_per_sector * fs->sectors_per_cluster);

    fat32_dir_entry_t *dot_entry = (fat32_dir_entry_t *)new_dir_cluster_data;
    fat32_dir_entry_t *dot_dot_entry =
        (fat32_dir_entry_t *)(new_dir_cluster_data + sizeof(fat32_dir_entry_t));

    // create '.' entry
    memset(dot_entry, 0, sizeof(fat32_dir_entry_t));
    memcpy(dot_entry->filename, ".          ", 11);
    dot_entry->attributes = FAT32_ATTRIBUTE_DIRECTORY;
    dot_entry->create_time_tenth = new_entry.create_time_tenth;
    dot_entry->create_time = new_entry.create_time;
    dot_entry->create_date = new_entry.create_date;
    dot_entry->last_access_date = new_entry.last_access_date;
    dot_entry->write_time = new_entry.write_time;
    dot_entry->write_date = new_entry.write_date;
    dot_entry->first_cluster_low = (uint16_t)(new_dir_cluster & 0xFFFF);
    dot_entry->first_cluster_high =
        (uint16_t)((new_dir_cluster >> 16) & 0xFFFF);
    dot_entry->file_size = 0;

    // create '..' entry
    memset(dot_dot_entry, 0, sizeof(fat32_dir_entry_t));
    memcpy(dot_dot_entry->filename, "..         ", 11);
    dot_dot_entry->attributes = FAT32_ATTRIBUTE_DIRECTORY;
    dot_dot_entry->create_time_tenth = new_entry.create_time_tenth;
    dot_dot_entry->create_time = new_entry.create_time;
    dot_dot_entry->create_date = new_entry.create_date;
    dot_dot_entry->last_access_date = new_entry.last_access_date;
    dot_dot_entry->write_time = new_entry.write_time;
    dot_dot_entry->write_date = new_entry.write_date;
    uint32_t parent_cluster_for_dot_dot =
        (cluster == fs->root_cluster) ? fs->root_cluster : cluster;
    dot_dot_entry->first_cluster_low =
        (uint16_t)(parent_cluster_for_dot_dot & 0xFFFF);
    dot_dot_entry->first_cluster_high =
        (uint16_t)((parent_cluster_for_dot_dot >> 16) & 0xFFFF);
    dot_dot_entry->file_size = 0;

    if (!ata_write_sectors(fs->drive, fat32_get_cluster_lba(new_dir_cluster),
                           fs->sectors_per_cluster, new_dir_cluster_data)) {
        log_err("Failed to write initial directory entries for '%s'", dirname);
        free(new_dir_cluster_data);
        return false;
    }

    free(new_dir_cluster_data);
    log_info("FAT32: Directory '%s' created successfully.", dirname);
    return true;
}

bool fat32_delete_directory(uint32_t cluster, const char *dirname)
{
    log_info("FAT32: Deleting directory '%s' from cluster %d", dirname,
             cluster);

    find_entry_for_delete_data_t find_data = {dirname, NULL, 0, 0, cluster};
    if (!fat32_iterate_directory(cluster, find_entry_for_delete_callback,
                                 &find_data)) {
        log_warn("FAT32: Directory '%s' not found for deletion.", dirname);
        free(find_data.entry);
        return false;
    }

    fat32_dir_entry_t *entry_to_delete = find_data.entry;
    if (!(entry_to_delete->attributes & FAT32_ATTRIBUTE_DIRECTORY)) {
        log_warn("FAT32: '%s' is not a directory.", dirname);
        free(entry_to_delete);
        return false;
    }

    uint32_t dir_cluster_to_free = (entry_to_delete->first_cluster_high << 16) |
                                   entry_to_delete->first_cluster_low;

    // check if directory is empty (except for . and ..)
    int dir_entry_count = 0;
    char **dir_contents =
        fat32_list_directory(dir_cluster_to_free, &dir_entry_count);
    if (dir_contents) {
        // . and .. are always present, so if count > 2, it's not empty
        if (dir_entry_count > 2) {
            log_warn("FAT32: Directory '%s' is not empty.", dirname);
            for (int k = 0; k < dir_entry_count; k++) {
                free(dir_contents[k]);
            }
            free(dir_contents);
            free(entry_to_delete);
            return false;
        }
        for (int k = 0; k < dir_entry_count; k++) {
            free(dir_contents[k]);
        }
        free(dir_contents);
    }

    // mark as deleted
    entry_to_delete->filename[0] = 0xE5;

    // write to disk
    uint32_t entry_lba = find_data.cluster_lba +
                         (find_data.entry_idx * sizeof(fat32_dir_entry_t)) /
                             fs->bytes_per_sector;
    uint8_t *sector_buffer = (uint8_t *)malloc(fs->bytes_per_sector);
    if (!sector_buffer) {
        log_err("Failed to allocate memory for sector buffer");
        return false;
    }
    if (!ata_read_sectors(fs->drive, entry_lba, 1, sector_buffer)) {
        log_err("Failed to read directory sector for update");
        free(sector_buffer);
        return false;
    }
    memcpy(sector_buffer + (find_data.entry_idx * sizeof(fat32_dir_entry_t)) %
                               fs->bytes_per_sector,
           entry_to_delete, sizeof(fat32_dir_entry_t));

    if (!ata_write_sectors(fs->drive, entry_lba, 1, sector_buffer)) {
        log_err("Failed to write updated directory entry for '%s'", dirname);
        free(sector_buffer);
        return false;
    }
    free(sector_buffer);

    // free clusters
    uint32_t data_cluster_to_free = dir_cluster_to_free;
    while (data_cluster_to_free >= 2 &&
           (data_cluster_to_free & 0x0FFFFFFF) < 0x0FFFFF8) {
        uint32_t next_data_cluster =
            fat32_get_next_cluster(data_cluster_to_free);
        fat32_set_next_cluster(fs, data_cluster_to_free, 0); // mark as free
        data_cluster_to_free = next_data_cluster;
    }
    free(entry_to_delete);

    log_info("FAT32: Directory '%s' deleted successfully.", dirname);
    return true;
}