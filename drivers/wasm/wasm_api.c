#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <fs.h>
#include <heap.h>
#include <keyboard.h>
#include <pit.h>
#include <process.h>
#include <scheduler.h>
#include <framebuffer.h>
#include <limine_defs.h>
#include <pipe.h>
#include <procfs.h>
#include <tty.h>
#include <waitqueue.h>
#include <wasm_api.h>
#include <wasm_runner.h>

#include "wasm_runtime.h"

#define WAMR_PROC(exec_env) ((wasm_process_t *)wasm_runtime_get_user_data(exec_env))

static uint32_t wamr_memory_size(wasm_exec_env_t exec_env)
{
    wasm_module_inst_t inst = wasm_runtime_get_module_inst(exec_env);
    WASMModuleInstance *mi = (WASMModuleInstance *)inst;
    if (mi->memory_count == 0)
        return 0;
    return mi->memories[0]->cur_page_count * 65536;
}

static uint8_t *wamr_memory_data(wasm_exec_env_t exec_env)
{
    wasm_module_inst_t inst = wasm_runtime_get_module_inst(exec_env);
    return (uint8_t *)wasm_runtime_addr_app_to_native(inst, 0);
}

static bool wamr_enlarge_memory(wasm_exec_env_t exec_env, uint32_t target_pages)
{
    wasm_module_inst_t inst = wasm_runtime_get_module_inst(exec_env);
    WASMModuleInstance *mi = (WASMModuleInstance *)inst;
    uint32_t cur = mi->memories[0]->cur_page_count;
    if (target_pages <= cur)
        return true;
    return wasm_runtime_enlarge_memory(inst, target_pages - cur);
}

wasm_process_t *wasm_process_create(int argc, char **argv)
{
    wasm_process_t *proc = malloc(sizeof(wasm_process_t));
    if (!proc)
        return NULL;
    memset(proc, 0, sizeof(wasm_process_t));

    proc->argc = argc < WASM_MAX_ARGC ? argc : WASM_MAX_ARGC;
    for (int i = 0; i < proc->argc; i++) {
        strncpy(proc->argv[i], argv[i], WASM_MAX_ARG_LEN - 1);
        proc->argv[i][WASM_MAX_ARG_LEN - 1] = '\0';
    }

    proc->fds[0].type = FD_CONSOLE;
    proc->fds[1].type = FD_CONSOLE;
    proc->fds[2].type = FD_CONSOLE;

    proc->brk_addr = 0;
    proc->mmap_top = 0;
    strncpy(proc->cwd, "/", sizeof(proc->cwd));

    proc->c_iflag = 0x0500;
    proc->c_oflag = 0x0005;
    proc->c_cflag = 0x00BF;
    proc->c_lflag = 0x8A3B;
    memset(proc->c_cc, 0, sizeof(proc->c_cc));
    proc->c_cc[0] = 0x03;
    proc->c_cc[1] = 0x1C;
    proc->c_cc[2] = 0x7F;
    proc->c_cc[3] = 0x15;
    proc->c_cc[4] = 0x04;
    proc->c_cc[5] = 0x00;
    proc->c_cc[6] = 0x01;
    proc->umask = 0022;

    return proc;
}

wasm_process_t *wasm_process_deep_copy(wasm_process_t *src)
{
    wasm_process_t *dst = malloc(sizeof(wasm_process_t));
    if (!dst)
        return NULL;
    memcpy(dst, src, sizeof(wasm_process_t));

    /* Clone file data buffers and ref-count pipes */
    for (int i = 0; i < WASM_MAX_FDS; i++) {
        wasm_fd_t *f = &dst->fds[i];
        switch (f->type) {
        case FD_FILE:
            if (f->file.data && f->file.size > 0) {
                uint8_t *copy = malloc(f->file.size);
                if (copy) {
                    memcpy(copy, f->file.data, f->file.size);
                    f->file.data = copy;
                } else {
                    f->file.data = NULL;
                    f->file.size = 0;
                }
            }
            break;
        case FD_PIPE_READ:
            pipe_ref_read(f->pipe.pipe_id);
            break;
        case FD_PIPE_WRITE:
            pipe_ref_write(f->pipe.pipe_id);
            break;
        default:
            break;
        }
    }

    /* Clear per-execution state that shouldn't be inherited */
    for (int i = 0; i < WASM_MAX_JMPBUFS; i++)
        dst->jmpbufs[i].active = false;
    dst->sig_pending = 0;
    dst->itimer_interval_us = 0;
    dst->itimer_value_us = 0;
    dst->itimer_next_tick = 0;

    /* Runtime pointers will be set by fork child entry */
    dst->wasm_module = NULL;
    dst->wasm_inst = NULL;
    dst->wasm_exec_env = NULL;
    dst->wasm_bytes = NULL;

    return dst;
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

static void wasm_fd_putchar(wasm_process_t *proc, char c)
{
    wasm_fd_t *f = &proc->fds[1];
    switch (f->type) {
    case FD_PIPE_WRITE:
        pipe_write(f->pipe.pipe_id, (const uint8_t *)&c, 1);
        break;
    default: {
        tty_t *tty = tty_get(proc->tty_id);
        tty_putchar(tty, c);
        break;
    }
    }
}

static void wasm_api_print(wasm_exec_env_t exec_env, const char *ptr, uint32_t len)
{
    wasm_process_t *proc = WAMR_PROC(exec_env);
    for (uint32_t i = 0; i < len; i++)
        wasm_fd_putchar(proc, ptr[i]);
}

static void wasm_api_putchar(wasm_exec_env_t exec_env, int32_t c)
{
    wasm_process_t *proc = WAMR_PROC(exec_env);
    wasm_fd_putchar(proc, c);
}

static int64_t wasm_api_get_ticks(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    return (int64_t)pit_ticks;
}

static void wasm_api_exit(wasm_exec_env_t exec_env, int32_t code)
{
    wasm_process_t *proc = WAMR_PROC(exec_env);
    proc->exit_code = code;
    wasm_module_inst_t inst = wasm_runtime_get_module_inst(exec_env);
    wasm_runtime_set_exception(inst, "wali exit");
}

static int32_t wasm_api_get_argc(wasm_exec_env_t exec_env)
{
    wasm_process_t *proc = WAMR_PROC(exec_env);
    return proc->argc;
}

static int32_t wasm_api_get_argv(wasm_exec_env_t exec_env, int32_t index,
                                  char *buf, int32_t buf_len)
{
    wasm_process_t *proc = WAMR_PROC(exec_env);
    if (index < 0 || index >= proc->argc)
        return -1;
    int len = strlen(proc->argv[index]);
    if (len >= buf_len)
        len = buf_len - 1;
    memcpy(buf, proc->argv[index], len);
    buf[len] = '\0';
    return len;
}

static int32_t wasm_api_getchar(wasm_exec_env_t exec_env)
{
    wasm_process_t *proc = WAMR_PROC(exec_env);
    tty_t *tty = tty_get(proc->tty_id);
    char c;
    while (!tty_input_pop(tty, &c)) {
        proc_entry_t *e = proc_get(proc->pid);
        if (e && e->killed) {
            proc->exit_code = 137;
            wasm_module_inst_t inst = wasm_runtime_get_module_inst(exec_env);
            wasm_runtime_set_exception(inst, "wali exit");
            return -1;
        }
        waitqueue_begin_sleep(&tty->input_wq);
        if (tty_input_pop(tty, &c)) {
            waitqueue_cancel_sleep(&tty->input_wq);
            break;
        }
        waitqueue_end_sleep(&tty->input_wq);
    }
    if (c == '\x03') {
        proc->exit_code = 130;
        wasm_module_inst_t inst = wasm_runtime_get_module_inst(exec_env);
        wasm_runtime_set_exception(inst, "wali exit");
        return -1;
    }
    if (c == '\x04')
        return -1;
    return (int32_t)c;
}

static int32_t wasm_api_read_line(wasm_exec_env_t exec_env, char *buf, int32_t max_len)
{
    wasm_process_t *proc = WAMR_PROC(exec_env);
    tty_t *tty = tty_get(proc->tty_id);
    int32_t i = 0;
    while (i < max_len - 1) {
        char c;
        while (!tty_input_pop(tty, &c)) {
            proc_entry_t *e = proc_get(proc->pid);
            if (e && e->killed) {
                proc->exit_code = 137;
                wasm_module_inst_t inst = wasm_runtime_get_module_inst(exec_env);
                wasm_runtime_set_exception(inst, "wali exit");
                return -1;
            }
            waitqueue_begin_sleep(&tty->input_wq);
            if (tty_input_pop(tty, &c)) {
                waitqueue_cancel_sleep(&tty->input_wq);
                break;
            }
            waitqueue_end_sleep(&tty->input_wq);
        }
        if (c == '\x03') {
            proc->exit_code = 130;
            wasm_module_inst_t inst = wasm_runtime_get_module_inst(exec_env);
            wasm_runtime_set_exception(inst, "wali exit");
            return -1;
        }
        if (c == '\x04') {
            if (i > 0)
                break;
            return -1;
        }
        if (c == '\n')
            break;
        buf[i++] = c;
    }
    buf[i] = '\0';
    return i;
}

static int32_t wasm_api_open(wasm_exec_env_t exec_env, const char *path,
                              int32_t path_len, int32_t flags)
{
    (void)path_len;
    wasm_process_t *proc = WAMR_PROC(exec_env);

    char pathbuf[256];
    int copy_len = path_len < 255 ? path_len : 255;
    memcpy(pathbuf, path, copy_len);
    pathbuf[copy_len] = '\0';

    if (strncmp(pathbuf, "/proc/", 6) == 0)
        return procfs_open(proc, pathbuf);

    int fd = wasm_fd_alloc(proc);
    if (fd < 0)
        return -1;

    uint32_t parent_cluster;
    char *filename = NULL;
    if (!vfs_resolve_path(pathbuf, &parent_cluster, &filename))
        return -1;

    wasm_fd_t *f = &proc->fds[fd];
    uint32_t size = 0;
    uint8_t *data = vfs_read_file(parent_cluster, filename, &size);

    if (!data && (flags & WASM_O_CREAT)) {
        data = malloc(1);
        size = 0;
    } else if (!data) {
        free(filename);
        return -1;
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

    return fd;
}

static int32_t wasm_api_close(wasm_exec_env_t exec_env, int32_t fd)
{
    wasm_process_t *proc = WAMR_PROC(exec_env);
    if (fd < 0 || fd >= WASM_MAX_FDS || proc->fds[fd].type == FD_NONE)
        return -1;

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
    return 0;
}

static int32_t wasm_api_read(wasm_exec_env_t exec_env, int32_t fd,
                              uint8_t *buf, int32_t count)
{
    wasm_process_t *proc = WAMR_PROC(exec_env);
    if (fd < 0 || fd >= WASM_MAX_FDS)
        return -1;

    wasm_fd_t *f = &proc->fds[fd];
    switch (f->type) {
    case FD_CONSOLE: {
        tty_t *tty = tty_get(proc->tty_id);
        int32_t i = 0;
        while (i < count) {
            char c;
            while (!tty_input_pop(tty, &c)) {
                proc_entry_t *e = proc_get(proc->pid);
                if (e && e->killed) {
                    proc->exit_code = 137;
                    wasm_module_inst_t inst = wasm_runtime_get_module_inst(exec_env);
                    wasm_runtime_set_exception(inst, "wali exit");
                    return -1;
                }
                waitqueue_begin_sleep(&tty->input_wq);
                if (tty_input_pop(tty, &c)) {
                    waitqueue_cancel_sleep(&tty->input_wq);
                    break;
                }
                waitqueue_end_sleep(&tty->input_wq);
            }
            if (c == '\x03') {
                proc->exit_code = 130;
                wasm_module_inst_t inst = wasm_runtime_get_module_inst(exec_env);
                wasm_runtime_set_exception(inst, "wali exit");
                return -1;
            }
            if (c == '\x04')
                return i;
            buf[i++] = (uint8_t)c;
            if (c == '\n')
                break;
        }
        return i;
    }
    case FD_FILE: {
        int32_t avail = f->file.size - f->file.pos;
        if (avail <= 0)
            return 0;
        if (count > avail)
            count = avail;
        memcpy(buf, f->file.data + f->file.pos, count);
        f->file.pos += count;
        return count;
    }
    case FD_PIPE_READ:
        return pipe_read(f->pipe.pipe_id, buf, count);
    default:
        return -1;
    }
}

static int32_t wasm_api_write(wasm_exec_env_t exec_env, int32_t fd,
                               const uint8_t *buf, int32_t count)
{
    wasm_process_t *proc = WAMR_PROC(exec_env);
    if (fd < 0 || fd >= WASM_MAX_FDS)
        return -1;

    wasm_fd_t *f = &proc->fds[fd];
    switch (f->type) {
    case FD_CONSOLE: {
        tty_t *tty = tty_get(proc->tty_id);
        tty_write(tty, (const char *)buf, count);
        return count;
    }
    case FD_FILE: {
        if (!f->file.writable)
            return -1;
        uint32_t end = f->file.pos + count;
        if (end > f->file.size) {
            uint8_t *new_data = realloc(f->file.data, end);
            if (!new_data)
                return -1;
            memset(new_data + f->file.size, 0, end - f->file.size);
            f->file.data = new_data;
            f->file.size = end;
        }
        memcpy(f->file.data + f->file.pos, buf, count);
        f->file.pos += count;
        f->file.dirty = true;
        return count;
    }
    case FD_PIPE_WRITE:
        return pipe_write(f->pipe.pipe_id, buf, count);
    default:
        return -1;
    }
}

static int32_t wasm_api_seek(wasm_exec_env_t exec_env, int32_t fd,
                              int32_t offset, int32_t whence)
{
    wasm_process_t *proc = WAMR_PROC(exec_env);
    if (fd < 0 || fd >= WASM_MAX_FDS || proc->fds[fd].type != FD_FILE)
        return -1;

    wasm_fd_t *f = &proc->fds[fd];
    int32_t new_pos;
    switch (whence) {
    case WASM_SEEK_SET: new_pos = offset; break;
    case WASM_SEEK_CUR: new_pos = (int32_t)f->file.pos + offset; break;
    case WASM_SEEK_END: new_pos = (int32_t)f->file.size + offset; break;
    default: return -1;
    }
    if (new_pos < 0)
        new_pos = 0;
    f->file.pos = (uint32_t)new_pos;
    return new_pos;
}

static int32_t wasm_api_stat(wasm_exec_env_t exec_env, const char *path,
                              int32_t path_len, uint32_t *stat_buf)
{
    (void)exec_env;
    char pathbuf[256];
    int copy_len = path_len < 255 ? path_len : 255;
    memcpy(pathbuf, path, copy_len);
    pathbuf[copy_len] = '\0';

    uint32_t parent_cluster;
    char *filename = NULL;
    if (!vfs_resolve_path(pathbuf, &parent_cluster, &filename))
        return -1;

    int count = 0;
    char **entries = vfs_list_directory(parent_cluster, &count);
    if (entries) {
        for (int i = 0; i < count; i++)
            free(entries[i]);
        free(entries);
        stat_buf[0] = 0;
        stat_buf[1] = VFS_DIRECTORY;
        free(filename);
        return 0;
    }

    uint32_t size = 0;
    uint8_t *data = vfs_read_file(parent_cluster, filename, &size);
    free(filename);
    if (!data)
        return -1;
    free(data);

    stat_buf[0] = size;
    stat_buf[1] = VFS_FILE;
    return 0;
}

static int32_t wasm_api_readdir(wasm_exec_env_t exec_env, const char *path,
                                 int32_t path_len, char *buf, int32_t buf_len)
{
    (void)exec_env;
    char pathbuf[256];
    int copy_len = path_len < 255 ? path_len : 255;
    memcpy(pathbuf, path, copy_len);
    pathbuf[copy_len] = '\0';

    vfs_mount_t *mount = vfs_get_mounted_fs();
    if (!mount)
        return -1;

    uint32_t cluster = mount->root_cluster;
    if (strcmp(pathbuf, "/") != 0) {
        uint32_t parent_cluster;
        char *filename = NULL;
        if (!vfs_resolve_path(pathbuf, &parent_cluster, &filename)) {
            if (filename) free(filename);
            return -1;
        }
        if (filename) free(filename);
    }

    int count = 0;
    char **entries = vfs_list_directory(cluster, &count);
    if (!entries)
        return -1;

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
    return written;
}

static int32_t wasm_api_mkdir(wasm_exec_env_t exec_env, const char *path, int32_t path_len)
{
    (void)exec_env;
    char pathbuf[256];
    int copy_len = path_len < 255 ? path_len : 255;
    memcpy(pathbuf, path, copy_len);
    pathbuf[copy_len] = '\0';

    uint32_t parent_cluster;
    char *dirname = NULL;
    if (!vfs_resolve_path(pathbuf, &parent_cluster, &dirname)) {
        if (dirname) free(dirname);
        return -1;
    }

    int32_t result = vfs_create_directory(parent_cluster, dirname) ? 0 : -1;
    free(dirname);
    return result;
}

static int32_t wasm_api_unlink(wasm_exec_env_t exec_env, const char *path, int32_t path_len)
{
    (void)exec_env;
    char pathbuf[256];
    int copy_len = path_len < 255 ? path_len : 255;
    memcpy(pathbuf, path, copy_len);
    pathbuf[copy_len] = '\0';

    uint32_t parent_cluster;
    char *filename = NULL;
    if (!vfs_resolve_path(pathbuf, &parent_cluster, &filename)) {
        if (filename) free(filename);
        return -1;
    }

    int32_t result = vfs_delete_file(parent_cluster, filename) ? 0 : -1;
    free(filename);
    return result;
}

static int32_t wasm_api_rmdir(wasm_exec_env_t exec_env, const char *path, int32_t path_len)
{
    (void)exec_env;
    char pathbuf[256];
    int copy_len = path_len < 255 ? path_len : 255;
    memcpy(pathbuf, path, copy_len);
    pathbuf[copy_len] = '\0';

    uint32_t parent_cluster;
    char *dirname = NULL;
    if (!vfs_resolve_path(pathbuf, &parent_cluster, &dirname)) {
        if (dirname) free(dirname);
        return -1;
    }

    int32_t result = vfs_delete_directory(parent_cluster, dirname) ? 0 : -1;
    free(dirname);
    return result;
}

static int32_t wasm_api_spawn(wasm_exec_env_t exec_env, const char *path,
                               int32_t path_len, const char *args_buf,
                               int32_t args_len, int32_t argc)
{
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

    wasm_process_t *proc = WAMR_PROC(exec_env);
    return wasm_spawn(pathbuf, actual_argc, argv, proc->pid);
}

static int32_t wasm_api_waitpid(wasm_exec_env_t exec_env, int32_t pid)
{
    (void)exec_env;
    proc_entry_t *e = proc_get(pid);
    if (!e)
        return -1;
    while (true) {
        waitqueue_begin_sleep(&e->exit_wq);
        if (e->state == PROC_EXITED) {
            waitqueue_cancel_sleep(&e->exit_wq);
            break;
        }
        waitqueue_end_sleep(&e->exit_wq);
    }
    int32_t code = e->exit_code;
    proc_free(pid);
    return code;
}

static int32_t wasm_api_kill(wasm_exec_env_t exec_env, int32_t pid)
{
    (void)exec_env;
    proc_entry_t *e = proc_get(pid);
    if (!e)
        return -1;
    e->killed = true;
    return 0;
}

static int32_t wasm_api_ptrace(wasm_exec_env_t exec_env, int32_t request,
                                int32_t pid, int32_t addr, int32_t data)
{
    (void)addr;

    switch (request) {
    case PTRACE_SYSCALL: {
        proc_entry_t *e = proc_get(pid);
        if (!e) return -1;
        e->ptrace_syscall = true;
        if (e->state == PROC_STOPPED) {
            e->state = PROC_RUNNING;
            waitqueue_wake_all(&e->exit_wq);
        }
        return 0;
    }
    case PTRACE_CONT: {
        proc_entry_t *e = proc_get(pid);
        if (!e) return -1;
        e->ptrace_syscall = false;
        if (e->state == PROC_STOPPED) {
            e->state = PROC_RUNNING;
            waitqueue_wake_all(&e->exit_wq);
        }
        return 0;
    }
    case PTRACE_GETREGS: {
        proc_entry_t *e = proc_get(pid);
        if (!e) return -1;
        wasm_module_inst_t inst = wasm_runtime_get_module_inst(exec_env);
        if (!wasm_runtime_validate_app_addr(inst, (uint64_t)data,
                                             (uint64_t)sizeof(ptrace_info_t)))
            return -1;
        ptrace_info_t *info = wasm_runtime_addr_app_to_native(inst,
                                                               (uint64_t)data);
        *info = e->ptrace_info;
        return 0;
    }
    default:
        return -1;
    }
}

static int32_t wasm_api_wait4(wasm_exec_env_t exec_env, int32_t pid,
                               int32_t *wstatus)
{
    (void)exec_env;
    if (pid <= 0)
        return -1;

    proc_entry_t *e = proc_get(pid);
    if (!e)
        return -1;

    while (true) {
        waitqueue_begin_sleep(&e->ptrace_wq);
        if (e->state == PROC_STOPPED || e->state == PROC_EXITED) {
            waitqueue_cancel_sleep(&e->ptrace_wq);
            break;
        }
        waitqueue_end_sleep(&e->ptrace_wq);
    }

    if (wstatus) {
        if (e->state == PROC_STOPPED)
            *wstatus = (5 << 8) | 0x7f;
        else
            *wstatus = (e->exit_code << 8);
    }
    return pid;
}

static int32_t wasm_api_getpid(wasm_exec_env_t exec_env)
{
    wasm_process_t *proc = WAMR_PROC(exec_env);
    return proc->pid;
}

static int32_t wasm_api_dup2(wasm_exec_env_t exec_env, int32_t oldfd, int32_t newfd)
{
    wasm_process_t *proc = WAMR_PROC(exec_env);
    if (oldfd < 0 || oldfd >= WASM_MAX_FDS || newfd < 0 || newfd >= WASM_MAX_FDS)
        return -1;
    if (proc->fds[oldfd].type == FD_NONE)
        return -1;

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

    return newfd;
}

static int32_t wasm_api_pipe(wasm_exec_env_t exec_env, int32_t *fds_ptr)
{
    wasm_process_t *proc = WAMR_PROC(exec_env);
    int pipe_id = pipe_alloc();
    if (pipe_id < 0)
        return -1;

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
        return -1;
    }

    proc->fds[rfd].type = FD_PIPE_READ;
    proc->fds[rfd].pipe.pipe_id = pipe_id;
    proc->fds[wfd].type = FD_PIPE_WRITE;
    proc->fds[wfd].pipe.pipe_id = pipe_id;

    fds_ptr[0] = rfd;
    fds_ptr[1] = wfd;
    return 0;
}

static int32_t wasm_api_pipe_create(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    return pipe_alloc();
}

static int32_t wasm_api_pipe_close_read(wasm_exec_env_t exec_env, int32_t pipe_id)
{
    (void)exec_env;
    pipe_unref_read(pipe_id);
    return 0;
}

static int32_t wasm_api_pipe_close_write(wasm_exec_env_t exec_env, int32_t pipe_id)
{
    (void)exec_env;
    pipe_unref_write(pipe_id);
    return 0;
}

static int32_t wasm_api_spawn_redirected(wasm_exec_env_t exec_env,
                                          const char *path, int32_t path_len,
                                          const char *args_buf, int32_t args_len,
                                          int32_t argc,
                                          const uint8_t *redir_buf, int32_t redir_len)
{
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

    wasm_process_t *proc = WAMR_PROC(exec_env);
    return wasm_spawn_redirected(pathbuf, actual_argc, argv, proc->pid,
                                 setups, setup_count);
}

static int32_t wasm_api_tty_set_mode(wasm_exec_env_t exec_env, int32_t mode)
{
    wasm_process_t *proc = WAMR_PROC(exec_env);
    tty_t *tty = tty_get(proc->tty_id);
    return tty_set_mode(tty, mode ? TTY_MODE_RAW : TTY_MODE_COOKED);
}

static int32_t wasm_api_tty_get_size(wasm_exec_env_t exec_env)
{
    wasm_process_t *proc = WAMR_PROC(exec_env);
    tty_t *tty = tty_get(proc->tty_id);
    return (int32_t)((tty->rows << 16) | (tty->cols & 0xFFFF));
}

static void wasm_api_fb_info(wasm_exec_env_t exec_env, uint32_t *buf)
{
    (void)exec_env;
    buf[0] = (uint32_t)fb->width;
    buf[1] = (uint32_t)fb->height;
    buf[2] = (uint32_t)(fb->width * 4);
    buf[3] = 32;
}

static uint32_t wasm_api_fb_alloc(wasm_exec_env_t exec_env, uint32_t w, uint32_t h)
{
    uint32_t needed = w * h * 4;
    uint32_t current_size = wamr_memory_size(exec_env);

    uint32_t alloc_offset = (current_size + 15) & ~15u;
    uint32_t total_needed = alloc_offset + needed;
    uint32_t pages_needed = (total_needed + 65535) / 65536;
    uint32_t current_pages = current_size / 65536;

    if (pages_needed > current_pages) {
        if (!wamr_enlarge_memory(exec_env, pages_needed)) {
            wasm_module_inst_t inst = wasm_runtime_get_module_inst(exec_env);
            wasm_runtime_set_exception(inst, "fb_alloc: failed to grow memory");
            return 0;
        }
    }

    return alloc_offset;
}

static void wasm_api_fb_flush(wasm_exec_env_t exec_env, uint32_t src_offset,
                               uint32_t dst_x, uint32_t dst_y,
                               uint32_t w, uint32_t h, uint32_t src_pitch)
{
    uint32_t mem_size = wamr_memory_size(exec_env);
    uint8_t *mem = wamr_memory_data(exec_env);
    if (!mem || src_offset + h * src_pitch > mem_size)
        return;

    uint8_t *src = mem + src_offset;
    uint32_t fb_pitch = fb->pitch;
    uint8_t *dst = (uint8_t *)fb_ptr + dst_y * fb_pitch + dst_x * 4;

    uint32_t max_w = (dst_x < fb->width) ? fb->width - dst_x : 0;
    uint32_t max_h = (dst_y < fb->height) ? fb->height - dst_y : 0;
    if (w > max_w) w = max_w;
    if (h > max_h) h = max_h;

    for (uint32_t row = 0; row < h; row++) {
        memcpy(dst, src, w * 4);
        src += src_pitch;
        dst += fb_pitch;
    }
}

static void wasm_api_fb_sync(wasm_exec_env_t exec_env)
{
    (void)exec_env;
}

static NativeSymbol env_symbols[] = {
    { "print",              (void *)wasm_api_print,             "(*~)",         NULL },
    { "putchar",            (void *)wasm_api_putchar,           "(i)",          NULL },
    { "get_ticks",          (void *)wasm_api_get_ticks,         "()I",          NULL },
    { "exit",               (void *)wasm_api_exit,              "(i)",          NULL },
    { "get_argc",           (void *)wasm_api_get_argc,          "()i",          NULL },
    { "get_argv",           (void *)wasm_api_get_argv,          "(i*~)i",       NULL },
    { "getchar",            (void *)wasm_api_getchar,           "()i",          NULL },
    { "read_line",          (void *)wasm_api_read_line,         "(*~)i",        NULL },
    { "open",               (void *)wasm_api_open,              "(*~i)i",       NULL },
    { "close",              (void *)wasm_api_close,             "(i)i",         NULL },
    { "read",               (void *)wasm_api_read,              "(i*~)i",       NULL },
    { "write",              (void *)wasm_api_write,             "(i*~)i",       NULL },
    { "seek",               (void *)wasm_api_seek,              "(iii)i",       NULL },
    { "stat",               (void *)wasm_api_stat,              "(*~*)i",       NULL },
    { "readdir",            (void *)wasm_api_readdir,           "(*~*~)i",      NULL },
    { "mkdir",              (void *)wasm_api_mkdir,             "(*~)i",        NULL },
    { "unlink",             (void *)wasm_api_unlink,            "(*~)i",        NULL },
    { "rmdir",              (void *)wasm_api_rmdir,             "(*~)i",        NULL },
    { "spawn",              (void *)wasm_api_spawn,             "(*~*~i)i",     NULL },
    { "waitpid",            (void *)wasm_api_waitpid,           "(i)i",         NULL },
    { "kill",               (void *)wasm_api_kill,              "(i)i",         NULL },
    { "ptrace",             (void *)wasm_api_ptrace,            "(iiii)i",      NULL },
    { "wait4",              (void *)wasm_api_wait4,             "(i*)i",        NULL },
    { "getpid",             (void *)wasm_api_getpid,            "()i",          NULL },
    { "dup2",               (void *)wasm_api_dup2,              "(ii)i",        NULL },
    { "pipe",               (void *)wasm_api_pipe,              "(*)i",         NULL },
    { "pipe_create",        (void *)wasm_api_pipe_create,       "()i",          NULL },
    { "pipe_close_read",    (void *)wasm_api_pipe_close_read,   "(i)i",         NULL },
    { "pipe_close_write",   (void *)wasm_api_pipe_close_write,  "(i)i",         NULL },
    { "spawn_redirected",   (void *)wasm_api_spawn_redirected,  "(*~*~i*~)i",   NULL },
    { "tty_set_mode",       (void *)wasm_api_tty_set_mode,      "(i)i",         NULL },
    { "tty_get_size",       (void *)wasm_api_tty_get_size,      "()i",          NULL },
    { "fb_info",            (void *)wasm_api_fb_info,           "(*)",          NULL },
    { "fb_alloc",           (void *)wasm_api_fb_alloc,          "(ii)i",        NULL },
    { "fb_flush",           (void *)wasm_api_fb_flush,          "(iiiiii)",     NULL },
    { "fb_sync",            (void *)wasm_api_fb_sync,           "()",           NULL },
};

void wasm_register_env_natives(void)
{
    wasm_runtime_register_natives("env", env_symbols,
                                  sizeof(env_symbols) / sizeof(NativeSymbol));
}
