#include <stddef.h>
#include <stdint.h>
#include <ctype.h>

#include <cmos.h>
#include <debug.h>
#include <disk.h>
#include <fat32.h>
#include <fs.h>
#include <heap.h>
#include <string.h>

#define NT_RES_LOWER_CASE_BASE 0x08
#define NT_RES_LOWER_CASE_EXT 0x10

typedef struct {
    fat32_dir_entry_t *entry;
    uint32_t cluster_lba;
    uint32_t entry_idx;
} find_empty_entry_data_t;

typedef struct {
    const char *filename;
    fat32_dir_entry_t *entry;
    uint32_t cluster_lba;
    uint32_t entry_idx;
    uint32_t parent_dir_cluster;
} find_entry_for_delete_data_t;

// Forward declarations of helper functions
static uint32_t fat32_get_cluster_lba(fat32_fs_t *fs, uint32_t cluster);
static uint32_t fat32_get_next_cluster(fat32_fs_t *fs, uint32_t current_cluster);
static void fat32_set_next_cluster(fat32_fs_t *fs, uint32_t current_cluster,
                                   uint32_t next_cluster);
static uint32_t fat32_find_free_cluster(fat32_fs_t *fs);
static void fat32_format_filename(const fat32_dir_entry_t *entry, char *buffer,
                                  size_t buffer_size);

typedef bool (*dir_entry_callback_t)(fat32_dir_entry_t *entry,
                                     uint32_t cluster_lba, uint32_t entry_idx,
                                     void *user_data);

static bool fat32_iterate_directory(fat32_fs_t *fs, uint32_t start_cluster,
                                    dir_entry_callback_t callback,
                                    void *user_data);

// Driver interface functions

bool fat32_mount_internal(int disk_id, uint32_t lba_start, uint32_t num_sectors,
                          vfs_mount_t *mount)
{
    fat32_fs_t *fs = (fat32_fs_t *)malloc(sizeof(fat32_fs_t));
    if (!fs) {
        log_err("Failed to allocate memory for FAT32 filesystem structure");
        return false;
    }

    fs->disk_id = disk_id;
    fs->lba_start = lba_start;
    fs->num_sectors = num_sectors;

    uint8_t *vbr_data = (uint8_t *)malloc(512);
    if (!vbr_data) {
        log_err("Failed to allocate memory for VBR");
        free(fs);
        return false;
    }

    if (!disk_read(disk_id, lba_start, 1, vbr_data)) {
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

    // Basic check for FAT32
    if (memcmp(bs->fs_type, "FAT32   ", 8) != 0) {
        // Not FAT32
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

    mount->fs_data = fs;
    mount->root_cluster = fs->root_cluster;

    log_info("FAT32: Mounted filesystem on disk %d, LBA start 0x%x",
             fs->disk_id, fs->lba_start);

    free(vbr_data);
    return true;
}

bool fat32_unmount_internal(vfs_mount_t *mount)
{
    if (mount->fs_data) {
        free(mount->fs_data);
        mount->fs_data = NULL;
        return true;
    }
    return false;
}

static uint32_t fat32_get_cluster_lba(fat32_fs_t *fs, uint32_t cluster)
{
    return fs->data_start + (cluster - 2) * fs->sectors_per_cluster;
}

static uint32_t fat32_get_next_cluster(fat32_fs_t *fs, uint32_t current_cluster)
{
    uint32_t fat_offset = current_cluster * 4;
    uint32_t fat_sector = fs->fat_start + (fat_offset / fs->bytes_per_sector);
    uint32_t entry_offset = fat_offset % fs->bytes_per_sector;

    uint8_t *fat_sector_data = (uint8_t *)malloc(fs->bytes_per_sector);
    if (!fat_sector_data) {
        return 0;
    }

    if (!disk_read(fs->disk_id, fat_sector, 1, fat_sector_data)) {
        free(fat_sector_data);
        return 0;
    }

    uint32_t next_cluster =
        *((uint32_t *)&fat_sector_data[entry_offset]) & 0x0FFFFFFF;
    free(fat_sector_data);

    return next_cluster;
}

static void fat32_set_next_cluster(fat32_fs_t *fs, uint32_t current_cluster,
                                   uint32_t next_cluster)
{
    uint32_t fat_offset = current_cluster * 4;
    uint32_t fat_sector = fs->fat_start + (fat_offset / fs->bytes_per_sector);
    uint32_t entry_offset = fat_offset % fs->bytes_per_sector;

    uint8_t *fat_sector_data = (uint8_t *)malloc(fs->bytes_per_sector);
    if (!fat_sector_data) {
        return;
    }

    if (!disk_read(fs->disk_id, fat_sector, 1, fat_sector_data)) {
        free(fat_sector_data);
        return;
    }

    *((uint32_t *)&fat_sector_data[entry_offset]) = next_cluster;

    disk_write(fs->disk_id, fat_sector, 1, fat_sector_data);
    free(fat_sector_data);
}

static uint32_t fat32_find_free_cluster(fat32_fs_t *fs)
{
    uint8_t *fat_sector_data = (uint8_t *)malloc(fs->bytes_per_sector);
    if (!fat_sector_data) {
        return 0;
    }

    for (uint32_t fat_sector_idx = 0; fat_sector_idx < fs->fat_size_32;
         fat_sector_idx++) {
        uint32_t current_fat_lba = fs->fat_start + fat_sector_idx;

        if (!disk_read(fs->disk_id, current_fat_lba, 1, fat_sector_data)) {
            free(fat_sector_data);
            return 0;
        }

        for (uint32_t i = 0; i < fs->bytes_per_sector / 4; i++) {
            uint32_t cluster_entry =
                *((uint32_t *)&fat_sector_data[i * 4]) & 0x0FFFFFFF;
            if (cluster_entry == 0) {
                uint32_t free_cluster =
                    (fat_sector_idx * (fs->bytes_per_sector / 4)) + i;
                if (free_cluster < 2) {
                    continue;
                }
                free(fat_sector_data);
                return free_cluster;
            }
        }
    }

    free(fat_sector_data);
    return 0;
}

static void fat32_format_filename(const fat32_dir_entry_t *entry, char *buffer,
                                  size_t buffer_size)
{
    memset(buffer, 0, buffer_size);
    char *p = buffer;

    for (int i = 0;
         i < 8 && entry->filename[i] != ' ' && entry->filename[i] != '\0';
         i++) {
        if (p - buffer < (int)buffer_size - 1) {
            if (entry->nt_res & NT_RES_LOWER_CASE_BASE) {
                *p++ = tolower(entry->filename[i]);
            } else {
                *p++ = entry->filename[i];
            }
        }
    }

    if (entry->ext[0] != ' ') {
        if (p - buffer < (int)buffer_size - 1) {
            *p++ = '.';
        }
        for (int i = 0; i < 3 && entry->ext[i] != ' '; i++) {
            if (p - buffer < (int)buffer_size - 1) {
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

static bool fat32_iterate_directory(fat32_fs_t *fs, uint32_t start_cluster,
                                    dir_entry_callback_t callback,
                                    void *user_data)
{
    uint8_t *cluster_data =
        (uint8_t *)malloc(fs->bytes_per_sector * fs->sectors_per_cluster);
    if (!cluster_data) {
        return false;
    }

    uint32_t current_cluster = start_cluster;
    while (current_cluster >= 2 && (current_cluster & 0x0FFFFFFF) < 0x0FFFFF8) {
        uint32_t cluster_lba = fat32_get_cluster_lba(fs, current_cluster);
        if (!disk_read(fs->disk_id, cluster_lba, fs->sectors_per_cluster,
                       cluster_data)) {
            free(cluster_data);
            return false;
        }

        fat32_dir_entry_t *entry = (fat32_dir_entry_t *)cluster_data;
        for (uint32_t i = 0;
             i < (uint32_t)(fs->bytes_per_sector * fs->sectors_per_cluster) /
                     sizeof(fat32_dir_entry_t);
             i++) {
            if (entry[i].filename[0] == 0x00) { // end of directory
                bool ret = callback(&entry[i], cluster_lba, i, user_data);
                free(cluster_data);
                return ret;
            }

            if (callback(&entry[i], cluster_lba, i, user_data)) {
                free(cluster_data);
                return true;
            }
        }

        current_cluster = fat32_get_next_cluster(fs, current_cluster);
    }

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
            data->error = true;
            return true;
        }
        data->dir_entries = new_entries;
    }
    data->dir_entries[*data->dir_count] = strdup(filename);
    (*data->dir_count)++;

    return false;
}

char **fat32_list_directory_internal(vfs_mount_t *mount, uint32_t cluster,
                                     int *count)
{
    fat32_fs_t *fs = (fat32_fs_t *)mount->fs_data;
    *count = 0;

    int capacity = 16;
    char **dir_entries = malloc(capacity * sizeof(char *));
    if (!dir_entries) {
        return NULL;
    }

    list_dir_data_t list_data = {dir_entries, count, capacity, false};
    fat32_iterate_directory(fs, cluster, list_directory_callback, &list_data);

    if (list_data.error) {
        for (int i = 0; i < *count; i++) {
            free(list_data.dir_entries[i]);
        }
        free(list_data.dir_entries);
        return NULL;
    }

    return list_data.dir_entries;
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

static fat32_dir_entry_t *fat32_find_file(fat32_fs_t *fs, uint32_t cluster,
                                          const char *filename)
{
    char *path_copy = strdup(filename);
    if (!path_copy) {
        return NULL;
    }

    char *token = strtok(path_copy, "/");
    uint32_t current_cluster = cluster;
    fat32_dir_entry_t *current_entry = NULL;

    while (token != NULL) {
        find_file_data_t find_data = {token, NULL};
        fat32_iterate_directory(fs, current_cluster, find_file_callback,
                                &find_data);

        if (current_entry) {
            free(current_entry);
            current_entry = NULL;
        }

        if (find_data.found_entry) {
            current_entry = find_data.found_entry;
            current_cluster = (current_entry->first_cluster_high << 16) |
                              current_entry->first_cluster_low;
            if (current_cluster == 0) {
                current_cluster = fs->root_cluster;
            }
        } else {
            free(path_copy);
            return NULL;
        }
        token = strtok(NULL, "/");
    }

    free(path_copy);
    return current_entry;
}

bool fat32_resolve_path_internal(vfs_mount_t *mount, const char *path,
                                 uint32_t *parent_cluster, char **filename)
{
    fat32_fs_t *fs = (fat32_fs_t *)mount->fs_data;
    char *path_copy = strdup(path);
    if (!path_copy) {
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
                fat32_find_file(fs, fs->root_cluster, dir_path);
            if (!parent_entry) {
                free(path_copy);
                return false;
            }
            if (!(parent_entry->attributes & FAT32_ATTRIBUTE_DIRECTORY)) {
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

bool fat32_write_file_internal(vfs_mount_t *mount, uint32_t parent_cluster,
                               const char *filename, const uint8_t *data,
                               uint32_t size)
{
    fat32_fs_t *fs = (fat32_fs_t *)mount->fs_data;
    if (strlen(filename) > 12) {
        return false;
    }

    find_entry_for_delete_data_t find_existing_data = {filename, NULL, 0, 0,
                                                       parent_cluster};
    bool found_existing = fat32_iterate_directory(
        fs, parent_cluster, find_entry_for_delete_callback, &find_existing_data);

    find_empty_entry_data_t find_empty_data = {NULL, 0, 0};
    uint32_t entry_lba;
    uint32_t entry_idx_in_sector;

    if (found_existing) {
        uint32_t cluster_to_free =
            (find_existing_data.entry->first_cluster_high << 16) |
            find_existing_data.entry->first_cluster_low;
        while (cluster_to_free >= 2 &&
               (cluster_to_free & 0x0FFFFFFF) < 0x0FFFFF8) {
            uint32_t next_cluster = fat32_get_next_cluster(fs, cluster_to_free);
            fat32_set_next_cluster(fs, cluster_to_free, 0);
            cluster_to_free = next_cluster;
        }
        free(find_existing_data.entry);
        entry_lba = find_existing_data.cluster_lba;
        entry_idx_in_sector =
            (find_existing_data.entry_idx * sizeof(fat32_dir_entry_t)) %
            fs->bytes_per_sector;
    } else {
        if (!fat32_iterate_directory(fs, parent_cluster,
                                     find_empty_entry_callback,
                                     &find_empty_data)) {
            return false;
        }
        entry_lba = find_empty_data.cluster_lba;
        entry_idx_in_sector =
            (find_empty_data.entry_idx * sizeof(fat32_dir_entry_t)) %
            fs->bytes_per_sector;
    }

    fat32_dir_entry_t new_entry;
    memset(&new_entry, 0, sizeof(fat32_dir_entry_t));

    char name_8_3[12];
    memset(name_8_3, ' ', 11);
    name_8_3[11] = '\0';
    new_entry.nt_res = 0;

    const char *dot = strrchr(filename, '.');
    int name_len = dot ? (dot - filename) : (int)strlen(filename);
    if (name_len > 8)
        name_len = 8;

    bool base_is_lower = true;
    for (int i = 0; i < name_len; i++) {
        if (filename[i] >= 'A' && filename[i] <= 'Z')
            base_is_lower = false;
        name_8_3[i] = toupper(filename[i]);
    }

    bool ext_is_lower = true;
    if (dot) {
        const char *ext = dot + 1;
        int ext_len = (int)strlen(ext);
        if (ext_len > 3)
            ext_len = 3;
        for (int i = 0; i < ext_len; i++) {
            if (ext[i] >= 'A' && ext[i] <= 'Z')
                ext_is_lower = false;
            name_8_3[8 + i] = toupper(ext[i]);
        }
        if (ext_len == 0)
            ext_is_lower = false;
    } else {
        ext_is_lower = false;
    }

    memcpy(new_entry.filename, name_8_3, 8);
    memcpy(new_entry.ext, name_8_3 + 8, 3);

    datetime_t datetime;
    cmos_get_datetime(&datetime);

    if (base_is_lower)
        new_entry.nt_res |= NT_RES_LOWER_CASE_BASE;
    if (ext_is_lower)
        new_entry.nt_res |= NT_RES_LOWER_CASE_EXT;

    new_entry.attributes = FAT32_ATTRIBUTE_ARCHIVE;
    new_entry.create_time =
        (datetime.hour << 11) | (datetime.minute << 5) | (datetime.second / 2);
    new_entry.create_date =
        ((datetime.year - 1980) << 9) | (datetime.month << 5) | datetime.day;
    new_entry.write_time = new_entry.create_time;
    new_entry.write_date = new_entry.create_date;
    new_entry.file_size = size;

    uint32_t first_data_cluster = 0;
    uint32_t current_data_cluster = 0;
    uint32_t clusters_needed =
        (size + (fs->bytes_per_sector * fs->sectors_per_cluster) - 1) /
        (fs->bytes_per_sector * fs->sectors_per_cluster);
    if (size == 0 && clusters_needed == 0)
        clusters_needed = 1;

    for (uint32_t j = 0; j < clusters_needed; j++) {
        uint32_t new_data_cluster = fat32_find_free_cluster(fs);
        if (new_data_cluster == 0)
            return false;
        if (j == 0) {
            first_data_cluster = new_data_cluster;
            new_entry.first_cluster_low = (uint16_t)(first_data_cluster & 0xFFFF);
            new_entry.first_cluster_high = (uint16_t)((first_data_cluster >> 16) & 0xFFFF);
        } else {
            fat32_set_next_cluster(fs, current_data_cluster, new_data_cluster);
        }
        current_data_cluster = new_data_cluster;
    }
    fat32_set_next_cluster(fs, current_data_cluster, 0x0FFFFFFF);

    uint32_t data_offset = 0;
    current_data_cluster = first_data_cluster;
    while (data_offset < size) {
        uint32_t cluster_lba = fat32_get_cluster_lba(fs, current_data_cluster);
        uint32_t bytes_to_write = fs->bytes_per_sector * fs->sectors_per_cluster;
        if (data_offset + bytes_to_write > size)
            bytes_to_write = size - data_offset;

        uint8_t *write_buffer = (uint8_t *)malloc(fs->bytes_per_sector * fs->sectors_per_cluster);
        if (!write_buffer)
            return false;
        memset(write_buffer, 0, fs->bytes_per_sector * fs->sectors_per_cluster);
        memcpy(write_buffer, data + data_offset, bytes_to_write);

        disk_write(fs->disk_id, cluster_lba, fs->sectors_per_cluster, write_buffer);
        free(write_buffer);
        data_offset += bytes_to_write;
        current_data_cluster = fat32_get_next_cluster(fs, current_data_cluster);
    }

    uint8_t *sector_buffer = (uint8_t *)malloc(fs->bytes_per_sector);
    if (!sector_buffer)
        return false;
    disk_read(fs->disk_id, entry_lba, 1, sector_buffer);
    memcpy(sector_buffer + entry_idx_in_sector, &new_entry, sizeof(fat32_dir_entry_t));
    disk_write(fs->disk_id, entry_lba, 1, sector_buffer);
    free(sector_buffer);

    return true;
}

bool fat32_delete_file_internal(vfs_mount_t *mount, uint32_t cluster,
                                const char *filename)
{
    fat32_fs_t *fs = (fat32_fs_t *)mount->fs_data;
    find_entry_for_delete_data_t find_data = {filename, NULL, 0, 0, cluster};
    if (!fat32_iterate_directory(fs, cluster, find_entry_for_delete_callback,
                                 &find_data)) {
        return false;
    }

    fat32_dir_entry_t *entry_to_delete = find_data.entry;
    uint32_t entry_lba = find_data.cluster_lba +
                         (find_data.entry_idx * sizeof(fat32_dir_entry_t)) /
                             fs->bytes_per_sector;

    entry_to_delete->filename[0] = 0xE5;

    uint8_t *sector_buffer = (uint8_t *)malloc(fs->bytes_per_sector);
    if (!sector_buffer)
        return false;
    disk_read(fs->disk_id, entry_lba, 1, sector_buffer);
    memcpy(sector_buffer + (find_data.entry_idx * sizeof(fat32_dir_entry_t)) %
                               fs->bytes_per_sector,
           entry_to_delete, sizeof(fat32_dir_entry_t));
    disk_write(fs->disk_id, entry_lba, 1, sector_buffer);
    free(sector_buffer);

    uint32_t cluster_to_free = (entry_to_delete->first_cluster_high << 16) |
                               entry_to_delete->first_cluster_low;
    while (cluster_to_free >= 2 && (cluster_to_free & 0x0FFFFFFF) < 0x0FFFFF8) {
        uint32_t next = fat32_get_next_cluster(fs, cluster_to_free);
        fat32_set_next_cluster(fs, cluster_to_free, 0);
        cluster_to_free = next;
    }
    free(entry_to_delete);
    return true;
}

static uint8_t *fat32_read_file_common(vfs_mount_t *mount, uint32_t cluster,
                                       const char *filename, uint32_t *size,
                                       vfs_progress_fn progress, void *ctx)
{
    fat32_fs_t *fs = (fat32_fs_t *)mount->fs_data;
    fat32_dir_entry_t *file_entry = fat32_find_file(fs, cluster, filename);
    if (!file_entry || (file_entry->attributes & FAT32_ATTRIBUTE_DIRECTORY)) {
        if (file_entry)
            free(file_entry);
        return NULL;
    }

    *size = file_entry->file_size;
    uint8_t *file_content = (uint8_t *)malloc(*size + 1);
    if (!file_content) {
        free(file_entry);
        return NULL;
    }

    uint32_t current_cluster = (file_entry->first_cluster_high << 16) |
                               file_entry->first_cluster_low;
    uint32_t bytes_read = 0;
    uint8_t *buffer = (uint8_t *)malloc(fs->bytes_per_sector * fs->sectors_per_cluster);

    while (bytes_read < *size) {
        uint32_t cluster_lba = fat32_get_cluster_lba(fs, current_cluster);
        disk_read(fs->disk_id, cluster_lba, fs->sectors_per_cluster, buffer);

        uint32_t to_copy = fs->bytes_per_sector * fs->sectors_per_cluster;
        if (bytes_read + to_copy > *size)
            to_copy = *size - bytes_read;
        memcpy(file_content + bytes_read, buffer, to_copy);
        bytes_read += to_copy;
        if (progress)
            progress(bytes_read, *size, ctx);
        current_cluster = fat32_get_next_cluster(fs, current_cluster);
    }
    file_content[*size] = '\0';
    free(buffer);
    free(file_entry);
    return file_content;
}

uint8_t *fat32_read_file_internal(vfs_mount_t *mount, uint32_t cluster,
                                  const char *filename, uint32_t *size)
{
    return fat32_read_file_common(mount, cluster, filename, size, NULL, NULL);
}

static uint8_t *fat32_read_file_ex(vfs_mount_t *mount, uint32_t cluster,
                                   const char *filename, uint32_t *size,
                                   vfs_progress_fn progress, void *ctx)
{
    return fat32_read_file_common(mount, cluster, filename, size, progress, ctx);
}

bool fat32_create_directory_internal(vfs_mount_t *mount, uint32_t cluster,
                                     const char *dirname)
{
    fat32_fs_t *fs = (fat32_fs_t *)mount->fs_data;
    find_empty_entry_data_t find_data = {NULL, 0, 0};
    if (!fat32_iterate_directory(fs, cluster, find_empty_entry_callback, &find_data))
        return false;

    uint32_t new_cluster = fat32_find_free_cluster(fs);
    if (new_cluster == 0)
        return false;
    fat32_set_next_cluster(fs, new_cluster, 0x0FFFFFFF);

    fat32_dir_entry_t new_entry;
    memset(&new_entry, 0, sizeof(fat32_dir_entry_t));
    char name_8_3[12];
    memset(name_8_3, ' ', 11);
    name_8_3[11] = '\0';
    bool lower = true;
    for (int i = 0; i < 8 && dirname[i]; i++) {
        name_8_3[i] = toupper(dirname[i]);
        if (dirname[i] != tolower(dirname[i]))
            lower = false;
    }
    memcpy(new_entry.filename, name_8_3, 8);
    memcpy(new_entry.ext, name_8_3 + 8, 3);

    datetime_t dt;
    cmos_get_datetime(&dt);
    if (lower)
        new_entry.nt_res |= NT_RES_LOWER_CASE_BASE;
    new_entry.attributes = FAT32_ATTRIBUTE_DIRECTORY;
    new_entry.create_time = (dt.hour << 11) | (dt.minute << 5) | (dt.second / 2);
    new_entry.create_date = ((dt.year - 1980) << 9) | (dt.month << 5) | dt.day;
    new_entry.write_time = new_entry.create_time;
    new_entry.write_date = new_entry.create_date;
    new_entry.first_cluster_low = (uint16_t)(new_cluster & 0xFFFF);
    new_entry.first_cluster_high = (uint16_t)((new_cluster >> 16) & 0xFFFF);

    uint8_t *sector = (uint8_t *)malloc(fs->bytes_per_sector);
    uint32_t lba = find_data.cluster_lba + (find_data.entry_idx * sizeof(fat32_dir_entry_t)) / fs->bytes_per_sector;
    disk_read(fs->disk_id, lba, 1, sector);
    memcpy(sector + (find_data.entry_idx * sizeof(fat32_dir_entry_t)) % fs->bytes_per_sector, &new_entry, sizeof(fat32_dir_entry_t));
    disk_write(fs->disk_id, lba, 1, sector);
    free(sector);

    uint8_t *dir_data = (uint8_t *)malloc(fs->bytes_per_sector * fs->sectors_per_cluster);
    memset(dir_data, 0, fs->bytes_per_sector * fs->sectors_per_cluster);
    fat32_dir_entry_t *dot = (fat32_dir_entry_t *)dir_data;
    fat32_dir_entry_t *dotdot = (fat32_dir_entry_t *)(dir_data + sizeof(fat32_dir_entry_t));

    memcpy(dot->filename, ".          ", 11);
    dot->attributes = FAT32_ATTRIBUTE_DIRECTORY;
    dot->first_cluster_low = new_entry.first_cluster_low;
    dot->first_cluster_high = new_entry.first_cluster_high;

    memcpy(dotdot->filename, "..         ", 11);
    dotdot->attributes = FAT32_ATTRIBUTE_DIRECTORY;
    uint32_t parent = (cluster == fs->root_cluster) ? 0 : cluster;
    dotdot->first_cluster_low = (uint16_t)(parent & 0xFFFF);
    dotdot->first_cluster_high = (uint16_t)((parent >> 16) & 0xFFFF);

    disk_write(fs->disk_id, fat32_get_cluster_lba(fs, new_cluster), fs->sectors_per_cluster, dir_data);
    free(dir_data);

    return true;
}

bool fat32_delete_directory_internal(vfs_mount_t *mount, uint32_t cluster,
                                     const char *dirname)
{
    fat32_fs_t *fs = (fat32_fs_t *)mount->fs_data;
    find_entry_for_delete_data_t find_data = {dirname, NULL, 0, 0, cluster};
    if (!fat32_iterate_directory(fs, cluster, find_entry_for_delete_callback, &find_data))
        return false;

    fat32_dir_entry_t *entry = find_data.entry;
    if (!(entry->attributes & FAT32_ATTRIBUTE_DIRECTORY)) {
        free(entry);
        return false;
    }

    uint32_t dir_cluster = (entry->first_cluster_high << 16) | entry->first_cluster_low;
    int count = 0;
    char **contents = fat32_list_directory_internal(mount, dir_cluster, &count);
    if (contents) {
        if (count > 2) { // . and ..
            for (int i = 0; i < count; i++) free(contents[i]);
            free(contents);
            free(entry);
            return false;
        }
        for (int i = 0; i < count; i++) free(contents[i]);
        free(contents);
    }

    entry->filename[0] = 0xE5;
    uint32_t lba = find_data.cluster_lba + (find_data.entry_idx * sizeof(fat32_dir_entry_t)) / fs->bytes_per_sector;
    uint8_t *sector = (uint8_t *)malloc(fs->bytes_per_sector);
    disk_read(fs->disk_id, lba, 1, sector);
    memcpy(sector + (find_data.entry_idx * sizeof(fat32_dir_entry_t)) % fs->bytes_per_sector, entry, sizeof(fat32_dir_entry_t));
    disk_write(fs->disk_id, lba, 1, sector);
    free(sector);

    uint32_t to_free = dir_cluster;
    while (to_free >= 2 && (to_free & 0x0FFFFFFF) < 0x0FFFFF8) {
        uint32_t next = fat32_get_next_cluster(fs, to_free);
        fat32_set_next_cluster(fs, to_free, 0);
        to_free = next;
    }
    free(entry);
    return true;
}

static fs_driver_t fat32_driver = {
    .name = "FAT32",
    .mount = fat32_mount_internal,
    .unmount = fat32_unmount_internal,
    .list_directory = fat32_list_directory_internal,
    .read_file = fat32_read_file_internal,
    .write_file = fat32_write_file_internal,
    .delete_file = fat32_delete_file_internal,
    .create_directory = fat32_create_directory_internal,
    .delete_directory = fat32_delete_directory_internal,
    .resolve_path = fat32_resolve_path_internal,
    .read_file_ex = fat32_read_file_ex
};

void fat32_init()
{
    fs_register_driver(&fat32_driver);
}
