#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <fs.h>
#include <heap.h>
#include <keyboard.h>
#include <pit.h>
#include <pipe.h>
#include <process.h>
#include <scheduler.h>
#include <tty.h>
#include <waitqueue.h>
#include <wasm_api.h>
#include <wali_defs.h>

#include "wasm3.h"
#include "m3_env.h"

#define WALI_PROC(ctx) ((wasm_process_t *)((ctx)->userdata))

#define WASM_PAGE_SIZE 65536
#define MMAP_REGION_TOP 0xF0000000u

static uint32_t wali_stub_mask[8];

static void wali_stub_log(int nr)
{
    int idx = nr / 32;
    int bit = nr % 32;
    if (idx < 8 && !(wali_stub_mask[idx] & (1u << bit))) {
        wali_stub_mask[idx] |= (1u << bit);
        printf("wali: unimplemented syscall %d\n", nr);
    }
}

/* Helper: resolve path using cwd for relative paths */
static bool wali_resolve_path(wasm_process_t *proc, const char *wasm_path,
                              char *out, int out_len)
{
    if (wasm_path[0] == '/') {
        strncpy(out, wasm_path, out_len - 1);
        out[out_len - 1] = '\0';
        return true;
    }
    int cwd_len = strlen(proc->cwd);
    if (cwd_len + 1 + (int)strlen(wasm_path) >= out_len)
        return false;
    strcpy(out, proc->cwd);
    if (proc->cwd[cwd_len - 1] != '/')
        strcat(out, "/");
    strcat(out, wasm_path);
    return true;
}

/* Helper: get a null-terminated string from wasm memory */
static const char *wali_get_string(IM3Runtime runtime, int32_t offset)
{
    uint32_t mem_size;
    uint8_t *mem = m3_GetMemory(runtime, &mem_size, 0);
    if (!mem || (uint32_t)offset >= mem_size)
        return NULL;
    return (const char *)(mem + offset);
}

static void *wali_get_mem(IM3Runtime runtime, int32_t offset, uint32_t len)
{
    uint32_t mem_size;
    uint8_t *mem = m3_GetMemory(runtime, &mem_size, 0);
    if (!mem || (uint64_t)offset + len > mem_size)
        return NULL;
    return mem + offset;
}

/* ---- Auxiliary functions (module "wali") ---- */

m3ApiRawFunction(wali_init)
{
    m3ApiReturnType(int32_t)
    m3ApiReturn(0);
}

m3ApiRawFunction(wali_deinit)
{
    m3ApiReturnType(int32_t)
    m3ApiReturn(0);
}

m3ApiRawFunction(wali_proc_exit)
{
    m3ApiGetArg(int32_t, code)
    wasm_process_t *proc = WALI_PROC(_ctx);
    proc->exit_code = code;
    m3ApiTrap(m3Err_trapExit);
}

m3ApiRawFunction(wali_cl_get_argc)
{
    m3ApiReturnType(uint32_t)
    wasm_process_t *proc = WALI_PROC(_ctx);
    m3ApiReturn((uint32_t)proc->argc);
}

m3ApiRawFunction(wali_cl_get_argv_len)
{
    m3ApiReturnType(uint32_t)
    m3ApiGetArg(uint32_t, index)
    wasm_process_t *proc = WALI_PROC(_ctx);
    if ((int)index >= proc->argc)
        m3ApiReturn(0);
    m3ApiReturn((uint32_t)strlen(proc->argv[index]) + 1);
}

m3ApiRawFunction(wali_cl_copy_argv)
{
    m3ApiReturnType(int32_t)
    m3ApiGetArgMem(char *, buf)
    m3ApiGetArg(uint32_t, index)
    wasm_process_t *proc = WALI_PROC(_ctx);
    if ((int)index >= proc->argc)
        m3ApiReturn(-1);
    uint32_t len = strlen(proc->argv[index]);
    m3ApiCheckMem(buf, len + 1);
    memcpy(buf, proc->argv[index], len + 1);
    m3ApiReturn(0);
}

m3ApiRawFunction(wali_get_init_envfile)
{
    m3ApiReturnType(int32_t)
    m3ApiGetArgMem(char *, buf)
    m3ApiGetArg(uint32_t, bufsize)
    m3ApiCheckMem(buf, bufsize);
    (void)buf;
    m3ApiReturn(0);
}

m3ApiRawFunction(wali_sigsetjmp)
{
    m3ApiReturnType(int32_t)
    m3ApiGetArg(int32_t, buf)
    m3ApiGetArg(int32_t, savesigs)
    (void)buf; (void)savesigs;
    m3ApiReturn(0);
}

m3ApiRawFunction(wali_setjmp)
{
    m3ApiReturnType(int32_t)
    m3ApiGetArg(int32_t, buf)
    (void)buf;
    m3ApiReturn(0);
}

m3ApiRawFunction(wali_longjmp)
{
    m3ApiGetArg(int32_t, buf)
    m3ApiGetArg(int32_t, val)
    (void)buf; (void)val;
    m3ApiTrap("longjmp not supported");
}

m3ApiRawFunction(wali_thread_spawn)
{
    m3ApiReturnType(int32_t)
    m3ApiGetArg(uint32_t, fn)
    m3ApiGetArg(int32_t, args)
    (void)fn; (void)args;
    m3ApiReturn(-L_ENOSYS);
}

/* ---- fd I/O helpers ---- */

static int64_t wali_do_write(wasm_process_t *proc, int32_t fd,
                             const uint8_t *buf, uint32_t count)
{
    if (fd < 0 || fd >= WASM_MAX_FDS)
        return -L_EBADF;
    wasm_fd_t *f = &proc->fds[fd];
    switch (f->type) {
    case FD_CONSOLE: {
        tty_t *tty = tty_get(proc->tty_id);
        tty_write(tty, (const char *)buf, count);
        return (int64_t)count;
    }
    case FD_FILE: {
        if (!f->file.writable)
            return -L_EBADF;
        uint32_t end = f->file.pos + count;
        if (end > f->file.size) {
            uint8_t *new_data = realloc(f->file.data, end);
            if (!new_data)
                return -L_ENOMEM;
            memset(new_data + f->file.size, 0, end - f->file.size);
            f->file.data = new_data;
            f->file.size = end;
        }
        memcpy(f->file.data + f->file.pos, buf, count);
        f->file.pos += count;
        f->file.dirty = true;
        return (int64_t)count;
    }
    case FD_PIPE_WRITE:
        return (int64_t)pipe_write(f->pipe.pipe_id, buf, count);
    default:
        return -L_EBADF;
    }
}

static int64_t wali_do_read(wasm_process_t *proc, int32_t fd,
                            uint8_t *buf, uint32_t count)
{
    if (fd < 0 || fd >= WASM_MAX_FDS)
        return -L_EBADF;
    wasm_fd_t *f = &proc->fds[fd];
    switch (f->type) {
    case FD_CONSOLE: {
        tty_t *tty = tty_get(proc->tty_id);
        int32_t i = 0;
        while (i < (int32_t)count) {
            char c;
            while (!tty_input_pop(tty, &c)) {
                proc_entry_t *e = proc_get(proc->pid);
                if (e && e->killed)
                    return -L_EINTR;
                waitqueue_sleep(&tty->input_wq);
            }
            if (c == '\x03')
                return -L_EINTR;
            if (c == '\x04')
                return i;
            buf[i++] = (uint8_t)c;
            if (c == '\n')
                break;
        }
        return (int64_t)i;
    }
    case FD_FILE: {
        int32_t avail = f->file.size - f->file.pos;
        if (avail <= 0)
            return 0;
        if ((int32_t)count > avail)
            count = avail;
        memcpy(buf, f->file.data + f->file.pos, count);
        f->file.pos += count;
        return (int64_t)count;
    }
    case FD_PIPE_READ:
        return (int64_t)pipe_read(f->pipe.pipe_id, buf, count);
    default:
        return -L_EBADF;
    }
}

/* ---- Syscalls ---- */

m3ApiRawFunction(wali_sys_brk)
{
    m3ApiReturnType(int64_t)
    m3ApiGetArg(uint32_t, addr)
    wasm_process_t *proc = WALI_PROC(_ctx);

    if (proc->brk_addr == 0) {
        uint32_t mem_size = m3_GetMemorySize(runtime);
        proc->brk_addr = mem_size;
        proc->mmap_top = MMAP_REGION_TOP;
    }

    if (addr == 0) {
        m3ApiReturn((int64_t)proc->brk_addr);
    }

    if (addr < proc->brk_addr) {
        proc->brk_addr = addr;
        m3ApiReturn((int64_t)proc->brk_addr);
    }

    uint32_t mem_size = m3_GetMemorySize(runtime);
    if (addr > mem_size) {
        uint32_t pages_needed = (addr + WASM_PAGE_SIZE - 1) / WASM_PAGE_SIZE;
        M3Result res = ResizeMemory(runtime, pages_needed);
        if (res)
            m3ApiReturn((int64_t)proc->brk_addr);
    }
    proc->brk_addr = addr;
    m3ApiReturn((int64_t)proc->brk_addr);
}

m3ApiRawFunction(wali_sys_mmap)
{
    m3ApiReturnType(int64_t)
    m3ApiGetArg(uint32_t, addr)
    m3ApiGetArg(uint32_t, length)
    m3ApiGetArg(int32_t, prot)
    m3ApiGetArg(int32_t, flags)
    m3ApiGetArg(int32_t, fd)
    m3ApiGetArg(int64_t, offset)
    (void)prot; (void)offset;

    wasm_process_t *proc = WALI_PROC(_ctx);

    if (proc->brk_addr == 0) {
        proc->brk_addr = m3_GetMemorySize(runtime);
        proc->mmap_top = MMAP_REGION_TOP;
    }

    if (!(flags & L_MAP_ANONYMOUS)) {
        (void)fd; (void)addr;
        m3ApiReturn(-L_ENOSYS);
    }

    uint32_t aligned_len = (length + WASM_PAGE_SIZE - 1) & ~(WASM_PAGE_SIZE - 1);
    uint32_t alloc_addr = proc->mmap_top - aligned_len;

    uint32_t mem_size = m3_GetMemorySize(runtime);
    if (alloc_addr + aligned_len > mem_size) {
        uint32_t pages_needed = (alloc_addr + aligned_len + WASM_PAGE_SIZE - 1) / WASM_PAGE_SIZE;
        M3Result res = ResizeMemory(runtime, pages_needed);
        if (res)
            m3ApiReturn(-L_ENOMEM);
    }

    proc->mmap_top = alloc_addr;

    uint8_t *mem = m3_GetMemory(runtime, &mem_size, 0);
    if (mem && alloc_addr + aligned_len <= mem_size)
        memset(mem + alloc_addr, 0, aligned_len);

    m3ApiReturn((int64_t)alloc_addr);
}

m3ApiRawFunction(wali_sys_munmap)
{
    m3ApiReturnType(int64_t)
    m3ApiGetArg(uint32_t, addr)
    m3ApiGetArg(uint32_t, length)
    (void)addr; (void)length;
    m3ApiReturn(0);
}

m3ApiRawFunction(wali_sys_mprotect)
{
    m3ApiReturnType(int64_t)
    m3ApiGetArg(uint32_t, addr)
    m3ApiGetArg(uint32_t, length)
    m3ApiGetArg(int32_t, prot)
    (void)addr; (void)length; (void)prot;
    m3ApiReturn(0);
}

m3ApiRawFunction(wali_sys_madvise)
{
    m3ApiReturnType(int64_t)
    m3ApiGetArg(uint32_t, addr)
    m3ApiGetArg(uint32_t, length)
    m3ApiGetArg(int32_t, advice)
    (void)addr; (void)length; (void)advice;
    m3ApiReturn(0);
}

m3ApiRawFunction(wali_sys_write)
{
    m3ApiReturnType(int64_t)
    m3ApiGetArg(int32_t, fd)
    m3ApiGetArg(int32_t, buf_off)
    m3ApiGetArg(uint32_t, count)

    wasm_process_t *proc = WALI_PROC(_ctx);
    const uint8_t *buf = wali_get_mem(runtime, buf_off, count);
    if (!buf)
        m3ApiReturn(-L_EFAULT);
    m3ApiReturn(wali_do_write(proc, fd, buf, count));
}

m3ApiRawFunction(wali_sys_read)
{
    m3ApiReturnType(int64_t)
    m3ApiGetArg(int32_t, fd)
    m3ApiGetArg(int32_t, buf_off)
    m3ApiGetArg(uint32_t, count)

    wasm_process_t *proc = WALI_PROC(_ctx);
    uint8_t *buf = wali_get_mem(runtime, buf_off, count);
    if (!buf)
        m3ApiReturn(-L_EFAULT);
    m3ApiReturn(wali_do_read(proc, fd, buf, count));
}

m3ApiRawFunction(wali_sys_writev)
{
    m3ApiReturnType(int64_t)
    m3ApiGetArg(int32_t, fd)
    m3ApiGetArg(int32_t, iov_off)
    m3ApiGetArg(int32_t, iovcnt)

    wasm_process_t *proc = WALI_PROC(_ctx);
    wasm_iovec_t *iov = wali_get_mem(runtime, iov_off, iovcnt * sizeof(wasm_iovec_t));
    if (!iov)
        m3ApiReturn(-L_EFAULT);

    int64_t total = 0;
    for (int i = 0; i < iovcnt; i++) {
        uint8_t *buf = wali_get_mem(runtime, iov[i].iov_base, iov[i].iov_len);
        if (!buf)
            m3ApiReturn(-L_EFAULT);
        int64_t r = wali_do_write(proc, fd, buf, iov[i].iov_len);
        if (r < 0) {
            if (total > 0)
                m3ApiReturn(total);
            m3ApiReturn(r);
        }
        total += r;
    }
    m3ApiReturn(total);
}

m3ApiRawFunction(wali_sys_readv)
{
    m3ApiReturnType(int64_t)
    m3ApiGetArg(int32_t, fd)
    m3ApiGetArg(int32_t, iov_off)
    m3ApiGetArg(int32_t, iovcnt)

    wasm_process_t *proc = WALI_PROC(_ctx);
    wasm_iovec_t *iov = wali_get_mem(runtime, iov_off, iovcnt * sizeof(wasm_iovec_t));
    if (!iov)
        m3ApiReturn(-L_EFAULT);

    int64_t total = 0;
    for (int i = 0; i < iovcnt; i++) {
        uint8_t *buf = wali_get_mem(runtime, iov[i].iov_base, iov[i].iov_len);
        if (!buf)
            m3ApiReturn(-L_EFAULT);
        int64_t r = wali_do_read(proc, fd, buf, iov[i].iov_len);
        if (r < 0) {
            if (total > 0)
                m3ApiReturn(total);
            m3ApiReturn(r);
        }
        total += r;
        if (r < (int64_t)iov[i].iov_len)
            break;
    }
    m3ApiReturn(total);
}

m3ApiRawFunction(wali_sys_exit)
{
    m3ApiReturnType(int64_t)
    m3ApiGetArg(int32_t, code)
    wasm_process_t *proc = WALI_PROC(_ctx);
    proc->exit_code = code;
    m3ApiTrap(m3Err_trapExit);
}

m3ApiRawFunction(wali_sys_exit_group)
{
    m3ApiReturnType(int64_t)
    m3ApiGetArg(int32_t, code)
    wasm_process_t *proc = WALI_PROC(_ctx);
    proc->exit_code = code;
    m3ApiTrap(m3Err_trapExit);
}

m3ApiRawFunction(wali_sys_set_tid_address)
{
    m3ApiReturnType(int64_t)
    m3ApiGetArg(int32_t, tidptr)
    (void)tidptr;
    wasm_process_t *proc = WALI_PROC(_ctx);
    m3ApiReturn((int64_t)proc->pid);
}

m3ApiRawFunction(wali_sys_rt_sigaction)
{
    m3ApiReturnType(int64_t)
    m3ApiGetArg(int32_t, signum)
    m3ApiGetArg(int32_t, act)
    m3ApiGetArg(int32_t, oldact)
    m3ApiGetArg(uint32_t, sigsetsize)
    (void)signum; (void)act; (void)oldact; (void)sigsetsize;
    m3ApiReturn(0);
}

m3ApiRawFunction(wali_sys_rt_sigprocmask)
{
    m3ApiReturnType(int64_t)
    m3ApiGetArg(int32_t, how)
    m3ApiGetArg(int32_t, set)
    m3ApiGetArg(int32_t, oldset)
    m3ApiGetArg(uint32_t, sigsetsize)
    (void)how; (void)set; (void)oldset; (void)sigsetsize;
    m3ApiReturn(0);
}

m3ApiRawFunction(wali_sys_sigaltstack)
{
    m3ApiReturnType(int64_t)
    m3ApiGetArg(int32_t, ss)
    m3ApiGetArg(int32_t, old_ss)
    (void)ss; (void)old_ss;
    m3ApiReturn(0);
}

m3ApiRawFunction(wali_sys_getpid)
{
    m3ApiReturnType(int64_t)
    wasm_process_t *proc = WALI_PROC(_ctx);
    m3ApiReturn((int64_t)proc->pid);
}

m3ApiRawFunction(wali_sys_gettid)
{
    m3ApiReturnType(int64_t)
    wasm_process_t *proc = WALI_PROC(_ctx);
    m3ApiReturn((int64_t)proc->pid);
}

m3ApiRawFunction(wali_sys_getuid)
{
    m3ApiReturnType(int64_t)
    m3ApiReturn(0);
}

m3ApiRawFunction(wali_sys_getgid)
{
    m3ApiReturnType(int64_t)
    m3ApiReturn(0);
}

m3ApiRawFunction(wali_sys_geteuid)
{
    m3ApiReturnType(int64_t)
    m3ApiReturn(0);
}

m3ApiRawFunction(wali_sys_getegid)
{
    m3ApiReturnType(int64_t)
    m3ApiReturn(0);
}

m3ApiRawFunction(wali_sys_getppid)
{
    m3ApiReturnType(int64_t)
    wasm_process_t *proc = WALI_PROC(_ctx);
    proc_entry_t *e = proc_get(proc->pid);
    m3ApiReturn(e ? (int64_t)e->parent_pid : 0);
}

m3ApiRawFunction(wali_sys_getpgid)
{
    m3ApiReturnType(int64_t)
    m3ApiGetArg(int32_t, pid)
    (void)pid;
    wasm_process_t *proc = WALI_PROC(_ctx);
    m3ApiReturn((int64_t)proc->pid);
}

m3ApiRawFunction(wali_sys_setpgid)
{
    m3ApiReturnType(int64_t)
    m3ApiGetArg(int32_t, pid)
    m3ApiGetArg(int32_t, pgid)
    (void)pid; (void)pgid;
    m3ApiReturn(0);
}

m3ApiRawFunction(wali_sys_setsid)
{
    m3ApiReturnType(int64_t)
    wasm_process_t *proc = WALI_PROC(_ctx);
    m3ApiReturn((int64_t)proc->pid);
}

m3ApiRawFunction(wali_sys_sched_yield)
{
    m3ApiReturnType(int64_t)
    scheduler_yield();
    m3ApiReturn(0);
}

/* ---- Phase 2: File I/O ---- */

static int wali_fd_alloc(wasm_process_t *proc)
{
    for (int i = 3; i < WASM_MAX_FDS; i++)
        if (proc->fds[i].type == FD_NONE)
            return i;
    return -1;
}

static int wali_fd_alloc_from(wasm_process_t *proc, int min_fd)
{
    for (int i = min_fd; i < WASM_MAX_FDS; i++)
        if (proc->fds[i].type == FD_NONE)
            return i;
    return -1;
}

static int64_t wali_do_open(wasm_process_t *proc, const char *pathname, int32_t flags)
{
    int fd = wali_fd_alloc(proc);
    if (fd < 0)
        return -L_EMFILE;

    char pathbuf[256];
    if (!wali_resolve_path(proc, pathname, pathbuf, sizeof(pathbuf)))
        return -L_ENAMETOOLONG;

    uint32_t parent_cluster;
    char *filename = NULL;

    if (flags & L_O_DIRECTORY) {
        if (!vfs_resolve_path(pathbuf, &parent_cluster, &filename)) {
            if (filename) free(filename);
            return -L_ENOENT;
        }
        int count = 0;
        char **entries = vfs_list_directory(parent_cluster, &count);
        if (!entries) {
            free(filename);
            return -L_ENOTDIR;
        }
        for (int i = 0; i < count; i++)
            free(entries[i]);
        free(entries);

        wasm_fd_t *f = &proc->fds[fd];
        f->type = FD_FILE;
        f->file.data = NULL;
        f->file.size = 0;
        f->file.pos = 0;
        f->file.writable = false;
        f->file.dirty = false;
        f->file.flags = flags;
        f->file.parent_cluster = parent_cluster;
        strncpy(f->file.filename, filename, 11);
        f->file.filename[11] = '\0';
        free(filename);
        return fd;
    }

    if (!vfs_resolve_path(pathbuf, &parent_cluster, &filename)) {
        if (filename) free(filename);
        return -L_ENOENT;
    }

    uint32_t size = 0;
    uint8_t *data = vfs_read_file(parent_cluster, filename, &size);

    if (!data && (flags & L_O_CREAT)) {
        data = malloc(1);
        size = 0;
    } else if (!data) {
        free(filename);
        return -L_ENOENT;
    }

    wasm_fd_t *f = &proc->fds[fd];
    f->type = FD_FILE;
    f->file.data = data;
    f->file.size = size;
    f->file.pos = (flags & L_O_APPEND) ? size : 0;
    f->file.writable = (flags & (L_O_WRONLY | L_O_RDWR)) != 0;
    f->file.dirty = false;
    f->file.flags = flags;
    f->file.parent_cluster = parent_cluster;
    strncpy(f->file.filename, filename, 11);
    f->file.filename[11] = '\0';
    free(filename);

    if (flags & L_O_TRUNC) {
        f->file.size = 0;
        f->file.pos = 0;
        f->file.dirty = true;
    }

    return fd;
}

m3ApiRawFunction(wali_sys_open)
{
    m3ApiReturnType(int64_t)
    m3ApiGetArg(int32_t, pathname_off)
    m3ApiGetArg(int32_t, flags)
    m3ApiGetArg(int32_t, mode)
    (void)mode;

    wasm_process_t *proc = WALI_PROC(_ctx);
    const char *pathname = wali_get_string(runtime, pathname_off);
    if (!pathname)
        m3ApiReturn(-L_EFAULT);
    m3ApiReturn(wali_do_open(proc, pathname, flags));
}

m3ApiRawFunction(wali_sys_openat)
{
    m3ApiReturnType(int64_t)
    m3ApiGetArg(int32_t, dirfd)
    m3ApiGetArg(int32_t, pathname_off)
    m3ApiGetArg(int32_t, flags)
    m3ApiGetArg(int32_t, mode)
    (void)mode;

    wasm_process_t *proc = WALI_PROC(_ctx);
    const char *pathname = wali_get_string(runtime, pathname_off);
    if (!pathname)
        m3ApiReturn(-L_EFAULT);

    if (dirfd == L_AT_FDCWD || pathname[0] == '/') {
        m3ApiReturn(wali_do_open(proc, pathname, flags));
    }
    m3ApiReturn(-L_ENOSYS);
}

m3ApiRawFunction(wali_sys_close)
{
    m3ApiReturnType(int64_t)
    m3ApiGetArg(int32_t, fd)

    wasm_process_t *proc = WALI_PROC(_ctx);
    if (fd < 0 || fd >= WASM_MAX_FDS || proc->fds[fd].type == FD_NONE)
        m3ApiReturn(-L_EBADF);

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
    proc->fd_flags[fd] = 0;
    m3ApiReturn(0);
}

m3ApiRawFunction(wali_sys_lseek)
{
    m3ApiReturnType(int64_t)
    m3ApiGetArg(int32_t, fd)
    m3ApiGetArg(int64_t, offset)
    m3ApiGetArg(int32_t, whence)

    wasm_process_t *proc = WALI_PROC(_ctx);
    if (fd < 0 || fd >= WASM_MAX_FDS)
        m3ApiReturn(-L_EBADF);
    wasm_fd_t *f = &proc->fds[fd];
    if (f->type == FD_CONSOLE)
        m3ApiReturn(-L_ESPIPE);
    if (f->type != FD_FILE)
        m3ApiReturn(-L_EBADF);

    int64_t new_pos;
    switch (whence) {
    case 0: new_pos = offset; break;
    case 1: new_pos = (int64_t)f->file.pos + offset; break;
    case 2: new_pos = (int64_t)f->file.size + offset; break;
    default: m3ApiReturn(-L_EINVAL);
    }
    if (new_pos < 0)
        m3ApiReturn(-L_EINVAL);
    f->file.pos = (uint32_t)new_pos;
    m3ApiReturn(new_pos);
}

static void wali_fill_stat(linux_stat_t *st, uint32_t size, uint32_t mode)
{
    memset(st, 0, sizeof(linux_stat_t));
    st->st_dev = 1;
    st->st_ino = 1;
    st->st_nlink = 1;
    st->st_mode = mode;
    st->st_size = size;
    st->st_blksize = 512;
    st->st_blocks = (size + 511) / 512;
}

static int64_t wali_do_stat_path(wasm_process_t *proc, const char *pathname,
                                 linux_stat_t *st)
{
    char pathbuf[256];
    if (!wali_resolve_path(proc, pathname, pathbuf, sizeof(pathbuf)))
        return -L_ENAMETOOLONG;

    uint32_t parent_cluster;
    char *filename = NULL;
    if (!vfs_resolve_path(pathbuf, &parent_cluster, &filename)) {
        if (filename) free(filename);
        return -L_ENOENT;
    }

    int count = 0;
    char **entries = vfs_list_directory(parent_cluster, &count);
    if (entries) {
        for (int i = 0; i < count; i++)
            free(entries[i]);
        free(entries);
        wali_fill_stat(st, 0, L_S_IFDIR | 0755);
        free(filename);
        return 0;
    }

    uint32_t size = 0;
    uint8_t *data = vfs_read_file(parent_cluster, filename, &size);
    free(filename);
    if (!data)
        return -L_ENOENT;
    free(data);
    wali_fill_stat(st, size, L_S_IFREG | 0644);
    return 0;
}

m3ApiRawFunction(wali_sys_stat)
{
    m3ApiReturnType(int64_t)
    m3ApiGetArg(int32_t, pathname_off)
    m3ApiGetArg(int32_t, statbuf_off)

    wasm_process_t *proc = WALI_PROC(_ctx);
    const char *pathname = wali_get_string(runtime, pathname_off);
    linux_stat_t *st = wali_get_mem(runtime, statbuf_off, sizeof(linux_stat_t));
    if (!pathname || !st)
        m3ApiReturn(-L_EFAULT);
    m3ApiReturn(wali_do_stat_path(proc, pathname, st));
}

m3ApiRawFunction(wali_sys_lstat)
{
    m3ApiReturnType(int64_t)
    m3ApiGetArg(int32_t, pathname_off)
    m3ApiGetArg(int32_t, statbuf_off)

    wasm_process_t *proc = WALI_PROC(_ctx);
    const char *pathname = wali_get_string(runtime, pathname_off);
    linux_stat_t *st = wali_get_mem(runtime, statbuf_off, sizeof(linux_stat_t));
    if (!pathname || !st)
        m3ApiReturn(-L_EFAULT);
    m3ApiReturn(wali_do_stat_path(proc, pathname, st));
}

m3ApiRawFunction(wali_sys_fstat)
{
    m3ApiReturnType(int64_t)
    m3ApiGetArg(int32_t, fd)
    m3ApiGetArg(int32_t, statbuf_off)

    wasm_process_t *proc = WALI_PROC(_ctx);
    linux_stat_t *st = wali_get_mem(runtime, statbuf_off, sizeof(linux_stat_t));
    if (!st)
        m3ApiReturn(-L_EFAULT);
    if (fd < 0 || fd >= WASM_MAX_FDS)
        m3ApiReturn(-L_EBADF);

    wasm_fd_t *f = &proc->fds[fd];
    switch (f->type) {
    case FD_CONSOLE:
        wali_fill_stat(st, 0, L_S_IFCHR | 0620);
        m3ApiReturn(0);
    case FD_FILE:
        wali_fill_stat(st, f->file.size, L_S_IFREG | 0644);
        m3ApiReturn(0);
    case FD_PIPE_READ:
    case FD_PIPE_WRITE:
        wali_fill_stat(st, 0, L_S_IFIFO | 0600);
        m3ApiReturn(0);
    default:
        m3ApiReturn(-L_EBADF);
    }
}

m3ApiRawFunction(wali_sys_newfstatat)
{
    m3ApiReturnType(int64_t)
    m3ApiGetArg(int32_t, dirfd)
    m3ApiGetArg(int32_t, pathname_off)
    m3ApiGetArg(int32_t, statbuf_off)
    m3ApiGetArg(int32_t, flags)
    (void)flags;

    wasm_process_t *proc = WALI_PROC(_ctx);
    const char *pathname = wali_get_string(runtime, pathname_off);
    linux_stat_t *st = wali_get_mem(runtime, statbuf_off, sizeof(linux_stat_t));
    if (!pathname || !st)
        m3ApiReturn(-L_EFAULT);

    if (pathname[0] == '\0' && (flags & 0x1000)) {
        if (dirfd < 0 || dirfd >= WASM_MAX_FDS)
            m3ApiReturn(-L_EBADF);
        wasm_fd_t *f = &proc->fds[dirfd];
        if (f->type == FD_CONSOLE)
            wali_fill_stat(st, 0, L_S_IFCHR | 0620);
        else if (f->type == FD_FILE)
            wali_fill_stat(st, f->file.size, L_S_IFREG | 0644);
        else
            m3ApiReturn(-L_EBADF);
        m3ApiReturn(0);
    }

    if (dirfd != L_AT_FDCWD && pathname[0] != '/')
        m3ApiReturn(-L_ENOSYS);

    m3ApiReturn(wali_do_stat_path(proc, pathname, st));
}

m3ApiRawFunction(wali_sys_access)
{
    m3ApiReturnType(int64_t)
    m3ApiGetArg(int32_t, pathname_off)
    m3ApiGetArg(int32_t, mode)
    (void)mode;

    wasm_process_t *proc = WALI_PROC(_ctx);
    const char *pathname = wali_get_string(runtime, pathname_off);
    if (!pathname)
        m3ApiReturn(-L_EFAULT);

    char pathbuf[256];
    if (!wali_resolve_path(proc, pathname, pathbuf, sizeof(pathbuf)))
        m3ApiReturn(-L_ENAMETOOLONG);

    uint32_t parent_cluster;
    char *filename = NULL;
    if (!vfs_resolve_path(pathbuf, &parent_cluster, &filename)) {
        if (filename) free(filename);
        m3ApiReturn(-L_ENOENT);
    }
    free(filename);
    m3ApiReturn(0);
}

m3ApiRawFunction(wali_sys_faccessat)
{
    m3ApiReturnType(int64_t)
    m3ApiGetArg(int32_t, dirfd)
    m3ApiGetArg(int32_t, pathname_off)
    m3ApiGetArg(int32_t, mode)
    m3ApiGetArg(int32_t, flags)
    (void)mode; (void)flags;

    wasm_process_t *proc = WALI_PROC(_ctx);
    const char *pathname = wali_get_string(runtime, pathname_off);
    if (!pathname)
        m3ApiReturn(-L_EFAULT);
    if (dirfd != L_AT_FDCWD && pathname[0] != '/')
        m3ApiReturn(-L_ENOSYS);

    char pathbuf[256];
    if (!wali_resolve_path(proc, pathname, pathbuf, sizeof(pathbuf)))
        m3ApiReturn(-L_ENAMETOOLONG);
    uint32_t parent_cluster;
    char *filename = NULL;
    if (!vfs_resolve_path(pathbuf, &parent_cluster, &filename)) {
        if (filename) free(filename);
        m3ApiReturn(-L_ENOENT);
    }
    free(filename);
    m3ApiReturn(0);
}

m3ApiRawFunction(wali_sys_getcwd)
{
    m3ApiReturnType(int64_t)
    m3ApiGetArg(int32_t, buf_off)
    m3ApiGetArg(uint32_t, size)

    wasm_process_t *proc = WALI_PROC(_ctx);
    uint32_t len = strlen(proc->cwd) + 1;
    if (size < len)
        m3ApiReturn(-L_ERANGE);
    char *buf = wali_get_mem(runtime, buf_off, len);
    if (!buf)
        m3ApiReturn(-L_EFAULT);
    memcpy(buf, proc->cwd, len);
    m3ApiReturn((int64_t)buf_off);
}

m3ApiRawFunction(wali_sys_chdir)
{
    m3ApiReturnType(int64_t)
    m3ApiGetArg(int32_t, path_off)

    wasm_process_t *proc = WALI_PROC(_ctx);
    const char *path = wali_get_string(runtime, path_off);
    if (!path)
        m3ApiReturn(-L_EFAULT);

    char pathbuf[256];
    if (!wali_resolve_path(proc, path, pathbuf, sizeof(pathbuf)))
        m3ApiReturn(-L_ENAMETOOLONG);

    uint32_t parent_cluster;
    char *filename = NULL;
    if (!vfs_resolve_path(pathbuf, &parent_cluster, &filename)) {
        if (filename) free(filename);
        m3ApiReturn(-L_ENOENT);
    }
    free(filename);

    strncpy(proc->cwd, pathbuf, sizeof(proc->cwd) - 1);
    proc->cwd[sizeof(proc->cwd) - 1] = '\0';
    m3ApiReturn(0);
}

m3ApiRawFunction(wali_sys_fcntl)
{
    m3ApiReturnType(int64_t)
    m3ApiGetArg(int32_t, fd)
    m3ApiGetArg(int32_t, cmd)
    m3ApiGetArg(uint64_t, arg)

    wasm_process_t *proc = WALI_PROC(_ctx);
    if (fd < 0 || fd >= WASM_MAX_FDS || proc->fds[fd].type == FD_NONE)
        m3ApiReturn(-L_EBADF);

    switch (cmd) {
    case L_F_DUPFD: {
        int new_fd = wali_fd_alloc_from(proc, (int)arg);
        if (new_fd < 0)
            m3ApiReturn(-L_EMFILE);
        proc->fds[new_fd] = proc->fds[fd];
        if (proc->fds[new_fd].type == FD_PIPE_READ)
            pipe_ref_read(proc->fds[new_fd].pipe.pipe_id);
        else if (proc->fds[new_fd].type == FD_PIPE_WRITE)
            pipe_ref_write(proc->fds[new_fd].pipe.pipe_id);
        else if (proc->fds[new_fd].type == FD_FILE && proc->fds[new_fd].file.data) {
            uint32_t sz = proc->fds[fd].file.size;
            uint8_t *copy = malloc(sz ? sz : 1);
            if (sz) memcpy(copy, proc->fds[fd].file.data, sz);
            proc->fds[new_fd].file.data = copy;
        }
        m3ApiReturn(new_fd);
    }
    case L_F_GETFD:
        m3ApiReturn((int64_t)proc->fd_flags[fd]);
    case L_F_SETFD:
        proc->fd_flags[fd] = (uint32_t)arg;
        m3ApiReturn(0);
    case L_F_GETFL:
        if (proc->fds[fd].type == FD_FILE)
            m3ApiReturn((int64_t)proc->fds[fd].file.flags);
        m3ApiReturn(0);
    case L_F_SETFL:
        if (proc->fds[fd].type == FD_FILE)
            proc->fds[fd].file.flags = (uint32_t)arg;
        m3ApiReturn(0);
    default:
        m3ApiReturn(-L_EINVAL);
    }
}

m3ApiRawFunction(wali_sys_dup)
{
    m3ApiReturnType(int64_t)
    m3ApiGetArg(int32_t, oldfd)

    wasm_process_t *proc = WALI_PROC(_ctx);
    if (oldfd < 0 || oldfd >= WASM_MAX_FDS || proc->fds[oldfd].type == FD_NONE)
        m3ApiReturn(-L_EBADF);

    int newfd = wali_fd_alloc(proc);
    if (newfd < 0)
        m3ApiReturn(-L_EMFILE);

    proc->fds[newfd] = proc->fds[oldfd];
    if (proc->fds[newfd].type == FD_PIPE_READ)
        pipe_ref_read(proc->fds[newfd].pipe.pipe_id);
    else if (proc->fds[newfd].type == FD_PIPE_WRITE)
        pipe_ref_write(proc->fds[newfd].pipe.pipe_id);
    else if (proc->fds[newfd].type == FD_FILE && proc->fds[newfd].file.data) {
        uint32_t sz = proc->fds[oldfd].file.size;
        uint8_t *copy = malloc(sz ? sz : 1);
        if (sz) memcpy(copy, proc->fds[oldfd].file.data, sz);
        proc->fds[newfd].file.data = copy;
    }
    m3ApiReturn(newfd);
}

static int64_t wali_do_dup2(wasm_process_t *proc, int32_t oldfd, int32_t newfd)
{
    if (oldfd < 0 || oldfd >= WASM_MAX_FDS || proc->fds[oldfd].type == FD_NONE)
        return -L_EBADF;
    if (newfd < 0 || newfd >= WASM_MAX_FDS)
        return -L_EBADF;
    if (oldfd == newfd)
        return newfd;

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
    else if (proc->fds[newfd].type == FD_FILE && proc->fds[newfd].file.data) {
        uint32_t sz = proc->fds[oldfd].file.size;
        uint8_t *copy = malloc(sz ? sz : 1);
        if (sz) memcpy(copy, proc->fds[oldfd].file.data, sz);
        proc->fds[newfd].file.data = copy;
    }
    return newfd;
}

m3ApiRawFunction(wali_sys_dup2)
{
    m3ApiReturnType(int64_t)
    m3ApiGetArg(int32_t, oldfd)
    m3ApiGetArg(int32_t, newfd)
    wasm_process_t *proc = WALI_PROC(_ctx);
    m3ApiReturn(wali_do_dup2(proc, oldfd, newfd));
}

m3ApiRawFunction(wali_sys_dup3)
{
    m3ApiReturnType(int64_t)
    m3ApiGetArg(int32_t, oldfd)
    m3ApiGetArg(int32_t, newfd)
    m3ApiGetArg(int32_t, flags)
    (void)flags;
    wasm_process_t *proc = WALI_PROC(_ctx);
    if (oldfd == newfd)
        m3ApiReturn(-L_EINVAL);
    m3ApiReturn(wali_do_dup2(proc, oldfd, newfd));
}

m3ApiRawFunction(wali_sys_pipe)
{
    m3ApiReturnType(int64_t)
    m3ApiGetArg(int32_t, pipefd_off)

    wasm_process_t *proc = WALI_PROC(_ctx);
    int32_t *fds = wali_get_mem(runtime, pipefd_off, 8);
    if (!fds)
        m3ApiReturn(-L_EFAULT);

    int pipe_id = pipe_alloc();
    if (pipe_id < 0)
        m3ApiReturn(-L_EMFILE);

    int rfd = -1, wfd = -1;
    for (int i = 3; i < WASM_MAX_FDS && (rfd < 0 || wfd < 0); i++) {
        if (proc->fds[i].type == FD_NONE) {
            if (rfd < 0) rfd = i;
            else wfd = i;
        }
    }
    if (rfd < 0 || wfd < 0) {
        pipe_unref_read(pipe_id);
        pipe_unref_write(pipe_id);
        m3ApiReturn(-L_EMFILE);
    }

    proc->fds[rfd].type = FD_PIPE_READ;
    proc->fds[rfd].pipe.pipe_id = pipe_id;
    proc->fds[wfd].type = FD_PIPE_WRITE;
    proc->fds[wfd].pipe.pipe_id = pipe_id;

    fds[0] = rfd;
    fds[1] = wfd;
    m3ApiReturn(0);
}

m3ApiRawFunction(wali_sys_pipe2)
{
    m3ApiReturnType(int64_t)
    m3ApiGetArg(int32_t, pipefd_off)
    m3ApiGetArg(int32_t, flags)
    (void)flags;

    wasm_process_t *proc = WALI_PROC(_ctx);
    int32_t *fds = wali_get_mem(runtime, pipefd_off, 8);
    if (!fds)
        m3ApiReturn(-L_EFAULT);

    int pipe_id = pipe_alloc();
    if (pipe_id < 0)
        m3ApiReturn(-L_EMFILE);

    int rfd = -1, wfd = -1;
    for (int i = 3; i < WASM_MAX_FDS && (rfd < 0 || wfd < 0); i++) {
        if (proc->fds[i].type == FD_NONE) {
            if (rfd < 0) rfd = i;
            else wfd = i;
        }
    }
    if (rfd < 0 || wfd < 0) {
        pipe_unref_read(pipe_id);
        pipe_unref_write(pipe_id);
        m3ApiReturn(-L_EMFILE);
    }

    proc->fds[rfd].type = FD_PIPE_READ;
    proc->fds[rfd].pipe.pipe_id = pipe_id;
    proc->fds[wfd].type = FD_PIPE_WRITE;
    proc->fds[wfd].pipe.pipe_id = pipe_id;

    fds[0] = rfd;
    fds[1] = wfd;
    m3ApiReturn(0);
}

m3ApiRawFunction(wali_sys_getdents64)
{
    m3ApiReturnType(int64_t)
    m3ApiGetArg(int32_t, fd)
    m3ApiGetArg(int32_t, dirp_off)
    m3ApiGetArg(int32_t, count)

    wasm_process_t *proc = WALI_PROC(_ctx);
    if (fd < 0 || fd >= WASM_MAX_FDS || proc->fds[fd].type != FD_FILE)
        m3ApiReturn(-L_EBADF);

    uint8_t *dirp = wali_get_mem(runtime, dirp_off, count);
    if (!dirp)
        m3ApiReturn(-L_EFAULT);

    wasm_fd_t *f = &proc->fds[fd];
    if (f->file.pos != 0)
        m3ApiReturn(0);

    int nentries = 0;
    char **entries = vfs_list_directory(f->file.parent_cluster, &nentries);
    if (!entries)
        m3ApiReturn(-L_ENOTDIR);

    int pos = 0;
    for (int i = 0; i < nentries; i++) {
        int name_len = strlen(entries[i]);
        int reclen = (int)(sizeof(uint64_t) + sizeof(int64_t) + sizeof(uint16_t) +
                           sizeof(uint8_t) + name_len + 1);
        reclen = (reclen + 7) & ~7;

        if (pos + reclen > count) {
            for (int j = i; j < nentries; j++)
                free(entries[j]);
            free(entries);
            if (pos == 0)
                m3ApiReturn(-L_EINVAL);
            f->file.pos = 1;
            m3ApiReturn(pos);
        }

        linux_dirent64_t *ent = (linux_dirent64_t *)(dirp + pos);
        ent->d_ino = i + 1;
        ent->d_off = i + 1;
        ent->d_reclen = reclen;
        ent->d_type = L_DT_UNKNOWN;
        memcpy(ent->d_name, entries[i], name_len + 1);
        free(entries[i]);
        pos += reclen;
    }
    free(entries);

    f->file.pos = 1;
    m3ApiReturn(pos);
}

m3ApiRawFunction(wali_sys_unlink)
{
    m3ApiReturnType(int64_t)
    m3ApiGetArg(int32_t, pathname_off)

    wasm_process_t *proc = WALI_PROC(_ctx);
    const char *pathname = wali_get_string(runtime, pathname_off);
    if (!pathname)
        m3ApiReturn(-L_EFAULT);

    char pathbuf[256];
    if (!wali_resolve_path(proc, pathname, pathbuf, sizeof(pathbuf)))
        m3ApiReturn(-L_ENAMETOOLONG);

    uint32_t parent_cluster;
    char *filename = NULL;
    if (!vfs_resolve_path(pathbuf, &parent_cluster, &filename)) {
        if (filename) free(filename);
        m3ApiReturn(-L_ENOENT);
    }
    bool ok = vfs_delete_file(parent_cluster, filename);
    free(filename);
    m3ApiReturn(ok ? 0 : -L_ENOENT);
}

m3ApiRawFunction(wali_sys_unlinkat)
{
    m3ApiReturnType(int64_t)
    m3ApiGetArg(int32_t, dirfd)
    m3ApiGetArg(int32_t, pathname_off)
    m3ApiGetArg(int32_t, flags)

    wasm_process_t *proc = WALI_PROC(_ctx);
    const char *pathname = wali_get_string(runtime, pathname_off);
    if (!pathname)
        m3ApiReturn(-L_EFAULT);
    if (dirfd != L_AT_FDCWD && pathname[0] != '/')
        m3ApiReturn(-L_ENOSYS);

    char pathbuf[256];
    if (!wali_resolve_path(proc, pathname, pathbuf, sizeof(pathbuf)))
        m3ApiReturn(-L_ENAMETOOLONG);

    uint32_t parent_cluster;
    char *filename = NULL;
    if (!vfs_resolve_path(pathbuf, &parent_cluster, &filename)) {
        if (filename) free(filename);
        m3ApiReturn(-L_ENOENT);
    }

    bool ok;
    if (flags & L_AT_REMOVEDIR)
        ok = vfs_delete_directory(parent_cluster, filename);
    else
        ok = vfs_delete_file(parent_cluster, filename);
    free(filename);
    m3ApiReturn(ok ? 0 : -L_ENOENT);
}

m3ApiRawFunction(wali_sys_rename)
{
    m3ApiReturnType(int64_t)
    m3ApiGetArg(int32_t, oldpath_off)
    m3ApiGetArg(int32_t, newpath_off)
    (void)oldpath_off; (void)newpath_off;
    wali_stub_log(SYS_RENAME);
    m3ApiReturn(-L_ENOSYS);
}

m3ApiRawFunction(wali_sys_renameat2)
{
    m3ApiReturnType(int64_t)
    m3ApiGetArg(int32_t, olddirfd)
    m3ApiGetArg(int32_t, oldpath_off)
    m3ApiGetArg(int32_t, newdirfd)
    m3ApiGetArg(int32_t, newpath_off)
    m3ApiGetArg(int32_t, flags)
    (void)olddirfd; (void)oldpath_off; (void)newdirfd;
    (void)newpath_off; (void)flags;
    wali_stub_log(SYS_RENAMEAT2);
    m3ApiReturn(-L_ENOSYS);
}

m3ApiRawFunction(wali_sys_mkdir)
{
    m3ApiReturnType(int64_t)
    m3ApiGetArg(int32_t, pathname_off)
    m3ApiGetArg(int32_t, mode)
    (void)mode;

    wasm_process_t *proc = WALI_PROC(_ctx);
    const char *pathname = wali_get_string(runtime, pathname_off);
    if (!pathname)
        m3ApiReturn(-L_EFAULT);

    char pathbuf[256];
    if (!wali_resolve_path(proc, pathname, pathbuf, sizeof(pathbuf)))
        m3ApiReturn(-L_ENAMETOOLONG);

    uint32_t parent_cluster;
    char *dirname = NULL;
    if (!vfs_resolve_path(pathbuf, &parent_cluster, &dirname)) {
        if (dirname) free(dirname);
        m3ApiReturn(-L_ENOENT);
    }
    bool ok = vfs_create_directory(parent_cluster, dirname);
    free(dirname);
    m3ApiReturn(ok ? 0 : -L_EEXIST);
}

m3ApiRawFunction(wali_sys_mkdirat)
{
    m3ApiReturnType(int64_t)
    m3ApiGetArg(int32_t, dirfd)
    m3ApiGetArg(int32_t, pathname_off)
    m3ApiGetArg(int32_t, mode)
    (void)mode;

    wasm_process_t *proc = WALI_PROC(_ctx);
    const char *pathname = wali_get_string(runtime, pathname_off);
    if (!pathname)
        m3ApiReturn(-L_EFAULT);
    if (dirfd != L_AT_FDCWD && pathname[0] != '/')
        m3ApiReturn(-L_ENOSYS);

    char pathbuf[256];
    if (!wali_resolve_path(proc, pathname, pathbuf, sizeof(pathbuf)))
        m3ApiReturn(-L_ENAMETOOLONG);
    uint32_t parent_cluster;
    char *dirname = NULL;
    if (!vfs_resolve_path(pathbuf, &parent_cluster, &dirname)) {
        if (dirname) free(dirname);
        m3ApiReturn(-L_ENOENT);
    }
    bool ok = vfs_create_directory(parent_cluster, dirname);
    free(dirname);
    m3ApiReturn(ok ? 0 : -L_EEXIST);
}

m3ApiRawFunction(wali_sys_rmdir)
{
    m3ApiReturnType(int64_t)
    m3ApiGetArg(int32_t, pathname_off)

    wasm_process_t *proc = WALI_PROC(_ctx);
    const char *pathname = wali_get_string(runtime, pathname_off);
    if (!pathname)
        m3ApiReturn(-L_EFAULT);

    char pathbuf[256];
    if (!wali_resolve_path(proc, pathname, pathbuf, sizeof(pathbuf)))
        m3ApiReturn(-L_ENAMETOOLONG);
    uint32_t parent_cluster;
    char *dirname = NULL;
    if (!vfs_resolve_path(pathbuf, &parent_cluster, &dirname)) {
        if (dirname) free(dirname);
        m3ApiReturn(-L_ENOENT);
    }
    bool ok = vfs_delete_directory(parent_cluster, dirname);
    free(dirname);
    m3ApiReturn(ok ? 0 : -L_ENOTEMPTY);
}

m3ApiRawFunction(wali_sys_ftruncate)
{
    m3ApiReturnType(int64_t)
    m3ApiGetArg(int32_t, fd)
    m3ApiGetArg(int64_t, length)

    wasm_process_t *proc = WALI_PROC(_ctx);
    if (fd < 0 || fd >= WASM_MAX_FDS || proc->fds[fd].type != FD_FILE)
        m3ApiReturn(-L_EBADF);
    wasm_fd_t *f = &proc->fds[fd];
    if (!f->file.writable)
        m3ApiReturn(-L_EINVAL);

    uint32_t new_size = (uint32_t)length;
    if (new_size != f->file.size) {
        uint8_t *new_data = realloc(f->file.data, new_size ? new_size : 1);
        if (!new_data && new_size)
            m3ApiReturn(-L_ENOMEM);
        if (new_size > f->file.size)
            memset(new_data + f->file.size, 0, new_size - f->file.size);
        f->file.data = new_data;
        f->file.size = new_size;
        if (f->file.pos > new_size)
            f->file.pos = new_size;
        f->file.dirty = true;
    }
    m3ApiReturn(0);
}

m3ApiRawFunction(wali_sys_fsync)
{
    m3ApiReturnType(int64_t)
    m3ApiGetArg(int32_t, fd)

    wasm_process_t *proc = WALI_PROC(_ctx);
    if (fd < 0 || fd >= WASM_MAX_FDS)
        m3ApiReturn(-L_EBADF);
    wasm_fd_t *f = &proc->fds[fd];
    if (f->type == FD_FILE && f->file.dirty && f->file.data) {
        vfs_write_file(f->file.parent_cluster, f->file.filename,
                       f->file.data, f->file.size);
        f->file.dirty = false;
    }
    m3ApiReturn(0);
}

m3ApiRawFunction(wali_sys_pread64)
{
    m3ApiReturnType(int64_t)
    m3ApiGetArg(int32_t, fd)
    m3ApiGetArg(int32_t, buf_off)
    m3ApiGetArg(uint32_t, count)
    m3ApiGetArg(int64_t, offset)

    wasm_process_t *proc = WALI_PROC(_ctx);
    if (fd < 0 || fd >= WASM_MAX_FDS || proc->fds[fd].type != FD_FILE)
        m3ApiReturn(-L_EBADF);
    uint8_t *buf = wali_get_mem(runtime, buf_off, count);
    if (!buf)
        m3ApiReturn(-L_EFAULT);

    wasm_fd_t *f = &proc->fds[fd];
    uint32_t off = (uint32_t)offset;
    if (off >= f->file.size)
        m3ApiReturn(0);
    uint32_t avail = f->file.size - off;
    if (count > avail) count = avail;
    memcpy(buf, f->file.data + off, count);
    m3ApiReturn((int64_t)count);
}

m3ApiRawFunction(wali_sys_pwrite64)
{
    m3ApiReturnType(int64_t)
    m3ApiGetArg(int32_t, fd)
    m3ApiGetArg(int32_t, buf_off)
    m3ApiGetArg(uint32_t, count)
    m3ApiGetArg(int64_t, offset)

    wasm_process_t *proc = WALI_PROC(_ctx);
    if (fd < 0 || fd >= WASM_MAX_FDS || proc->fds[fd].type != FD_FILE)
        m3ApiReturn(-L_EBADF);
    if (!proc->fds[fd].file.writable)
        m3ApiReturn(-L_EBADF);
    const uint8_t *buf = wali_get_mem(runtime, buf_off, count);
    if (!buf)
        m3ApiReturn(-L_EFAULT);

    wasm_fd_t *f = &proc->fds[fd];
    uint32_t off = (uint32_t)offset;
    uint32_t end = off + count;
    if (end > f->file.size) {
        uint8_t *new_data = realloc(f->file.data, end);
        if (!new_data) m3ApiReturn(-L_ENOMEM);
        memset(new_data + f->file.size, 0, end - f->file.size);
        f->file.data = new_data;
        f->file.size = end;
    }
    memcpy(f->file.data + off, buf, count);
    f->file.dirty = true;
    m3ApiReturn((int64_t)count);
}

m3ApiRawFunction(wali_sys_readlink)
{
    m3ApiReturnType(int64_t)
    m3ApiGetArg(int32_t, pathname_off)
    m3ApiGetArg(int32_t, buf_off)
    m3ApiGetArg(uint32_t, bufsiz)
    (void)pathname_off; (void)buf_off; (void)bufsiz;
    m3ApiReturn(-L_EINVAL);
}

/* ---- Phase 3: Terminal / ioctl ---- */

m3ApiRawFunction(wali_sys_ioctl)
{
    m3ApiReturnType(int64_t)
    m3ApiGetArg(int32_t, fd)
    m3ApiGetArg(int32_t, request)
    m3ApiGetArg(int32_t, argp_off)

    wasm_process_t *proc = WALI_PROC(_ctx);
    if (fd < 0 || fd >= WASM_MAX_FDS)
        m3ApiReturn(-L_EBADF);

    if (proc->fds[fd].type != FD_CONSOLE)
        m3ApiReturn(-L_ENOTTY);

    tty_t *tty = tty_get(proc->tty_id);

    switch (request) {
    case L_TCGETS: {
        linux_termios_t *t = wali_get_mem(runtime, argp_off, sizeof(linux_termios_t));
        if (!t) m3ApiReturn(-L_EFAULT);
        memset(t, 0, sizeof(linux_termios_t));
        t->c_iflag = proc->c_iflag;
        t->c_oflag = proc->c_oflag;
        t->c_cflag = proc->c_cflag;
        t->c_lflag = proc->c_lflag;
        memcpy(t->c_cc, proc->c_cc, sizeof(proc->c_cc));
        t->c_ispeed = 38400;
        t->c_ospeed = 38400;
        m3ApiReturn(0);
    }
    case L_TCSETS:
    case L_TCSETSW:
    case L_TCSETSF: {
        linux_termios_t *t = wali_get_mem(runtime, argp_off, sizeof(linux_termios_t));
        if (!t) m3ApiReturn(-L_EFAULT);
        proc->c_iflag = t->c_iflag;
        proc->c_oflag = t->c_oflag;
        proc->c_cflag = t->c_cflag;
        proc->c_lflag = t->c_lflag;
        memcpy(proc->c_cc, t->c_cc, sizeof(proc->c_cc));

        if (!(proc->c_lflag & L_ICANON))
            tty_set_mode(tty, TTY_MODE_RAW);
        else
            tty_set_mode(tty, TTY_MODE_COOKED);

        if (request == L_TCSETSF)
            tty_input_flush(tty);
        m3ApiReturn(0);
    }
    case L_TIOCGWINSZ: {
        linux_winsize_t *ws = wali_get_mem(runtime, argp_off, sizeof(linux_winsize_t));
        if (!ws) m3ApiReturn(-L_EFAULT);
        ws->ws_row = tty->rows;
        ws->ws_col = tty->cols;
        ws->ws_xpixel = 0;
        ws->ws_ypixel = 0;
        m3ApiReturn(0);
    }
    case L_FIONREAD: {
        int32_t *np = wali_get_mem(runtime, argp_off, 4);
        if (!np) m3ApiReturn(-L_EFAULT);
        uint16_t h = tty->input_head;
        uint16_t t = tty->input_tail;
        *np = (h >= t) ? (h - t) : (TTY_INPUT_BUF_SIZE - t + h);
        m3ApiReturn(0);
    }
    default:
        m3ApiReturn(-L_ENOTTY);
    }
}

m3ApiRawFunction(wali_sys_poll)
{
    m3ApiReturnType(int64_t)
    m3ApiGetArg(int32_t, fds_off)
    m3ApiGetArg(uint64_t, nfds)
    m3ApiGetArg(int32_t, timeout)

    wasm_process_t *proc = WALI_PROC(_ctx);
    linux_pollfd_t *pfds = wali_get_mem(runtime, fds_off,
                                        (uint32_t)(nfds * sizeof(linux_pollfd_t)));
    if (!pfds)
        m3ApiReturn(-L_EFAULT);

    uint64_t deadline = 0;
    if (timeout > 0)
        deadline = pit_ticks + (uint64_t)timeout;

    int ready;
    do {
        ready = 0;
        for (uint64_t i = 0; i < nfds; i++) {
            pfds[i].revents = 0;
            int fd = pfds[i].fd;
            if (fd < 0 || fd >= WASM_MAX_FDS) {
                pfds[i].revents = L_POLLNVAL;
                ready++;
                continue;
            }
            wasm_fd_t *f = &proc->fds[fd];
            if (f->type == FD_NONE) {
                pfds[i].revents = L_POLLNVAL;
                ready++;
                continue;
            }

            if (pfds[i].events & L_POLLIN) {
                switch (f->type) {
                case FD_CONSOLE: {
                    tty_t *tty = tty_get(proc->tty_id);
                    if (tty->input_head != tty->input_tail)
                        pfds[i].revents |= L_POLLIN;
                    break;
                }
                case FD_FILE:
                    if (f->file.pos < f->file.size)
                        pfds[i].revents |= L_POLLIN;
                    break;
                case FD_PIPE_READ: {
                    pipe_t *p = pipe_get(f->pipe.pipe_id);
                    if (p && p->head != p->tail)
                        pfds[i].revents |= L_POLLIN;
                    else if (p && p->write_refs == 0)
                        pfds[i].revents |= L_POLLHUP;
                    break;
                }
                default:
                    break;
                }
            }
            if (pfds[i].events & L_POLLOUT) {
                if (f->type == FD_CONSOLE || f->type == FD_PIPE_WRITE ||
                    (f->type == FD_FILE && f->file.writable))
                    pfds[i].revents |= L_POLLOUT;
            }
            if (pfds[i].revents)
                ready++;
        }

        if (ready > 0 || timeout == 0)
            break;

        if (timeout < 0) {
            tty_t *tty = tty_get(proc->tty_id);
            waitqueue_sleep(&tty->input_wq);
        } else {
            if (pit_ticks >= deadline)
                break;
            scheduler_yield();
        }

        proc_entry_t *e = proc_get(proc->pid);
        if (e && e->killed)
            m3ApiReturn(-L_EINTR);
    } while (1);

    m3ApiReturn((int64_t)ready);
}

m3ApiRawFunction(wali_sys_ppoll)
{
    m3ApiReturnType(int64_t)
    m3ApiGetArg(int32_t, fds_off)
    m3ApiGetArg(uint64_t, nfds)
    m3ApiGetArg(int32_t, tmo_off)
    m3ApiGetArg(int32_t, sigmask)
    m3ApiGetArg(uint32_t, sigsetsize)
    (void)sigmask; (void)sigsetsize;

    wasm_process_t *proc = WALI_PROC(_ctx);
    linux_pollfd_t *pfds = wali_get_mem(runtime, fds_off,
                                        (uint32_t)(nfds * sizeof(linux_pollfd_t)));
    if (!pfds)
        m3ApiReturn(-L_EFAULT);

    int32_t timeout_ms = -1;
    if (tmo_off) {
        linux_timespec_t *ts = wali_get_mem(runtime, tmo_off, sizeof(linux_timespec_t));
        if (ts)
            timeout_ms = (int32_t)(ts->tv_sec * 1000 + ts->tv_nsec / 1000000);
    }

    uint64_t deadline = 0;
    if (timeout_ms > 0)
        deadline = pit_ticks + (uint64_t)timeout_ms;

    int ready;
    do {
        ready = 0;
        for (uint64_t i = 0; i < nfds; i++) {
            pfds[i].revents = 0;
            int fd = pfds[i].fd;
            if (fd < 0 || fd >= WASM_MAX_FDS) {
                pfds[i].revents = L_POLLNVAL;
                ready++;
                continue;
            }
            wasm_fd_t *f = &proc->fds[fd];
            if (f->type == FD_NONE) {
                pfds[i].revents = L_POLLNVAL;
                ready++;
                continue;
            }
            if (pfds[i].events & L_POLLIN) {
                switch (f->type) {
                case FD_CONSOLE: {
                    tty_t *tty = tty_get(proc->tty_id);
                    if (tty->input_head != tty->input_tail)
                        pfds[i].revents |= L_POLLIN;
                    break;
                }
                case FD_FILE:
                    if (f->file.pos < f->file.size)
                        pfds[i].revents |= L_POLLIN;
                    break;
                case FD_PIPE_READ: {
                    pipe_t *p = pipe_get(f->pipe.pipe_id);
                    if (p && p->head != p->tail)
                        pfds[i].revents |= L_POLLIN;
                    else if (p && p->write_refs == 0)
                        pfds[i].revents |= L_POLLHUP;
                    break;
                }
                default:
                    break;
                }
            }
            if (pfds[i].events & L_POLLOUT) {
                if (f->type == FD_CONSOLE || f->type == FD_PIPE_WRITE ||
                    (f->type == FD_FILE && f->file.writable))
                    pfds[i].revents |= L_POLLOUT;
            }
            if (pfds[i].revents)
                ready++;
        }

        if (ready > 0 || timeout_ms == 0)
            break;

        if (timeout_ms < 0) {
            tty_t *tty = tty_get(proc->tty_id);
            waitqueue_sleep(&tty->input_wq);
        } else {
            if (pit_ticks >= deadline)
                break;
            scheduler_yield();
        }

        proc_entry_t *e = proc_get(proc->pid);
        if (e && e->killed)
            m3ApiReturn(-L_EINTR);
    } while (1);

    m3ApiReturn((int64_t)ready);
}

/* ---- Phase 4: Time, identity, misc ---- */

m3ApiRawFunction(wali_sys_clock_gettime)
{
    m3ApiReturnType(int64_t)
    m3ApiGetArg(int32_t, clockid)
    m3ApiGetArg(int32_t, tp_off)

    linux_timespec_t *tp = wali_get_mem(runtime, tp_off, sizeof(linux_timespec_t));
    if (!tp)
        m3ApiReturn(-L_EFAULT);

    uint64_t ticks = pit_ticks;
    if (clockid == L_CLOCK_MONOTONIC || clockid == L_CLOCK_REALTIME) {
        tp->tv_sec = ticks / 1000;
        tp->tv_nsec = (ticks % 1000) * 1000000;
    } else {
        tp->tv_sec = ticks / 1000;
        tp->tv_nsec = (ticks % 1000) * 1000000;
    }
    m3ApiReturn(0);
}

m3ApiRawFunction(wali_sys_clock_getres)
{
    m3ApiReturnType(int64_t)
    m3ApiGetArg(int32_t, clockid)
    m3ApiGetArg(int32_t, res_off)
    (void)clockid;

    if (res_off) {
        linux_timespec_t *res = wali_get_mem(runtime, res_off, sizeof(linux_timespec_t));
        if (res) {
            res->tv_sec = 0;
            res->tv_nsec = 1000000;
        }
    }
    m3ApiReturn(0);
}

m3ApiRawFunction(wali_sys_clock_nanosleep)
{
    m3ApiReturnType(int64_t)
    m3ApiGetArg(int32_t, clockid)
    m3ApiGetArg(int32_t, flags)
    m3ApiGetArg(int32_t, request_off)
    m3ApiGetArg(int32_t, remain_off)
    (void)clockid; (void)flags; (void)remain_off;

    linux_timespec_t *req = wali_get_mem(runtime, request_off, sizeof(linux_timespec_t));
    if (!req)
        m3ApiReturn(-L_EFAULT);

    uint64_t ms = req->tv_sec * 1000 + req->tv_nsec / 1000000;
    uint64_t target = pit_ticks + ms;
    while (pit_ticks < target)
        scheduler_yield();
    m3ApiReturn(0);
}

m3ApiRawFunction(wali_sys_nanosleep)
{
    m3ApiReturnType(int64_t)
    m3ApiGetArg(int32_t, req_off)
    m3ApiGetArg(int32_t, rem_off)
    (void)rem_off;

    linux_timespec_t *req = wali_get_mem(runtime, req_off, sizeof(linux_timespec_t));
    if (!req)
        m3ApiReturn(-L_EFAULT);

    uint64_t ms = req->tv_sec * 1000 + req->tv_nsec / 1000000;
    uint64_t target = pit_ticks + ms;
    while (pit_ticks < target)
        scheduler_yield();
    m3ApiReturn(0);
}

m3ApiRawFunction(wali_sys_gettimeofday)
{
    m3ApiReturnType(int64_t)
    m3ApiGetArg(int32_t, tv_off)
    m3ApiGetArg(int32_t, tz_off)
    (void)tz_off;

    if (tv_off) {
        linux_timeval_t *tv = wali_get_mem(runtime, tv_off, sizeof(linux_timeval_t));
        if (tv) {
            uint64_t ticks = pit_ticks;
            tv->tv_sec = ticks / 1000;
            tv->tv_usec = (ticks % 1000) * 1000;
        }
    }
    m3ApiReturn(0);
}

m3ApiRawFunction(wali_sys_uname)
{
    m3ApiReturnType(int64_t)
    m3ApiGetArg(int32_t, buf_off)

    linux_utsname_t *u = wali_get_mem(runtime, buf_off, sizeof(linux_utsname_t));
    if (!u)
        m3ApiReturn(-L_EFAULT);

    memset(u, 0, sizeof(linux_utsname_t));
    strncpy(u->sysname, "Linux", 64);
    strncpy(u->nodename, "wali", 64);
    strncpy(u->release, "6.1.0-wali", 64);
    strncpy(u->version, BUILD_VERSION, 64);
    strncpy(u->machine, "x86_64", 64);
    m3ApiReturn(0);
}

m3ApiRawFunction(wali_sys_getrandom)
{
    m3ApiReturnType(int64_t)
    m3ApiGetArg(int32_t, buf_off)
    m3ApiGetArg(uint32_t, buflen)
    m3ApiGetArg(int32_t, flags)
    (void)flags;

    uint8_t *buf = wali_get_mem(runtime, buf_off, buflen);
    if (!buf)
        m3ApiReturn(-L_EFAULT);

    uint64_t seed = pit_ticks;
    for (uint32_t i = 0; i < buflen; i++) {
        seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
        buf[i] = (uint8_t)(seed >> 33);
    }
    m3ApiReturn((int64_t)buflen);
}

m3ApiRawFunction(wali_sys_prlimit64)
{
    m3ApiReturnType(int64_t)
    m3ApiGetArg(int32_t, pid)
    m3ApiGetArg(int32_t, resource)
    m3ApiGetArg(int32_t, new_limit_off)
    m3ApiGetArg(int32_t, old_limit_off)
    (void)pid; (void)new_limit_off;

    if (old_limit_off) {
        linux_rlimit_t *rl = wali_get_mem(runtime, old_limit_off, sizeof(linux_rlimit_t));
        if (rl) {
            switch (resource) {
            case L_RLIMIT_NOFILE:
                rl->rlim_cur = WASM_MAX_FDS;
                rl->rlim_max = WASM_MAX_FDS;
                break;
            case L_RLIMIT_STACK:
                rl->rlim_cur = 8 * 1024 * 1024;
                rl->rlim_max = 8 * 1024 * 1024;
                break;
            default:
                rl->rlim_cur = (uint64_t)-1;
                rl->rlim_max = (uint64_t)-1;
                break;
            }
        }
    }
    m3ApiReturn(0);
}

m3ApiRawFunction(wali_sys_getrlimit)
{
    m3ApiReturnType(int64_t)
    m3ApiGetArg(int32_t, resource)
    m3ApiGetArg(int32_t, rlim_off)

    if (rlim_off) {
        linux_rlimit_t *rl = wali_get_mem(runtime, rlim_off, sizeof(linux_rlimit_t));
        if (rl) {
            switch (resource) {
            case L_RLIMIT_NOFILE:
                rl->rlim_cur = WASM_MAX_FDS;
                rl->rlim_max = WASM_MAX_FDS;
                break;
            case L_RLIMIT_STACK:
                rl->rlim_cur = 8 * 1024 * 1024;
                rl->rlim_max = 8 * 1024 * 1024;
                break;
            default:
                rl->rlim_cur = (uint64_t)-1;
                rl->rlim_max = (uint64_t)-1;
                break;
            }
        }
    }
    m3ApiReturn(0);
}

m3ApiRawFunction(wali_sys_umask)
{
    m3ApiReturnType(int64_t)
    m3ApiGetArg(int32_t, mask)
    wasm_process_t *proc = WALI_PROC(_ctx);
    int64_t old = proc->umask;
    proc->umask = mask & 0777;
    m3ApiReturn(old);
}

m3ApiRawFunction(wali_sys_flock)
{
    m3ApiReturnType(int64_t)
    m3ApiGetArg(int32_t, fd)
    m3ApiGetArg(int32_t, operation)
    (void)fd; (void)operation;
    m3ApiReturn(0);
}

m3ApiRawFunction(wali_sys_chmod)
{
    m3ApiReturnType(int64_t)
    m3ApiGetArg(int32_t, pathname_off)
    m3ApiGetArg(int32_t, mode)
    (void)pathname_off; (void)mode;
    m3ApiReturn(0);
}

m3ApiRawFunction(wali_sys_fchmod)
{
    m3ApiReturnType(int64_t)
    m3ApiGetArg(int32_t, fd)
    m3ApiGetArg(int32_t, mode)
    (void)fd; (void)mode;
    m3ApiReturn(0);
}

m3ApiRawFunction(wali_sys_kill)
{
    m3ApiReturnType(int64_t)
    m3ApiGetArg(int32_t, pid)
    m3ApiGetArg(int32_t, sig)
    (void)sig;

    proc_entry_t *e = proc_get(pid);
    if (!e)
        m3ApiReturn(-L_ESRCH);
    e->killed = true;
    m3ApiReturn(0);
}

m3ApiRawFunction(wali_sys_futex)
{
    m3ApiReturnType(int64_t)
    m3ApiGetArg(int32_t, uaddr)
    m3ApiGetArg(int32_t, futex_op)
    m3ApiGetArg(int32_t, val)
    m3ApiGetArg(int32_t, timeout)
    m3ApiGetArg(int32_t, uaddr2)
    m3ApiGetArg(int32_t, val3)
    (void)uaddr; (void)futex_op; (void)val;
    (void)timeout; (void)uaddr2; (void)val3;
    m3ApiReturn(-L_ENOSYS);
}

m3ApiRawFunction(wali_sys_setuid)
{
    m3ApiReturnType(int64_t)
    m3ApiGetArg(int32_t, uid)
    (void)uid;
    m3ApiReturn(0);
}

m3ApiRawFunction(wali_sys_setgid)
{
    m3ApiReturnType(int64_t)
    m3ApiGetArg(int32_t, gid)
    (void)gid;
    m3ApiReturn(0);
}

m3ApiRawFunction(wali_sys_prctl)
{
    m3ApiReturnType(int64_t)
    m3ApiGetArg(int32_t, option)
    m3ApiGetArg(uint64_t, arg2)
    m3ApiGetArg(uint64_t, arg3)
    m3ApiGetArg(uint64_t, arg4)
    m3ApiGetArg(uint64_t, arg5)
    (void)option; (void)arg2; (void)arg3; (void)arg4; (void)arg5;
    m3ApiReturn(0);
}

m3ApiRawFunction(wali_sys_fadvise)
{
    m3ApiReturnType(int64_t)
    m3ApiGetArg(int32_t, fd)
    m3ApiGetArg(int64_t, offset)
    m3ApiGetArg(int64_t, len)
    m3ApiGetArg(int32_t, advice)
    (void)fd; (void)offset; (void)len; (void)advice;
    m3ApiReturn(0);
}

m3ApiRawFunction(wali_sys_statx)
{
    m3ApiReturnType(int64_t)
    m3ApiGetArg(int32_t, dirfd)
    m3ApiGetArg(int32_t, pathname_off)
    m3ApiGetArg(int32_t, flags)
    m3ApiGetArg(uint32_t, mask)
    m3ApiGetArg(int32_t, statxbuf_off)
    (void)dirfd; (void)pathname_off; (void)flags; (void)mask; (void)statxbuf_off;
    wali_stub_log(SYS_STATX);
    m3ApiReturn(-L_ENOSYS);
}

/* Default stub for any unlinked syscall */
m3ApiRawFunction(wali_sys_stub)
{
    m3ApiReturnType(int64_t)
    wali_stub_log(-1);
    m3ApiReturn(-L_ENOSYS);
}

/* ---- Link all WALI syscalls ---- */

#define WALI_LINK(name, sig, func) \
    m3_LinkRawFunctionEx(module, "wali", name, sig, func, proc)

void wali_link_api(IM3Module module, IM3Runtime runtime, wasm_process_t *proc)
{
    (void)runtime;

    /* Aux functions */
    WALI_LINK("__init",           "i()",   &wali_init);
    WALI_LINK("__deinit",         "i()",   &wali_deinit);
    WALI_LINK("__proc_exit",      "v(i)",  &wali_proc_exit);
    WALI_LINK("__cl_get_argc",    "i()",   &wali_cl_get_argc);
    WALI_LINK("__cl_get_argv_len","i(i)",  &wali_cl_get_argv_len);
    WALI_LINK("__cl_copy_argv",   "i(*i)", &wali_cl_copy_argv);
    WALI_LINK("__get_init_envfile","i(*i)", &wali_get_init_envfile);
    WALI_LINK("sigsetjmp",        "i(ii)", &wali_sigsetjmp);
    WALI_LINK("setjmp",           "i(i)",  &wali_setjmp);
    WALI_LINK("longjmp",          "v(ii)", &wali_longjmp);
    WALI_LINK("__wasm_thread_spawn","i(ii)",&wali_thread_spawn);

    /* Syscalls - all return I (int64) */
    WALI_LINK("SYS_read",           "I(iii)",    &wali_sys_read);
    WALI_LINK("SYS_write",          "I(iii)",    &wali_sys_write);
    WALI_LINK("SYS_open",           "I(iii)",    &wali_sys_open);
    WALI_LINK("SYS_close",          "I(i)",      &wali_sys_close);
    WALI_LINK("SYS_stat",           "I(ii)",     &wali_sys_stat);
    WALI_LINK("SYS_fstat",          "I(ii)",     &wali_sys_fstat);
    WALI_LINK("SYS_lstat",          "I(ii)",     &wali_sys_lstat);
    WALI_LINK("SYS_poll",           "I(iIi)",    &wali_sys_poll);
    WALI_LINK("SYS_lseek",          "I(iIi)",    &wali_sys_lseek);
    WALI_LINK("SYS_mmap",           "I(iiiiiI)", &wali_sys_mmap);
    WALI_LINK("SYS_mprotect",       "I(iii)",    &wali_sys_mprotect);
    WALI_LINK("SYS_munmap",         "I(ii)",     &wali_sys_munmap);
    WALI_LINK("SYS_brk",            "I(i)",      &wali_sys_brk);
    WALI_LINK("SYS_rt_sigaction",   "I(iiii)",   &wali_sys_rt_sigaction);
    WALI_LINK("SYS_rt_sigprocmask", "I(iiii)",   &wali_sys_rt_sigprocmask);
    WALI_LINK("SYS_ioctl",          "I(iii)",    &wali_sys_ioctl);
    WALI_LINK("SYS_pread64",        "I(iiiI)",   &wali_sys_pread64);
    WALI_LINK("SYS_pwrite64",       "I(iiiI)",   &wali_sys_pwrite64);
    WALI_LINK("SYS_readv",          "I(iii)",    &wali_sys_readv);
    WALI_LINK("SYS_writev",         "I(iii)",    &wali_sys_writev);
    WALI_LINK("SYS_access",         "I(ii)",     &wali_sys_access);
    WALI_LINK("SYS_pipe",           "I(i)",      &wali_sys_pipe);
    WALI_LINK("SYS_sched_yield",    "I()",       &wali_sys_sched_yield);
    WALI_LINK("SYS_madvise",        "I(iii)",    &wali_sys_madvise);
    WALI_LINK("SYS_dup",            "I(i)",      &wali_sys_dup);
    WALI_LINK("SYS_dup2",           "I(ii)",     &wali_sys_dup2);
    WALI_LINK("SYS_nanosleep",      "I(ii)",     &wali_sys_nanosleep);
    WALI_LINK("SYS_getpid",         "I()",       &wali_sys_getpid);
    WALI_LINK("SYS_exit",           "I(i)",      &wali_sys_exit);
    WALI_LINK("SYS_kill",           "I(ii)",     &wali_sys_kill);
    WALI_LINK("SYS_uname",          "I(i)",      &wali_sys_uname);
    WALI_LINK("SYS_fcntl",          "I(iiI)",    &wali_sys_fcntl);
    WALI_LINK("SYS_flock",          "I(ii)",     &wali_sys_flock);
    WALI_LINK("SYS_fsync",          "I(i)",      &wali_sys_fsync);
    WALI_LINK("SYS_fdatasync",      "I(i)",      &wali_sys_fsync);
    WALI_LINK("SYS_ftruncate",      "I(iI)",     &wali_sys_ftruncate);
    WALI_LINK("SYS_getcwd",         "I(ii)",     &wali_sys_getcwd);
    WALI_LINK("SYS_chdir",          "I(i)",      &wali_sys_chdir);
    WALI_LINK("SYS_rename",         "I(ii)",     &wali_sys_rename);
    WALI_LINK("SYS_mkdir",          "I(ii)",     &wali_sys_mkdir);
    WALI_LINK("SYS_rmdir",          "I(i)",      &wali_sys_rmdir);
    WALI_LINK("SYS_unlink",         "I(i)",      &wali_sys_unlink);
    WALI_LINK("SYS_readlink",       "I(iii)",    &wali_sys_readlink);
    WALI_LINK("SYS_chmod",          "I(ii)",     &wali_sys_chmod);
    WALI_LINK("SYS_fchmod",         "I(ii)",     &wali_sys_fchmod);
    WALI_LINK("SYS_umask",          "I(i)",      &wali_sys_umask);
    WALI_LINK("SYS_gettimeofday",   "I(ii)",     &wali_sys_gettimeofday);
    WALI_LINK("SYS_getrlimit",      "I(ii)",     &wali_sys_getrlimit);
    WALI_LINK("SYS_getuid",         "I()",       &wali_sys_getuid);
    WALI_LINK("SYS_getgid",         "I()",       &wali_sys_getgid);
    WALI_LINK("SYS_setuid",         "I(i)",      &wali_sys_setuid);
    WALI_LINK("SYS_setgid",         "I(i)",      &wali_sys_setgid);
    WALI_LINK("SYS_geteuid",        "I()",       &wali_sys_geteuid);
    WALI_LINK("SYS_getegid",        "I()",       &wali_sys_getegid);
    WALI_LINK("SYS_setpgid",        "I(ii)",     &wali_sys_setpgid);
    WALI_LINK("SYS_getppid",        "I()",       &wali_sys_getppid);
    WALI_LINK("SYS_setsid",         "I()",       &wali_sys_setsid);
    WALI_LINK("SYS_getpgid",        "I(i)",      &wali_sys_getpgid);
    WALI_LINK("SYS_sigaltstack",    "I(ii)",     &wali_sys_sigaltstack);
    WALI_LINK("SYS_prctl",          "I(iIIII)",  &wali_sys_prctl);
    WALI_LINK("SYS_gettid",         "I()",       &wali_sys_gettid);
    WALI_LINK("SYS_futex",          "I(iiiiii)", &wali_sys_futex);
    WALI_LINK("SYS_getdents64",     "I(iii)",    &wali_sys_getdents64);
    WALI_LINK("SYS_set_tid_address","I(i)",      &wali_sys_set_tid_address);
    WALI_LINK("SYS_fadvise",        "I(iIIi)",   &wali_sys_fadvise);
    WALI_LINK("SYS_clock_gettime",  "I(ii)",     &wali_sys_clock_gettime);
    WALI_LINK("SYS_clock_getres",   "I(ii)",     &wali_sys_clock_getres);
    WALI_LINK("SYS_clock_nanosleep","I(iiii)",   &wali_sys_clock_nanosleep);
    WALI_LINK("SYS_exit_group",     "I(i)",      &wali_sys_exit_group);
    WALI_LINK("SYS_openat",         "I(iiii)",   &wali_sys_openat);
    WALI_LINK("SYS_mkdirat",        "I(iii)",    &wali_sys_mkdirat);
    WALI_LINK("SYS_newfstatat",     "I(iiii)",   &wali_sys_newfstatat);
    WALI_LINK("SYS_unlinkat",       "I(iii)",    &wali_sys_unlinkat);
    WALI_LINK("SYS_renameat2",      "I(iiiii)",  &wali_sys_renameat2);
    WALI_LINK("SYS_faccessat",      "I(iiii)",   &wali_sys_faccessat);
    WALI_LINK("SYS_faccessat2",     "I(iiii)",   &wali_sys_faccessat);
    WALI_LINK("SYS_dup3",           "I(iii)",    &wali_sys_dup3);
    WALI_LINK("SYS_pipe2",          "I(ii)",     &wali_sys_pipe2);
    WALI_LINK("SYS_prlimit64",      "I(iiii)",   &wali_sys_prlimit64);
    WALI_LINK("SYS_ppoll",          "I(iIiii)",  &wali_sys_ppoll);
    WALI_LINK("SYS_getrandom",      "I(iii)",    &wali_sys_getrandom);
    WALI_LINK("SYS_statx",          "I(iiiii)",  &wali_sys_statx);
}
