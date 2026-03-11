#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define VFS_INVALID_CLUSTER 0xFFFFFFFF

typedef enum {
    VFS_FILE,
    VFS_DIRECTORY,
    VFS_INVALID
} vfs_node_type_t;

struct vfs_mount;

typedef void (*vfs_progress_fn)(uint32_t bytes_read, uint32_t total, void *ctx);

typedef struct vfs_node {
    char name[256];
    uint32_t size;
    vfs_node_type_t type;
    uint32_t cluster; // For FAT32, or other ID for other FS
    struct vfs_mount *mount;
} vfs_node_t;

typedef struct fs_driver {
    const char *name;
    bool (*mount)(int disk_id, uint32_t lba_start, uint32_t num_sectors, struct vfs_mount *mount);
    bool (*unmount)(struct vfs_mount *mount);
    char **(*list_directory)(struct vfs_mount *mount, uint32_t cluster, int *count);
    uint8_t *(*read_file)(struct vfs_mount *mount, uint32_t cluster, const char *filename, uint32_t *size);
    bool (*write_file)(struct vfs_mount *mount, uint32_t cluster, const char *filename, const uint8_t *data, uint32_t size);
    bool (*delete_file)(struct vfs_mount *mount, uint32_t cluster, const char *filename);
    bool (*create_directory)(struct vfs_mount *mount, uint32_t cluster, const char *dirname);
    bool (*delete_directory)(struct vfs_mount *mount, uint32_t cluster, const char *dirname);
    bool (*resolve_path)(struct vfs_mount *mount, const char *path, uint32_t *parent_cluster, char **filename);
    uint8_t *(*read_file_ex)(struct vfs_mount *mount, uint32_t cluster,
                             const char *filename, uint32_t *size,
                             vfs_progress_fn progress, void *ctx);
} fs_driver_t;

typedef struct vfs_mount {
    fs_driver_t *driver;
    void *fs_data; // filesystem specific data (e.g. fat32_fs_t)
    int disk_id;
    uint32_t lba_start;
    uint32_t num_sectors;
    uint32_t root_cluster;
} vfs_mount_t;

void fs_init();
void fs_register_driver(fs_driver_t *driver);

vfs_mount_t *vfs_get_mounted_fs();
bool vfs_mount(int disk_id, uint32_t lba_start, uint32_t num_sectors);
bool vfs_unmount();

// Wrappers that call the driver-specific functions
char **vfs_list_directory(uint32_t cluster, int *count);
uint8_t *vfs_read_file(uint32_t cluster, const char *filename, uint32_t *size);
bool vfs_write_file(uint32_t cluster, const char *filename, const uint8_t *data, uint32_t size);
bool vfs_delete_file(uint32_t cluster, const char *filename);
bool vfs_create_directory(uint32_t cluster, const char *dirname);
bool vfs_delete_directory(uint32_t cluster, const char *dirname);
bool vfs_resolve_path(const char *path, uint32_t *parent_cluster, char **filename);
uint8_t *vfs_read_file_ex(uint32_t cluster, const char *filename, uint32_t *size,
                          vfs_progress_fn progress, void *ctx);
