#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <fs.h>
#include <heap.h>
#include <keyboard.h>
#include <pit.h>
#include <scheduler.h>
#include <wasm_api.h>

#include "wasm3.h"
#include "m3_env.h"

#define WASM_PROC(ctx) ((wasm_process_t *)((ctx)->userdata))

wasm_process_t *wasm_process_create(int argc, char **argv)
{
    wasm_process_t *proc = malloc(sizeof(wasm_process_t));
    memset(proc, 0, sizeof(wasm_process_t));

    proc->argc = argc < WASM_MAX_ARGC ? argc : WASM_MAX_ARGC;
    for (int i = 0; i < proc->argc; i++) {
        strncpy(proc->argv[i], argv[i], WASM_MAX_ARG_LEN - 1);
        proc->argv[i][WASM_MAX_ARG_LEN - 1] = '\0';
    }

    proc->fds[0].in_use = true;
    proc->fds[1].in_use = true;
    proc->fds[2].in_use = true;

    return proc;
}

void wasm_process_destroy(wasm_process_t *proc)
{
    for (int i = 3; i < WASM_MAX_FDS; i++) {
        if (!proc->fds[i].in_use)
            continue;
        if (proc->fds[i].dirty && proc->fds[i].data)
            vfs_write_file(proc->fds[i].parent_cluster, proc->fds[i].filename,
                           proc->fds[i].data, proc->fds[i].size);
        if (proc->fds[i].data)
            free(proc->fds[i].data);
    }
    free(proc);
}

static int wasm_fd_alloc(wasm_process_t *proc)
{
    for (int i = 3; i < WASM_MAX_FDS; i++)
        if (!proc->fds[i].in_use)
            return i;
    return -1;
}

/* --- IO APIs --- */

m3ApiRawFunction(wasm_api_print)
{
    m3ApiGetArgMem(const char *, ptr)
    m3ApiGetArg(uint32_t, len)
    m3ApiCheckMem(ptr, len);
    for (uint32_t i = 0; i < len; i++)
        putchar(ptr[i]);
    m3ApiSuccess();
}

m3ApiRawFunction(wasm_api_putchar)
{
    m3ApiGetArg(int32_t, c)
    putchar(c);
    m3ApiSuccess();
}

m3ApiRawFunction(wasm_api_get_ticks)
{
    m3ApiReturnType(uint64_t)
    m3ApiReturn(pit_ticks);
}

m3ApiRawFunction(wasm_api_exit)
{
    m3ApiGetArg(int32_t, code)
    wasm_process_t *proc = WASM_PROC(_ctx);
    proc->exit_code = code;
    m3ApiTrap(m3Err_trapExit);
}

m3ApiRawFunction(wasm_api_get_argc)
{
    m3ApiReturnType(int32_t)
    wasm_process_t *proc = WASM_PROC(_ctx);
    m3ApiReturn(proc->argc);
}

m3ApiRawFunction(wasm_api_get_argv)
{
    m3ApiReturnType(int32_t)
    m3ApiGetArg(int32_t, index)
    m3ApiGetArgMem(char *, buf)
    m3ApiGetArg(int32_t, buf_len)
    m3ApiCheckMem(buf, buf_len);
    wasm_process_t *proc = WASM_PROC(_ctx);
    if (index < 0 || index >= proc->argc)
        m3ApiReturn(-1);
    int len = strlen(proc->argv[index]);
    if (len >= buf_len)
        len = buf_len - 1;
    memcpy(buf, proc->argv[index], len);
    buf[len] = '\0';
    m3ApiReturn(len);
}

m3ApiRawFunction(wasm_api_getchar)
{
    m3ApiReturnType(int32_t)
    char c;
    while (!kbd_buffer_pop(&c))
        scheduler_yield();
    m3ApiReturn((int32_t)c);
}

m3ApiRawFunction(wasm_api_read_line)
{
    m3ApiReturnType(int32_t)
    m3ApiGetArgMem(char *, buf)
    m3ApiGetArg(int32_t, max_len)
    m3ApiCheckMem(buf, max_len);
    int32_t i = 0;
    while (i < max_len - 1) {
        char c;
        while (!kbd_buffer_pop(&c))
            scheduler_yield();
        if (c == '\n' || c == '\r') {
            putchar('\n');
            break;
        }
        if (c == '\b') {
            if (i > 0) {
                i--;
                putchar('\b');
            }
            continue;
        }
        putchar(c);
        buf[i++] = c;
    }
    buf[i] = '\0';
    m3ApiReturn(i);
}

/* --- Filesystem APIs --- */

m3ApiRawFunction(wasm_api_open)
{
    m3ApiReturnType(int32_t)
    m3ApiGetArgMem(const char *, path)
    m3ApiGetArg(int32_t, path_len)
    m3ApiGetArg(int32_t, flags)
    m3ApiCheckMem(path, path_len);

    wasm_process_t *proc = WASM_PROC(_ctx);
    int fd = wasm_fd_alloc(proc);
    if (fd < 0)
        m3ApiReturn(-1);

    char pathbuf[256];
    int copy_len = path_len < 255 ? path_len : 255;
    memcpy(pathbuf, path, copy_len);
    pathbuf[copy_len] = '\0';

    uint32_t parent_cluster;
    char *filename = NULL;
    if (!vfs_resolve_path(pathbuf, &parent_cluster, &filename))
        m3ApiReturn(-1);

    wasm_fd_t *f = &proc->fds[fd];
    uint32_t size = 0;
    uint8_t *data = vfs_read_file(parent_cluster, filename, &size);

    if (!data && (flags & WASM_O_CREAT)) {
        data = malloc(1);
        size = 0;
    } else if (!data) {
        free(filename);
        m3ApiReturn(-1);
    }

    f->in_use = true;
    f->data = data;
    f->size = size;
    f->pos = (flags & WASM_O_APPEND) ? size : 0;
    f->writable = (flags & (WASM_O_WRONLY | WASM_O_RDWR)) != 0;
    f->dirty = false;
    f->flags = flags;
    f->parent_cluster = parent_cluster;
    strncpy(f->filename, filename, 11);
    f->filename[11] = '\0';
    free(filename);

    if (flags & WASM_O_TRUNC) {
        f->size = 0;
        f->pos = 0;
        f->dirty = true;
    }

    m3ApiReturn(fd);
}

m3ApiRawFunction(wasm_api_close)
{
    m3ApiReturnType(int32_t)
    m3ApiGetArg(int32_t, fd)

    wasm_process_t *proc = WASM_PROC(_ctx);
    if (fd < 3 || fd >= WASM_MAX_FDS || !proc->fds[fd].in_use)
        m3ApiReturn(-1);

    wasm_fd_t *f = &proc->fds[fd];
    if (f->dirty && f->data)
        vfs_write_file(f->parent_cluster, f->filename, f->data, f->size);
    if (f->data)
        free(f->data);
    memset(f, 0, sizeof(wasm_fd_t));
    m3ApiReturn(0);
}

m3ApiRawFunction(wasm_api_read)
{
    m3ApiReturnType(int32_t)
    m3ApiGetArg(int32_t, fd)
    m3ApiGetArgMem(uint8_t *, buf)
    m3ApiGetArg(int32_t, count)
    m3ApiCheckMem(buf, count);

    wasm_process_t *proc = WASM_PROC(_ctx);

    if (fd == 0) {
        int32_t i = 0;
        while (i < count) {
            char c;
            while (!kbd_buffer_pop(&c))
                scheduler_yield();
            buf[i++] = (uint8_t)c;
            if (c == '\n')
                break;
        }
        m3ApiReturn(i);
    }

    if (fd < 0 || fd >= WASM_MAX_FDS || !proc->fds[fd].in_use)
        m3ApiReturn(-1);

    wasm_fd_t *f = &proc->fds[fd];
    int32_t avail = f->size - f->pos;
    if (avail <= 0)
        m3ApiReturn(0);
    if (count > avail)
        count = avail;
    memcpy(buf, f->data + f->pos, count);
    f->pos += count;
    m3ApiReturn(count);
}

m3ApiRawFunction(wasm_api_write)
{
    m3ApiReturnType(int32_t)
    m3ApiGetArg(int32_t, fd)
    m3ApiGetArgMem(const uint8_t *, buf)
    m3ApiGetArg(int32_t, count)
    m3ApiCheckMem(buf, count);

    wasm_process_t *proc = WASM_PROC(_ctx);

    if (fd == 1 || fd == 2) {
        for (int32_t i = 0; i < count; i++)
            putchar(buf[i]);
        m3ApiReturn(count);
    }

    if (fd < 3 || fd >= WASM_MAX_FDS || !proc->fds[fd].in_use)
        m3ApiReturn(-1);

    wasm_fd_t *f = &proc->fds[fd];
    if (!f->writable)
        m3ApiReturn(-1);

    uint32_t end = f->pos + count;
    if (end > f->size) {
        uint8_t *new_data = realloc(f->data, end);
        if (!new_data)
            m3ApiReturn(-1);
        memset(new_data + f->size, 0, end - f->size);
        f->data = new_data;
        f->size = end;
    }
    memcpy(f->data + f->pos, buf, count);
    f->pos += count;
    f->dirty = true;
    m3ApiReturn(count);
}

m3ApiRawFunction(wasm_api_seek)
{
    m3ApiReturnType(int32_t)
    m3ApiGetArg(int32_t, fd)
    m3ApiGetArg(int32_t, offset)
    m3ApiGetArg(int32_t, whence)

    wasm_process_t *proc = WASM_PROC(_ctx);
    if (fd < 3 || fd >= WASM_MAX_FDS || !proc->fds[fd].in_use)
        m3ApiReturn(-1);

    wasm_fd_t *f = &proc->fds[fd];
    int32_t new_pos;
    switch (whence) {
    case WASM_SEEK_SET:
        new_pos = offset;
        break;
    case WASM_SEEK_CUR:
        new_pos = (int32_t)f->pos + offset;
        break;
    case WASM_SEEK_END:
        new_pos = (int32_t)f->size + offset;
        break;
    default:
        m3ApiReturn(-1);
    }
    if (new_pos < 0)
        new_pos = 0;
    f->pos = (uint32_t)new_pos;
    m3ApiReturn(new_pos);
}

m3ApiRawFunction(wasm_api_stat)
{
    m3ApiReturnType(int32_t)
    m3ApiGetArgMem(const char *, path)
    m3ApiGetArg(int32_t, path_len)
    m3ApiGetArgMem(uint32_t *, stat_buf)
    m3ApiCheckMem(path, path_len);
    m3ApiCheckMem(stat_buf, 8);

    char pathbuf[256];
    int copy_len = path_len < 255 ? path_len : 255;
    memcpy(pathbuf, path, copy_len);
    pathbuf[copy_len] = '\0';

    uint32_t parent_cluster;
    char *filename = NULL;
    if (!vfs_resolve_path(pathbuf, &parent_cluster, &filename))
        m3ApiReturn(-1);

    int count = 0;
    char **entries = vfs_list_directory(parent_cluster, &count);
    if (entries) {
        for (int i = 0; i < count; i++)
            free(entries[i]);
        free(entries);
        stat_buf[0] = 0;
        stat_buf[1] = VFS_DIRECTORY;
        free(filename);
        m3ApiReturn(0);
    }

    uint32_t size = 0;
    uint8_t *data = vfs_read_file(parent_cluster, filename, &size);
    free(filename);
    if (!data)
        m3ApiReturn(-1);
    free(data);

    stat_buf[0] = size;
    stat_buf[1] = VFS_FILE;
    m3ApiReturn(0);
}

m3ApiRawFunction(wasm_api_readdir)
{
    m3ApiReturnType(int32_t)
    m3ApiGetArgMem(const char *, path)
    m3ApiGetArg(int32_t, path_len)
    m3ApiGetArgMem(char *, buf)
    m3ApiGetArg(int32_t, buf_len)
    m3ApiCheckMem(path, path_len);
    m3ApiCheckMem(buf, buf_len);

    char pathbuf[256];
    int copy_len = path_len < 255 ? path_len : 255;
    memcpy(pathbuf, path, copy_len);
    pathbuf[copy_len] = '\0';

    vfs_mount_t *mount = vfs_get_mounted_fs();
    if (!mount)
        m3ApiReturn(-1);

    uint32_t cluster = mount->root_cluster;
    if (strcmp(pathbuf, "/") != 0) {
        uint32_t parent_cluster;
        char *filename = NULL;
        if (!vfs_resolve_path(pathbuf, &parent_cluster, &filename)) {
            if (filename) free(filename);
            m3ApiReturn(-1);
        }
        if (filename) free(filename);
    }

    int count = 0;
    char **entries = vfs_list_directory(cluster, &count);
    if (!entries)
        m3ApiReturn(-1);

    int pos = 0;
    int written = 0;
    for (int i = 0; i < count; i++) {
        int len = strlen(entries[i]);
        if (pos + len + 1 <= buf_len) {
            memcpy(buf + pos, entries[i], len);
            buf[pos + len] = '\0';
            pos += len + 1;
            written++;
        }
        free(entries[i]);
    }
    free(entries);
    m3ApiReturn(written);
}

m3ApiRawFunction(wasm_api_mkdir)
{
    m3ApiReturnType(int32_t)
    m3ApiGetArgMem(const char *, path)
    m3ApiGetArg(int32_t, path_len)
    m3ApiCheckMem(path, path_len);

    char pathbuf[256];
    int copy_len = path_len < 255 ? path_len : 255;
    memcpy(pathbuf, path, copy_len);
    pathbuf[copy_len] = '\0';

    uint32_t parent_cluster;
    char *dirname = NULL;
    if (!vfs_resolve_path(pathbuf, &parent_cluster, &dirname)) {
        if (dirname) free(dirname);
        m3ApiReturn(-1);
    }

    int32_t result = vfs_create_directory(parent_cluster, dirname) ? 0 : -1;
    free(dirname);
    m3ApiReturn(result);
}

m3ApiRawFunction(wasm_api_unlink)
{
    m3ApiReturnType(int32_t)
    m3ApiGetArgMem(const char *, path)
    m3ApiGetArg(int32_t, path_len)
    m3ApiCheckMem(path, path_len);

    char pathbuf[256];
    int copy_len = path_len < 255 ? path_len : 255;
    memcpy(pathbuf, path, copy_len);
    pathbuf[copy_len] = '\0';

    uint32_t parent_cluster;
    char *filename = NULL;
    if (!vfs_resolve_path(pathbuf, &parent_cluster, &filename)) {
        if (filename) free(filename);
        m3ApiReturn(-1);
    }

    int32_t result = vfs_delete_file(parent_cluster, filename) ? 0 : -1;
    free(filename);
    m3ApiReturn(result);
}

m3ApiRawFunction(wasm_api_rmdir)
{
    m3ApiReturnType(int32_t)
    m3ApiGetArgMem(const char *, path)
    m3ApiGetArg(int32_t, path_len)
    m3ApiCheckMem(path, path_len);

    char pathbuf[256];
    int copy_len = path_len < 255 ? path_len : 255;
    memcpy(pathbuf, path, copy_len);
    pathbuf[copy_len] = '\0';

    uint32_t parent_cluster;
    char *dirname = NULL;
    if (!vfs_resolve_path(pathbuf, &parent_cluster, &dirname)) {
        if (dirname) free(dirname);
        m3ApiReturn(-1);
    }

    int32_t result = vfs_delete_directory(parent_cluster, dirname) ? 0 : -1;
    free(dirname);
    m3ApiReturn(result);
}

/* --- Link all APIs --- */

void wasm_link_api(IM3Module module, wasm_process_t *proc)
{
    m3_LinkRawFunctionEx(module, "env", "print", "v(*i)", &wasm_api_print, proc);
    m3_LinkRawFunctionEx(module, "env", "putchar", "v(i)", &wasm_api_putchar, proc);
    m3_LinkRawFunctionEx(module, "env", "get_ticks", "I()", &wasm_api_get_ticks, proc);
    m3_LinkRawFunctionEx(module, "env", "exit", "v(i)", &wasm_api_exit, proc);
    m3_LinkRawFunctionEx(module, "env", "get_argc", "i()", &wasm_api_get_argc, proc);
    m3_LinkRawFunctionEx(module, "env", "get_argv", "i(i*i)", &wasm_api_get_argv, proc);
    m3_LinkRawFunctionEx(module, "env", "getchar", "i()", &wasm_api_getchar, proc);
    m3_LinkRawFunctionEx(module, "env", "read_line", "i(*i)", &wasm_api_read_line, proc);
    m3_LinkRawFunctionEx(module, "env", "open", "i(*ii)", &wasm_api_open, proc);
    m3_LinkRawFunctionEx(module, "env", "close", "i(i)", &wasm_api_close, proc);
    m3_LinkRawFunctionEx(module, "env", "read", "i(i*i)", &wasm_api_read, proc);
    m3_LinkRawFunctionEx(module, "env", "write", "i(i*i)", &wasm_api_write, proc);
    m3_LinkRawFunctionEx(module, "env", "seek", "i(iii)", &wasm_api_seek, proc);
    m3_LinkRawFunctionEx(module, "env", "stat", "i(*i*)", &wasm_api_stat, proc);
    m3_LinkRawFunctionEx(module, "env", "readdir", "i(*i*i)", &wasm_api_readdir, proc);
    m3_LinkRawFunctionEx(module, "env", "mkdir", "i(*i)", &wasm_api_mkdir, proc);
    m3_LinkRawFunctionEx(module, "env", "unlink", "i(*i)", &wasm_api_unlink, proc);
    m3_LinkRawFunctionEx(module, "env", "rmdir", "i(*i)", &wasm_api_rmdir, proc);
}
