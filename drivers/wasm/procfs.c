#include <heap.h>
#include <process.h>
#include <procfs.h>
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

int procfs_open(wasm_process_t *proc, const char *path)
{
    int32_t pid = 0;
    const char *p = path + 6;
    while (*p >= '0' && *p <= '9')
        pid = pid * 10 + (*p++ - '0');
    if (*p != '/' || pid <= 0)
        return -1;
    p++;

    uint32_t size = 0;
    uint8_t *data = NULL;
    if (strcmp(p, "status") == 0)
        data = procfs_generate_status(pid, &size);

    if (!data)
        return -1;

    int fd = -1;
    for (int i = 3; i < WASM_MAX_FDS; i++) {
        if (proc->fds[i].type == FD_NONE) {
            fd = i;
            break;
        }
    }
    if (fd < 0) {
        free(data);
        return -1;
    }

    wasm_fd_t *f = &proc->fds[fd];
    f->type = FD_FILE;
    f->file.data = data;
    f->file.size = size;
    f->file.pos = 0;
    f->file.writable = false;
    f->file.dirty = false;
    return fd;
}
