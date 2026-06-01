#include <fs.h>
#include <heap.h>
#include <process.h>
#include <stdio.h>
#include <string.h>

static uint8_t *procfs_generate_status(int32_t pid, uint32_t *out_size)
{
    proc_entry_t *p = proc_get(pid);
    if (!p)
        return NULL;

    static const char state_chars[] = {
        [PROC_FREE] = 'X', [PROC_RUNNING] = 'R',
        [PROC_STOPPED] = 'T', [PROC_EXITED] = 'Z'
    };

    char buf[512];
    int n = snprintf(buf, sizeof(buf),
        "Pid:\t%d\n"
        "PPid:\t%d\n"
        "State:\t%c\n",
        p->pid, p->parent_pid,
        state_chars[p->state]);

    *out_size = n;
    uint8_t *data = malloc(n + 1);
    memcpy(data, buf, n + 1);
    return data;
}

static bool procfs_mount(int disk_id, uint32_t lba, uint32_t sectors,
                         vfs_mount_t *m)
{
    (void)disk_id;
    (void)lba;
    (void)sectors;
    (void)m;
    return true;
}

static bool procfs_unmount(vfs_mount_t *m)
{
    (void)m;
    return true;
}

static bool procfs_resolve(vfs_mount_t *m, const char *path,
                           uint32_t *cluster, char **filename)
{
    (void)m;

    if (path[0] == '\0' || (path[0] == '/' && path[1] == '\0')) {
        *cluster = 0;
        *filename = NULL;
        return true;
    }

    const char *p = (path[0] == '/') ? path + 1 : path;
    int32_t pid = 0;
    while (*p >= '0' && *p <= '9')
        pid = pid * 10 + (*p++ - '0');

    if (pid <= 0)
        return false;

    if (*p == '\0') {
        *cluster = pid;
        *filename = NULL;
        return true;
    }

    if (*p != '/')
        return false;
    p++;

    if (*p == '\0') {
        *cluster = pid;
        *filename = NULL;
        return true;
    }

    *cluster = pid;
    *filename = strdup(p);
    return true;
}

static char **procfs_list(vfs_mount_t *m, uint32_t cluster, int *count)
{
    (void)m;

    if (cluster == 0) {
        int n = 0;
        char **entries = malloc(64 * sizeof(char *));
        for (int i = 1; i < 256; i++) {
            proc_entry_t *p = proc_get(i);
            if (p && p->state != PROC_FREE) {
                char name[16];
                snprintf(name, sizeof(name), "%d/", i);
                entries[n++] = strdup(name);
            }
        }
        *count = n;
        return entries;
    }

    char **entries = malloc(2 * sizeof(char *));
    entries[0] = strdup("status");
    *count = 1;
    return entries;
}

static uint8_t *procfs_read(vfs_mount_t *m, uint32_t cluster,
                            const char *filename, uint32_t *size)
{
    (void)m;
    if (strcmp(filename, "status") == 0)
        return procfs_generate_status(cluster, size);
    return NULL;
}

fs_driver_t procfs_driver = {
    .name = "procfs",
    .mount = procfs_mount,
    .unmount = procfs_unmount,
    .list_directory = procfs_list,
    .read_file = procfs_read,
    .resolve_path = procfs_resolve,
};
