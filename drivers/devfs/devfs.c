#include <debug.h>
#include <devfs.h>
#include <fs.h>
#include <heap.h>
#include <string.h>

static devfs_device_t devices[DEVFS_MAX_DEVICES];
static int device_count = 0;

void devfs_init(void)
{
    device_count = 0;
    fb_dev_init();
    input_dev_init();
    log_info("devfs: registered %d devices", device_count);
}

int devfs_register(const devfs_device_t *dev)
{
    if (device_count >= DEVFS_MAX_DEVICES)
        return -1;
    devices[device_count] = *dev;
    return device_count++;
}

int devfs_lookup(const char *name)
{
    for (int i = 0; i < device_count; i++) {
        if (strcmp(devices[i].name, name) == 0)
            return i;
    }
    return -1;
}

const devfs_device_t *devfs_get(int dev_id)
{
    if (dev_id < 0 || dev_id >= device_count)
        return NULL;
    return &devices[dev_id];
}

int devfs_count(void)
{
    return device_count;
}

const char *devfs_name(int dev_id)
{
    if (dev_id < 0 || dev_id >= device_count)
        return NULL;
    return devices[dev_id].name;
}

static bool devfs_drv_mount(int disk_id, uint32_t lba, uint32_t sectors,
                            vfs_mount_t *m)
{
    (void)disk_id;
    (void)lba;
    (void)sectors;
    (void)m;
    return true;
}

static bool devfs_drv_unmount(vfs_mount_t *m)
{
    (void)m;
    return true;
}

static bool devfs_drv_resolve(vfs_mount_t *m, const char *path,
                              uint32_t *cluster, char **filename)
{
    (void)m;
    *cluster = 0;

    if (path[0] == '\0' || (path[0] == '/' && path[1] == '\0')) {
        *filename = NULL;
        return true;
    }

    const char *name = (path[0] == '/') ? path + 1 : path;
    if (devfs_lookup(name) >= 0) {
        *filename = strdup(name);
        return true;
    }
    return false;
}

static char **devfs_drv_list(vfs_mount_t *m, uint32_t cluster, int *count)
{
    (void)m;
    (void)cluster;
    int n = device_count;
    char **entries = malloc((n + 1) * sizeof(char *));
    for (int i = 0; i < n; i++)
        entries[i] = strdup(devices[i].name);
    *count = n;
    return entries;
}

static uint8_t *devfs_drv_read(vfs_mount_t *m, uint32_t cluster,
                               const char *filename, uint32_t *size)
{
    (void)m;
    (void)cluster;
    int dev_id = devfs_lookup(filename);
    if (dev_id < 0)
        return NULL;
    const devfs_device_t *dev = &devices[dev_id];
    void *state = NULL;
    if (dev->open(&state) < 0)
        return NULL;

    uint32_t buf_size = 4096;
    uint8_t *buf = malloc(buf_size);
    int64_t n = dev->read(state, buf, buf_size, 0, true);
    if (n > 0 && (uint32_t)n == buf_size) {
        free(buf);
        buf_size = 8 * 1024 * 1024;
        buf = malloc(buf_size);
        n = dev->read(state, buf, buf_size, 0, true);
    }
    dev->close(state);

    if (n <= 0) {
        free(buf);
        return NULL;
    }
    *size = (uint32_t)n;
    return buf;
}

fs_driver_t devfs_driver = {
    .name = "devfs",
    .mount = devfs_drv_mount,
    .unmount = devfs_drv_unmount,
    .list_directory = devfs_drv_list,
    .read_file = devfs_drv_read,
    .resolve_path = devfs_drv_resolve,
};
