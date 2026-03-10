#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <fs.h>
#include <heap.h>
#include <keyboard.h>
#include <pit.h>
#include <process.h>
#include <scheduler.h>
#include <pipe.h>
#include <waitqueue.h>
#include <wasm_api.h>
#include <wasm_runner.h>

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

    proc->fds[0].type = FD_CONSOLE;
    proc->fds[1].type = FD_CONSOLE;
    proc->fds[2].type = FD_CONSOLE;

    return proc;
}

void wasm_process_destroy(wasm_process_t *proc)
{
    for (int i = 0; i < WASM_MAX_FDS; i++) {
        wasm_fd_t *f = &proc->fds[i];
        switch (f->type) {
        case FD_FILE:
            if (f->file.dirty && f->file.data)
                vfs_write_file(f->file.parent_cluster, f->file.filename,
                               f->file.data, f->file.size);
            if (f->file.data)
                free(f->file.data);
            break;
        case FD_PIPE_READ:
            pipe_unref_read(f->pipe.pipe_id);
            break;
        case FD_PIPE_WRITE:
            pipe_unref_write(f->pipe.pipe_id);
            break;
        default:
            break;
        }
        f->type = FD_NONE;
    }
    free(proc);
}

static int wasm_fd_alloc(wasm_process_t *proc)
{
    for (int i = 3; i < WASM_MAX_FDS; i++)
        if (proc->fds[i].type == FD_NONE)
            return i;
    return -1;
}

/* --- IO APIs --- */

static void wasm_fd_putchar(wasm_process_t *proc, char c)
{
    wasm_fd_t *f = &proc->fds[1];
    switch (f->type) {
    case FD_PIPE_WRITE:
        pipe_write(f->pipe.pipe_id, (const uint8_t *)&c, 1);
        break;
    default:
        putchar(c);
        break;
    }
}

m3ApiRawFunction(wasm_api_print)
{
    m3ApiGetArgMem(const char *, ptr)
    m3ApiGetArg(uint32_t, len)
    m3ApiCheckMem(ptr, len);
    wasm_process_t *proc = WASM_PROC(_ctx);
    for (uint32_t i = 0; i < len; i++)
        wasm_fd_putchar(proc, ptr[i]);
    m3ApiSuccess();
}

m3ApiRawFunction(wasm_api_putchar)
{
    m3ApiGetArg(int32_t, c)
    wasm_process_t *proc = WASM_PROC(_ctx);
    wasm_fd_putchar(proc, c);
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
    while (!kbd_buffer_pop(&c)) {
        proc_entry_t *e = proc_get(WASM_PROC(_ctx)->pid);
        if (e && e->killed) {
            WASM_PROC(_ctx)->exit_code = 137;
            m3ApiTrap(m3Err_trapExit);
        }
        scheduler_yield();
    }
    if (c == '\x03') {
        WASM_PROC(_ctx)->exit_code = 130;
        m3ApiTrap(m3Err_trapExit);
    }
    if (c == '\x04')
        m3ApiReturn(-1);
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
        while (!kbd_buffer_pop(&c)) {
            proc_entry_t *e = proc_get(WASM_PROC(_ctx)->pid);
            if (e && e->killed) {
                WASM_PROC(_ctx)->exit_code = 137;
                m3ApiTrap(m3Err_trapExit);
            }
            scheduler_yield();
        }
        if (c == '\x03') {
            WASM_PROC(_ctx)->exit_code = 130;
            m3ApiTrap(m3Err_trapExit);
        }
        if (c == '\x04') {
            if (i > 0)
                break;
            m3ApiReturn(-1);
        }
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

    f->type = FD_FILE;
    f->file.data = data;
    f->file.size = size;
    f->file.pos = (flags & WASM_O_APPEND) ? size : 0;
    f->file.writable = (flags & (WASM_O_WRONLY | WASM_O_RDWR)) != 0;
    f->file.dirty = false;
    f->file.flags = flags;
    f->file.parent_cluster = parent_cluster;
    strncpy(f->file.filename, filename, 11);
    f->file.filename[11] = '\0';
    free(filename);

    if (flags & WASM_O_TRUNC) {
        f->file.size = 0;
        f->file.pos = 0;
        f->file.dirty = true;
    }

    m3ApiReturn(fd);
}

m3ApiRawFunction(wasm_api_close)
{
    m3ApiReturnType(int32_t)
    m3ApiGetArg(int32_t, fd)

    wasm_process_t *proc = WASM_PROC(_ctx);
    if (fd < 0 || fd >= WASM_MAX_FDS || proc->fds[fd].type == FD_NONE)
        m3ApiReturn(-1);

    wasm_fd_t *f = &proc->fds[fd];
    switch (f->type) {
    case FD_CONSOLE:
        break;
    case FD_FILE:
        if (f->file.dirty && f->file.data)
            vfs_write_file(f->file.parent_cluster, f->file.filename,
                           f->file.data, f->file.size);
        if (f->file.data)
            free(f->file.data);
        break;
    case FD_PIPE_READ:
        pipe_unref_read(f->pipe.pipe_id);
        break;
    case FD_PIPE_WRITE:
        pipe_unref_write(f->pipe.pipe_id);
        break;
    default:
        break;
    }
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
    if (fd < 0 || fd >= WASM_MAX_FDS)
        m3ApiReturn(-1);

    wasm_fd_t *f = &proc->fds[fd];
    switch (f->type) {
    case FD_CONSOLE: {
        int32_t i = 0;
        while (i < count) {
            char c;
            while (!kbd_buffer_pop(&c)) {
                proc_entry_t *e = proc_get(proc->pid);
                if (e && e->killed) {
                    proc->exit_code = 137;
                    m3ApiTrap(m3Err_trapExit);
                }
                scheduler_yield();
            }
            if (c == '\x03') {
                proc->exit_code = 130;
                m3ApiTrap(m3Err_trapExit);
            }
            if (c == '\x04')
                m3ApiReturn(i);
            buf[i++] = (uint8_t)c;
            if (c == '\n')
                break;
        }
        m3ApiReturn(i);
    }
    case FD_FILE: {
        int32_t avail = f->file.size - f->file.pos;
        if (avail <= 0)
            m3ApiReturn(0);
        if (count > avail)
            count = avail;
        memcpy(buf, f->file.data + f->file.pos, count);
        f->file.pos += count;
        m3ApiReturn(count);
    }
    case FD_PIPE_READ: {
        int32_t n = pipe_read(f->pipe.pipe_id, buf, count);
        m3ApiReturn(n);
    }
    default:
        m3ApiReturn(-1);
    }
}

m3ApiRawFunction(wasm_api_write)
{
    m3ApiReturnType(int32_t)
    m3ApiGetArg(int32_t, fd)
    m3ApiGetArgMem(const uint8_t *, buf)
    m3ApiGetArg(int32_t, count)
    m3ApiCheckMem(buf, count);

    wasm_process_t *proc = WASM_PROC(_ctx);
    if (fd < 0 || fd >= WASM_MAX_FDS)
        m3ApiReturn(-1);

    wasm_fd_t *f = &proc->fds[fd];
    switch (f->type) {
    case FD_CONSOLE:
        for (int32_t i = 0; i < count; i++)
            putchar(buf[i]);
        m3ApiReturn(count);
    case FD_FILE: {
        if (!f->file.writable)
            m3ApiReturn(-1);
        uint32_t end = f->file.pos + count;
        if (end > f->file.size) {
            uint8_t *new_data = realloc(f->file.data, end);
            if (!new_data)
                m3ApiReturn(-1);
            memset(new_data + f->file.size, 0, end - f->file.size);
            f->file.data = new_data;
            f->file.size = end;
        }
        memcpy(f->file.data + f->file.pos, buf, count);
        f->file.pos += count;
        f->file.dirty = true;
        m3ApiReturn(count);
    }
    case FD_PIPE_WRITE: {
        int32_t n = pipe_write(f->pipe.pipe_id, buf, count);
        m3ApiReturn(n);
    }
    default:
        m3ApiReturn(-1);
    }
}

m3ApiRawFunction(wasm_api_seek)
{
    m3ApiReturnType(int32_t)
    m3ApiGetArg(int32_t, fd)
    m3ApiGetArg(int32_t, offset)
    m3ApiGetArg(int32_t, whence)

    wasm_process_t *proc = WASM_PROC(_ctx);
    if (fd < 0 || fd >= WASM_MAX_FDS || proc->fds[fd].type != FD_FILE)
        m3ApiReturn(-1);

    wasm_fd_t *f = &proc->fds[fd];
    int32_t new_pos;
    switch (whence) {
    case WASM_SEEK_SET:
        new_pos = offset;
        break;
    case WASM_SEEK_CUR:
        new_pos = (int32_t)f->file.pos + offset;
        break;
    case WASM_SEEK_END:
        new_pos = (int32_t)f->file.size + offset;
        break;
    default:
        m3ApiReturn(-1);
    }
    if (new_pos < 0)
        new_pos = 0;
    f->file.pos = (uint32_t)new_pos;
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

/* --- Process APIs --- */

m3ApiRawFunction(wasm_api_spawn)
{
    m3ApiReturnType(int32_t)
    m3ApiGetArgMem(const char *, path)
    m3ApiGetArg(int32_t, path_len)
    m3ApiGetArgMem(const char *, args_buf)
    m3ApiGetArg(int32_t, args_len)
    m3ApiGetArg(int32_t, argc)
    m3ApiCheckMem(path, path_len);
    if (args_len > 0)
        m3ApiCheckMem(args_buf, args_len);

    char pathbuf[256];
    int copy_len = path_len < 255 ? path_len : 255;
    memcpy(pathbuf, path, copy_len);
    pathbuf[copy_len] = '\0';

    char *argv[WASM_MAX_ARGC];
    int actual_argc = 0;
    const char *p = args_buf;
    const char *end = args_buf + args_len;
    while (p < end && actual_argc < argc && actual_argc < WASM_MAX_ARGC) {
        argv[actual_argc++] = (char *)p;
        while (p < end && *p != '\0')
            p++;
        p++;
    }

    wasm_process_t *proc = WASM_PROC(_ctx);
    int32_t pid = wasm_spawn(pathbuf, actual_argc, argv, proc->pid);
    m3ApiReturn(pid);
}

m3ApiRawFunction(wasm_api_waitpid)
{
    m3ApiReturnType(int32_t)
    m3ApiGetArg(int32_t, pid)

    proc_entry_t *e = proc_get(pid);
    if (!e)
        m3ApiReturn(-1);
    while (e->state != PROC_EXITED)
        waitqueue_sleep(&e->exit_wq);
    int32_t code = e->exit_code;
    proc_free(pid);
    m3ApiReturn(code);
}

m3ApiRawFunction(wasm_api_kill)
{
    m3ApiReturnType(int32_t)
    m3ApiGetArg(int32_t, pid)

    proc_entry_t *e = proc_get(pid);
    if (!e)
        m3ApiReturn(-1);
    e->killed = true;
    m3ApiReturn(0);
}

m3ApiRawFunction(wasm_api_getpid)
{
    m3ApiReturnType(int32_t)
    wasm_process_t *proc = WASM_PROC(_ctx);
    m3ApiReturn(proc->pid);
}

/* --- Pipe & Redirection APIs --- */

m3ApiRawFunction(wasm_api_dup2)
{
    m3ApiReturnType(int32_t)
    m3ApiGetArg(int32_t, oldfd)
    m3ApiGetArg(int32_t, newfd)

    wasm_process_t *proc = WASM_PROC(_ctx);
    if (oldfd < 0 || oldfd >= WASM_MAX_FDS || newfd < 0 || newfd >= WASM_MAX_FDS)
        m3ApiReturn(-1);
    if (proc->fds[oldfd].type == FD_NONE)
        m3ApiReturn(-1);

    if (proc->fds[newfd].type != FD_NONE) {
        wasm_fd_t *f = &proc->fds[newfd];
        switch (f->type) {
        case FD_FILE:
            if (f->file.dirty && f->file.data)
                vfs_write_file(f->file.parent_cluster, f->file.filename,
                               f->file.data, f->file.size);
            if (f->file.data)
                free(f->file.data);
            break;
        case FD_PIPE_READ:
            pipe_unref_read(f->pipe.pipe_id);
            break;
        case FD_PIPE_WRITE:
            pipe_unref_write(f->pipe.pipe_id);
            break;
        default:
            break;
        }
    }

    proc->fds[newfd] = proc->fds[oldfd];
    if (proc->fds[newfd].type == FD_PIPE_READ)
        pipe_ref_read(proc->fds[newfd].pipe.pipe_id);
    else if (proc->fds[newfd].type == FD_PIPE_WRITE)
        pipe_ref_write(proc->fds[newfd].pipe.pipe_id);

    m3ApiReturn(newfd);
}

m3ApiRawFunction(wasm_api_pipe)
{
    m3ApiReturnType(int32_t)
    m3ApiGetArgMem(int32_t *, fds_ptr)
    m3ApiCheckMem(fds_ptr, 8);

    wasm_process_t *proc = WASM_PROC(_ctx);
    int pipe_id = pipe_alloc();
    if (pipe_id < 0)
        m3ApiReturn(-1);

    int rfd = -1, wfd = -1;
    for (int i = 3; i < WASM_MAX_FDS && (rfd < 0 || wfd < 0); i++) {
        if (proc->fds[i].type == FD_NONE) {
            if (rfd < 0)
                rfd = i;
            else
                wfd = i;
        }
    }
    if (rfd < 0 || wfd < 0) {
        pipe_unref_read(pipe_id);
        pipe_unref_write(pipe_id);
        m3ApiReturn(-1);
    }

    proc->fds[rfd].type = FD_PIPE_READ;
    proc->fds[rfd].pipe.pipe_id = pipe_id;
    proc->fds[wfd].type = FD_PIPE_WRITE;
    proc->fds[wfd].pipe.pipe_id = pipe_id;

    fds_ptr[0] = rfd;
    fds_ptr[1] = wfd;
    m3ApiReturn(0);
}

m3ApiRawFunction(wasm_api_pipe_create)
{
    m3ApiReturnType(int32_t)
    int pipe_id = pipe_alloc();
    m3ApiReturn(pipe_id);
}

m3ApiRawFunction(wasm_api_pipe_close_read)
{
    m3ApiReturnType(int32_t)
    m3ApiGetArg(int32_t, pipe_id)
    pipe_unref_read(pipe_id);
    m3ApiReturn(0);
}

m3ApiRawFunction(wasm_api_pipe_close_write)
{
    m3ApiReturnType(int32_t)
    m3ApiGetArg(int32_t, pipe_id)
    pipe_unref_write(pipe_id);
    m3ApiReturn(0);
}

m3ApiRawFunction(wasm_api_spawn_redirected)
{
    m3ApiReturnType(int32_t)
    m3ApiGetArgMem(const char *, path)
    m3ApiGetArg(int32_t, path_len)
    m3ApiGetArgMem(const char *, args_buf)
    m3ApiGetArg(int32_t, args_len)
    m3ApiGetArg(int32_t, argc)
    m3ApiGetArgMem(const uint8_t *, redir_buf)
    m3ApiGetArg(int32_t, redir_len)
    m3ApiCheckMem(path, path_len);
    if (args_len > 0)
        m3ApiCheckMem(args_buf, args_len);
    if (redir_len > 0)
        m3ApiCheckMem(redir_buf, redir_len);

    char pathbuf[256];
    int copy_len = path_len < 255 ? path_len : 255;
    memcpy(pathbuf, path, copy_len);
    pathbuf[copy_len] = '\0';

    char *argv[WASM_MAX_ARGC];
    int actual_argc = 0;
    const char *p = args_buf;
    const char *end = args_buf + args_len;
    while (p < end && actual_argc < argc && actual_argc < WASM_MAX_ARGC) {
        argv[actual_argc++] = (char *)p;
        while (p < end && *p != '\0')
            p++;
        p++;
    }

    fd_setup_entry_t setups[SPAWN_MAX_FD_SETUP];
    int setup_count = 0;
    int pos = 0;
    while (pos + 12 <= redir_len && setup_count < SPAWN_MAX_FD_SETUP) {
        int32_t target_fd = *(int32_t *)(redir_buf + pos);
        int32_t type = *(int32_t *)(redir_buf + pos + 4);
        int32_t pipe_or_len = *(int32_t *)(redir_buf + pos + 8);
        pos += 12;

        setups[setup_count].target_fd = target_fd;
        setups[setup_count].type = (fd_setup_type_t)type;
        setups[setup_count].pipe_id = 0;
        setups[setup_count].path[0] = '\0';

        if (type == FD_SETUP_PIPE_READ || type == FD_SETUP_PIPE_WRITE) {
            setups[setup_count].pipe_id = pipe_or_len;
        } else if (type == FD_SETUP_FILE_READ || type == FD_SETUP_FILE_WRITE ||
                   type == FD_SETUP_FILE_APPEND) {
            int plen = pipe_or_len < 63 ? pipe_or_len : 63;
            if (pos + plen <= redir_len) {
                memcpy(setups[setup_count].path, redir_buf + pos, plen);
                setups[setup_count].path[plen] = '\0';
                pos += plen;
            }
        }
        setup_count++;
    }

    wasm_process_t *proc = WASM_PROC(_ctx);
    int32_t pid = wasm_spawn_redirected(pathbuf, actual_argc, argv, proc->pid,
                                        setups, setup_count);
    m3ApiReturn(pid);
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
    m3_LinkRawFunctionEx(module, "env", "spawn", "i(*i*ii)", &wasm_api_spawn, proc);
    m3_LinkRawFunctionEx(module, "env", "waitpid", "i(i)", &wasm_api_waitpid, proc);
    m3_LinkRawFunctionEx(module, "env", "kill", "i(i)", &wasm_api_kill, proc);
    m3_LinkRawFunctionEx(module, "env", "getpid", "i()", &wasm_api_getpid, proc);
    m3_LinkRawFunctionEx(module, "env", "dup2", "i(ii)", &wasm_api_dup2, proc);
    m3_LinkRawFunctionEx(module, "env", "pipe", "i(*)", &wasm_api_pipe, proc);
    m3_LinkRawFunctionEx(module, "env", "pipe_create", "i()", &wasm_api_pipe_create, proc);
    m3_LinkRawFunctionEx(module, "env", "pipe_close_read", "i(i)", &wasm_api_pipe_close_read, proc);
    m3_LinkRawFunctionEx(module, "env", "pipe_close_write", "i(i)", &wasm_api_pipe_close_write, proc);
    m3_LinkRawFunctionEx(module, "env", "spawn_redirected", "i(*i*ii*i)", &wasm_api_spawn_redirected, proc);
}
