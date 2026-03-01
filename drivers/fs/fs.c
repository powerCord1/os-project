#include <debug.h>
#include <disk.h>
#include <fat32.h>
#include <fs.h>
#include <heap.h>
#include <string.h>

#define MAX_DRIVERS 10

static fs_driver_t *drivers[MAX_DRIVERS];
static int driver_count = 0;
static vfs_mount_t *current_mount = NULL;

void fs_register_driver(fs_driver_t *driver)
{
    if (driver_count < MAX_DRIVERS) {
        drivers[driver_count++] = driver;
        log_info("fs: Registered driver: %s", driver->name);
    }
}

vfs_mount_t *vfs_get_mounted_fs()
{
    return current_mount;
}

bool vfs_mount(int disk_id, uint32_t lba_start, uint32_t num_sectors)
{
    if (current_mount) {
        log_warn("vfs_mount: A filesystem is already mounted");
        return false;
    }

    for (int i = 0; i < driver_count; i++) {
        vfs_mount_t *mount = (vfs_mount_t *)malloc(sizeof(vfs_mount_t));
        memset(mount, 0, sizeof(vfs_mount_t));
        mount->driver = drivers[i];
        mount->disk_id = disk_id;
        mount->lba_start = lba_start;
        mount->num_sectors = num_sectors;

        if (drivers[i]->mount(disk_id, lba_start, num_sectors, mount)) {
            current_mount = mount;
            log_info("vfs_mount: Successfully mounted %s on disk %d, LBA %u",
                     drivers[i]->name, disk_id, lba_start);
            return true;
        }
        free(mount);
    }

    return false;
}

bool vfs_unmount()
{
    if (!current_mount) {
        return false;
    }

    if (current_mount->driver->unmount(current_mount)) {
        free(current_mount);
        current_mount = NULL;
        return true;
    }

    return false;
}

void fs_init()
{
    disk_init();
    fat32_init();
}

char **vfs_list_directory(uint32_t cluster, int *count)
{
    if (!current_mount || !current_mount->driver->list_directory) {
        return NULL;
    }
    return current_mount->driver->list_directory(current_mount, cluster, count);
}

uint8_t *vfs_read_file(uint32_t cluster, const char *filename, uint32_t *size)
{
    if (!current_mount || !current_mount->driver->read_file) {
        return NULL;
    }
    return current_mount->driver->read_file(current_mount, cluster, filename,
                                            size);
}

bool vfs_write_file(uint32_t cluster, const char *filename, const uint8_t *data,
                    uint32_t size)
{
    if (!current_mount || !current_mount->driver->write_file) {
        return false;
    }
    return current_mount->driver->write_file(current_mount, cluster, filename,
                                             data, size);
}

bool vfs_delete_file(uint32_t cluster, const char *filename)
{
    if (!current_mount || !current_mount->driver->delete_file) {
        return false;
    }
    return current_mount->driver->delete_file(current_mount, cluster, filename);
}

bool vfs_create_directory(uint32_t cluster, const char *dirname)
{
    if (!current_mount || !current_mount->driver->create_directory) {
        return false;
    }
    return current_mount->driver->create_directory(current_mount, cluster,
                                                   dirname);
}

bool vfs_delete_directory(uint32_t cluster, const char *dirname)
{
    if (!current_mount || !current_mount->driver->delete_directory) {
        return false;
    }
    return current_mount->driver->delete_directory(current_mount, cluster,
                                                   dirname);
}

bool vfs_resolve_path(const char *path, uint32_t *parent_cluster,
                      char **filename)
{
    if (!current_mount || !current_mount->driver->resolve_path) {
        return false;
    }
    return current_mount->driver->resolve_path(current_mount, path,
                                               parent_cluster, filename);
}
