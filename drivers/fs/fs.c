#include <debug.h>
#include <devfs.h>
#include <disk.h>
#include <ext2.h>
#include <fat32.h>
#include <fs.h>
#include <heap.h>
#include <procfs.h>
#include <stdio.h>
#include <string.h>

#define MAX_DRIVERS 10
#define MAX_MOUNTS 8

static fs_driver_t *drivers[MAX_DRIVERS];
static int driver_count = 0;

static vfs_mount_t mounts[MAX_MOUNTS];
static int mount_count = 0;

void fs_register_driver(fs_driver_t *driver)
{
    if (driver_count < MAX_DRIVERS) {
        drivers[driver_count++] = driver;
        log_info("fs: Registered driver: %s", driver->name);
    }
}

static vfs_mount_t *vfs_find_mount(const char *path, const char **remainder)
{
    vfs_mount_t *best = NULL;
    int best_len = -1;

    for (int i = 0; i < mount_count; i++) {
        const char *mp = mounts[i].mount_path;
        int len = strlen(mp);

        if (len == 1 && mp[0] == '/') {
            if (best_len < 1) {
                best = &mounts[i];
                best_len = 1;
            }
            continue;
        }

        if (strncmp(path, mp, len) == 0 &&
            (path[len] == '/' || path[len] == '\0')) {
            if (len > best_len) {
                best = &mounts[i];
                best_len = len;
            }
        }
    }

    if (best && remainder) {
        int mp_len = strlen(best->mount_path);
        if (mp_len == 1 && best->mount_path[0] == '/') {
            *remainder = (path[0] == '/') ? path + 1 : path;
        } else {
            const char *r = path + mp_len;
            if (*r == '/')
                r++;
            *remainder = r;
        }
    }
    return best;
}

bool vfs_mount_at(const char *path, int disk_id, uint32_t lba_start,
                  uint32_t num_sectors)
{
    if (mount_count >= MAX_MOUNTS)
        return false;

    for (int i = 0; i < driver_count; i++) {
        vfs_mount_t *m = &mounts[mount_count];
        memset(m, 0, sizeof(*m));
        m->driver = drivers[i];
        m->disk_id = disk_id;
        m->lba_start = lba_start;
        m->num_sectors = num_sectors;
        strncpy(m->mount_path, path, sizeof(m->mount_path) - 1);

        if (drivers[i]->mount(disk_id, lba_start, num_sectors, m)) {
            mount_count++;
            log_info("vfs: mounted %s at %s (disk %d, LBA %u)",
                     drivers[i]->name, path, disk_id, lba_start);
            return true;
        }
    }
    return false;
}

bool vfs_mount_virtual(const char *path, fs_driver_t *driver)
{
    if (mount_count >= MAX_MOUNTS)
        return false;

    vfs_mount_t *m = &mounts[mount_count];
    memset(m, 0, sizeof(*m));
    m->driver = driver;
    m->disk_id = -1;
    strncpy(m->mount_path, path, sizeof(m->mount_path) - 1);

    if (driver->mount && !driver->mount(-1, 0, 0, m))
        return false;

    mount_count++;
    log_info("vfs: mounted %s at %s", driver->name, path);
    return true;
}

bool vfs_unmount(const char *path)
{
    for (int i = 0; i < mount_count; i++) {
        if (strcmp(mounts[i].mount_path, path) == 0) {
            if (mounts[i].driver->unmount)
                mounts[i].driver->unmount(&mounts[i]);
            mounts[i] = mounts[--mount_count];
            log_info("vfs: unmounted %s", path);
            return true;
        }
    }
    return false;
}

static void inject_mount_children(const char *dir_path, char ***entries,
                                  int *count)
{
    int dir_len = strlen(dir_path);
    bool is_root = (dir_len == 1 && dir_path[0] == '/');

    for (int i = 0; i < mount_count; i++) {
        const char *mp = mounts[i].mount_path;
        if (is_root) {
            if (mp[0] == '/' && strchr(mp + 1, '/') == NULL && mp[1] != '\0') {
                int n = *count;
                *entries = realloc(*entries, (n + 2) * sizeof(char *));
                char name[64];
                snprintf(name, sizeof(name), "%s/", mp + 1);
                (*entries)[n] = strdup(name);
                *count = n + 1;
            }
        } else {
            if (strncmp(mp, dir_path, dir_len) == 0 && mp[dir_len] == '/') {
                const char *child = mp + dir_len + 1;
                if (strchr(child, '/') == NULL) {
                    int n = *count;
                    *entries = realloc(*entries, (n + 2) * sizeof(char *));
                    char name[64];
                    snprintf(name, sizeof(name), "%s/", child);
                    (*entries)[n] = strdup(name);
                    *count = n + 1;
                }
            }
        }
    }
}

char **vfs_list(const char *path, int *count)
{
    *count = 0;
    const char *remainder;
    vfs_mount_t *m = vfs_find_mount(path, &remainder);

    char **entries = NULL;
    if (m && m->driver->list_directory) {
        uint32_t cluster;
        char *filename = NULL;

        if (remainder[0] == '\0') {
            cluster = m->root_cluster;
        } else {
            if (!m->driver->resolve_path ||
                !m->driver->resolve_path(m, remainder, &cluster, &filename))
                return NULL;
            if (filename) {
                free(filename);
                return NULL;
            }
        }
        entries = m->driver->list_directory(m, cluster, count);
    }

    if (!entries) {
        entries = malloc(sizeof(char *));
        *count = 0;
    }
    inject_mount_children(path, &entries, count);
    return *count > 0 ? entries : (free(entries), NULL);
}

uint8_t *vfs_read(const char *path, uint32_t *size)
{
    const char *remainder;
    vfs_mount_t *m = vfs_find_mount(path, &remainder);
    if (!m || !m->driver->read_file)
        return NULL;

    uint32_t cluster;
    char *filename = NULL;
    if (!m->driver->resolve_path ||
        !m->driver->resolve_path(m, remainder, &cluster, &filename))
        return NULL;

    uint8_t *data = m->driver->read_file(m, cluster, filename, size);
    free(filename);
    return data;
}

uint8_t *vfs_read_ex(const char *path, uint32_t *size,
                     vfs_progress_fn progress, void *ctx)
{
    const char *remainder;
    vfs_mount_t *m = vfs_find_mount(path, &remainder);
    if (!m)
        return NULL;

    uint32_t cluster;
    char *filename = NULL;
    if (!m->driver->resolve_path ||
        !m->driver->resolve_path(m, remainder, &cluster, &filename))
        return NULL;

    uint8_t *data;
    if (m->driver->read_file_ex)
        data = m->driver->read_file_ex(m, cluster, filename, size, progress, ctx);
    else if (m->driver->read_file)
        data = m->driver->read_file(m, cluster, filename, size);
    else
        data = NULL;

    free(filename);
    return data;
}

bool vfs_write(const char *path, const uint8_t *data, uint32_t size)
{
    const char *remainder;
    vfs_mount_t *m = vfs_find_mount(path, &remainder);
    if (!m || !m->driver->write_file)
        return false;

    uint32_t cluster;
    char *filename = NULL;
    if (!m->driver->resolve_path ||
        !m->driver->resolve_path(m, remainder, &cluster, &filename))
        return false;

    bool ok = m->driver->write_file(m, cluster, filename, data, size);
    free(filename);
    return ok;
}

bool vfs_delete(const char *path)
{
    const char *remainder;
    vfs_mount_t *m = vfs_find_mount(path, &remainder);
    if (!m || !m->driver->delete_file)
        return false;

    uint32_t cluster;
    char *filename = NULL;
    if (!m->driver->resolve_path ||
        !m->driver->resolve_path(m, remainder, &cluster, &filename))
        return false;

    bool ok = m->driver->delete_file(m, cluster, filename);
    free(filename);
    return ok;
}

bool vfs_mkdir(const char *path)
{
    const char *remainder;
    vfs_mount_t *m = vfs_find_mount(path, &remainder);
    if (!m || !m->driver->create_directory)
        return false;

    uint32_t cluster;
    char *filename = NULL;
    if (!m->driver->resolve_path) {
        const char *slash = strrchr(remainder, '/');
        if (slash) {
            char parent[256];
            int plen = slash - remainder;
            memcpy(parent, remainder, plen);
            parent[plen] = '\0';
            if (!m->driver->resolve_path(m, parent, &cluster, &filename))
                return false;
            free(filename);
            return m->driver->create_directory(m, cluster, slash + 1);
        }
        return m->driver->create_directory(m, m->root_cluster, remainder);
    }

    const char *slash = strrchr(remainder, '/');
    if (slash) {
        char parent[256];
        int plen = slash - remainder;
        memcpy(parent, remainder, plen);
        parent[plen] = '\0';
        if (!m->driver->resolve_path(m, parent, &cluster, &filename))
            return false;
        free(filename);
        return m->driver->create_directory(m, cluster, slash + 1);
    }
    return m->driver->create_directory(m, m->root_cluster, remainder);
}

bool vfs_rmdir(const char *path)
{
    const char *remainder;
    vfs_mount_t *m = vfs_find_mount(path, &remainder);
    if (!m || !m->driver->delete_directory)
        return false;

    uint32_t cluster;
    char *filename = NULL;
    if (!m->driver->resolve_path ||
        !m->driver->resolve_path(m, remainder, &cluster, &filename))
        return false;

    bool ok = m->driver->delete_directory(m, cluster, filename);
    free(filename);
    return ok;
}

bool vfs_exists(const char *path)
{
    const char *remainder;
    vfs_mount_t *m = vfs_find_mount(path, &remainder);
    if (!m)
        return false;

    if (remainder[0] == '\0')
        return true;

    if (!m->driver->resolve_path)
        return false;

    uint32_t cluster;
    char *filename = NULL;
    bool found = m->driver->resolve_path(m, remainder, &cluster, &filename);
    free(filename);
    return found;
}

bool vfs_is_dir(const char *path)
{
    const char *remainder;
    vfs_mount_t *m = vfs_find_mount(path, &remainder);
    if (!m)
        return false;

    if (remainder[0] == '\0')
        return true;

    for (int i = 0; i < mount_count; i++) {
        if (strcmp(mounts[i].mount_path, path) == 0)
            return true;
    }

    if (!m->driver->resolve_path || !m->driver->list_directory)
        return false;

    uint32_t cluster;
    char *filename = NULL;
    if (!m->driver->resolve_path(m, remainder, &cluster, &filename))
        return false;

    if (!filename) {
        return true;
    }

    int cnt = 0;
    char **entries = m->driver->list_directory(m, cluster, &cnt);
    if (!entries) {
        free(filename);
        return false;
    }
    bool is_dir = false;
    for (int i = 0; i < cnt; i++) {
        int len = strlen(entries[i]);
        if (len > 0 && entries[i][len - 1] == '/') {
            char name[256];
            strncpy(name, entries[i], len - 1);
            name[len - 1] = '\0';
            if (strcmp(name, filename) == 0)
                is_dir = true;
        }
        free(entries[i]);
    }
    free(entries);
    free(filename);
    return is_dir;
}

vfs_node_type_t vfs_type(const char *path)
{
    if (!vfs_exists(path))
        return VFS_INVALID;
    if (vfs_is_dir(path))
        return VFS_DIRECTORY;
    return VFS_FILE;
}

void fs_init(void)
{
    disk_init();
    ext2_init();
    devfs_init();
    vfs_mount_virtual("/dev", (fs_driver_t *)&devfs_driver);
    vfs_mount_virtual("/proc", (fs_driver_t *)&procfs_driver);
}
