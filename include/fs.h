#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

typedef enum {
    VFS_FILE,
    VFS_DIRECTORY,
    VFS_DEVICE,
    VFS_INVALID
} vfs_node_type_t;

struct vfs_mount;

typedef void (*vfs_progress_fn)(uint32_t bytes_read, uint32_t total, void *ctx);

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
    void *fs_data;
    int disk_id;
    uint32_t lba_start;
    uint32_t num_sectors;
    uint32_t root_cluster;
    char mount_path[64];
} vfs_mount_t;

void fs_init(void);
void fs_register_driver(fs_driver_t *driver);

// Mount management
bool vfs_mount_at(const char *path, int disk_id, uint32_t lba_start, uint32_t num_sectors);
bool vfs_mount_virtual(const char *path, fs_driver_t *driver);
bool vfs_unmount(const char *path);

// Path-based operations
char **vfs_list(const char *path, int *count);
uint8_t *vfs_read(const char *path, uint32_t *size);
uint8_t *vfs_read_ex(const char *path, uint32_t *size, vfs_progress_fn progress, void *ctx);
bool vfs_write(const char *path, const uint8_t *data, uint32_t size);
bool vfs_delete(const char *path);
bool vfs_mkdir(const char *path);
bool vfs_rmdir(const char *path);
bool vfs_exists(const char *path);
bool vfs_is_dir(const char *path);
vfs_node_type_t vfs_type(const char *path);
