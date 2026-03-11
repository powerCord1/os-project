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

#include "wasm_runtime.h"

#define WALI_PROC(exec_env) ((wasm_process_t *)wasm_runtime_get_user_data(exec_env))

#define WASM_PAGE_SIZE 65536
#define MMAP_BRK_GAP (64 * WASM_PAGE_SIZE)

static void ptrace_notify_entry(wasm_exec_env_t exec_env, int32_t nr,
                                int64_t a0, int64_t a1, int64_t a2,
                                int64_t a3, int64_t a4, int64_t a5)
{
    wasm_process_t *proc = WALI_PROC(exec_env);
    proc_entry_t *e = proc_get(proc->pid);
    if (!e || !e->ptrace_syscall) return;

    e->ptrace_info.syscall_nr = nr;
    e->ptrace_info.args[0] = a0;
    e->ptrace_info.args[1] = a1;
    e->ptrace_info.args[2] = a2;
    e->ptrace_info.args[3] = a3;
    e->ptrace_info.args[4] = a4;
    e->ptrace_info.args[5] = a5;
    e->ptrace_info.at_entry = 1;

    e->state = PROC_STOPPED;
    waitqueue_wake_one(&e->ptrace_wq);
    while (e->state == PROC_STOPPED)
        waitqueue_sleep(&e->exit_wq);
}

static void ptrace_notify_exit(wasm_exec_env_t exec_env, int64_t ret)
{
    wasm_process_t *proc = WALI_PROC(exec_env);
    proc_entry_t *e = proc_get(proc->pid);
    if (!e || !e->ptrace_syscall) return;

    e->ptrace_info.ret = ret;
    e->ptrace_info.at_entry = 0;

    e->state = PROC_STOPPED;
    waitqueue_wake_one(&e->ptrace_wq);
    while (e->state == PROC_STOPPED)
        waitqueue_sleep(&e->exit_wq);
}

#define PTRACE_ENTER(env, nr, ...) ptrace_notify_entry(env, nr, __VA_ARGS__)
#define PTRACE_EXIT(env, ret)      ptrace_notify_exit(env, ret)
#define PTRACE_RETURN(env, val) do { \
    int64_t _r = (val); \
    ptrace_notify_exit(env, _r); \
    return _r; \
} while (0)

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

static const char *wali_get_string(wasm_exec_env_t exec_env, int32_t offset)
{
    wasm_module_inst_t inst = wasm_runtime_get_module_inst(exec_env);
    if (!wasm_runtime_validate_app_str_addr(inst, (uint64_t)offset))
        return NULL;
    return (const char *)wasm_runtime_addr_app_to_native(inst, (uint64_t)offset);
}

static void *wali_get_mem(wasm_exec_env_t exec_env, int32_t offset, uint32_t len)
{
    wasm_module_inst_t inst = wasm_runtime_get_module_inst(exec_env);
    if (!wasm_runtime_validate_app_addr(inst, (uint64_t)offset, (uint64_t)len))
        return NULL;
    return wasm_runtime_addr_app_to_native(inst, (uint64_t)offset);
}

static uint32_t wali_memory_size(wasm_exec_env_t exec_env)
{
    wasm_module_inst_t inst = wasm_runtime_get_module_inst(exec_env);
    WASMModuleInstance *mi = (WASMModuleInstance *)inst;
    if (mi->memory_count == 0)
        return 0;
    return mi->memories[0]->cur_page_count * WASM_PAGE_SIZE;
}

static bool wali_enlarge_memory(wasm_exec_env_t exec_env, uint32_t target_pages)
{
    wasm_module_inst_t inst = wasm_runtime_get_module_inst(exec_env);
    WASMModuleInstance *mi = (WASMModuleInstance *)inst;
    uint32_t cur = mi->memories[0]->cur_page_count;
    if (target_pages <= cur)
        return true;
    return wasm_runtime_enlarge_memory(inst, target_pages - cur);
}

static void wali_trap_exit(wasm_exec_env_t exec_env, int32_t code)
{
    wasm_process_t *proc = WALI_PROC(exec_env);
    proc->exit_code = code;
    wasm_module_inst_t inst = wasm_runtime_get_module_inst(exec_env);
    wasm_runtime_set_exception(inst, "wali exit");
}

static int32_t wali_init(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    return 0;
}

static int32_t wali_deinit(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    return 0;
}

static void wali_proc_exit(wasm_exec_env_t exec_env, int32_t code)
{
    wali_trap_exit(exec_env, code);
}

static uint32_t wali_cl_get_argc(wasm_exec_env_t exec_env)
{
    wasm_process_t *proc = WALI_PROC(exec_env);
    return (uint32_t)proc->argc;
}

static uint32_t wali_cl_get_argv_len(wasm_exec_env_t exec_env, uint32_t index)
{
    wasm_process_t *proc = WALI_PROC(exec_env);
    if ((int)index >= proc->argc)
        return 0;
    return (uint32_t)strlen(proc->argv[index]) + 1;
}

static int32_t wali_cl_copy_argv(wasm_exec_env_t exec_env, int32_t buf_off, uint32_t index)
{
    wasm_process_t *proc = WALI_PROC(exec_env);
    if ((int)index >= proc->argc)
        return -1;
    char *buf = (char *)wali_get_mem(exec_env, buf_off, 1);
    if (!buf)
        return -1;
    uint32_t len = strlen(proc->argv[index]);
    memcpy(buf, proc->argv[index], len + 1);
    return 0;
}

static int32_t wali_get_init_envfile(wasm_exec_env_t exec_env, int32_t buf_off, uint32_t bufsize)
{
    (void)exec_env;
    (void)buf_off;
    (void)bufsize;
    return 0;
}

static int32_t wali_sigsetjmp(wasm_exec_env_t exec_env, int32_t buf, int32_t savesigs)
{
    (void)exec_env; (void)buf; (void)savesigs;
    return 0;
}

static int32_t wali_setjmp(wasm_exec_env_t exec_env, int32_t buf)
{
    (void)exec_env; (void)buf;
    return 0;
}

static void wali_longjmp(wasm_exec_env_t exec_env, int32_t buf, int32_t val)
{
    (void)buf; (void)val;
    wasm_module_inst_t inst = wasm_runtime_get_module_inst(exec_env);
    wasm_runtime_set_exception(inst, "longjmp not supported");
}

static int32_t wali_thread_spawn(wasm_exec_env_t exec_env, uint32_t fn, int32_t args)
{
    (void)exec_env; (void)fn; (void)args;
    return -L_ENOSYS;
}

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

static int64_t wali_sys_brk(wasm_exec_env_t exec_env, uint32_t addr)
{

    wasm_process_t *proc = WALI_PROC(exec_env);

    if (proc->brk_addr == 0) {
        proc->brk_addr = wali_memory_size(exec_env);
        proc->mmap_top = proc->brk_addr + MMAP_BRK_GAP;
    }

    if (addr == 0)
        return (int64_t)proc->brk_addr;

    if (addr < proc->brk_addr) {
        proc->brk_addr = addr;
        return (int64_t)proc->brk_addr;
    }

    uint32_t mem_size = wali_memory_size(exec_env);
    if (addr > mem_size) {
        uint32_t pages_needed = (addr + WASM_PAGE_SIZE - 1) / WASM_PAGE_SIZE;
        if (!wali_enlarge_memory(exec_env, pages_needed))
            return (int64_t)proc->brk_addr;
    }
    proc->brk_addr = addr;
    return (int64_t)proc->brk_addr;
}

static int64_t wali_sys_mmap(wasm_exec_env_t exec_env, uint32_t addr,
                              uint32_t length, int32_t prot, int32_t flags,
                              int32_t fd, int64_t offset)
{
    (void)prot; (void)offset;
    wasm_process_t *proc = WALI_PROC(exec_env);

    if (proc->brk_addr == 0) {
        proc->brk_addr = wali_memory_size(exec_env);
        proc->mmap_top = proc->brk_addr + MMAP_BRK_GAP;
    }

    if (!(flags & L_MAP_ANONYMOUS)) {
        (void)fd; (void)addr;
        return -L_ENOSYS;
    }

    uint32_t aligned_len = (length + WASM_PAGE_SIZE - 1) & ~(WASM_PAGE_SIZE - 1);
    uint32_t alloc_addr = proc->mmap_top;

    uint32_t end = alloc_addr + aligned_len;
    uint32_t mem_size = wali_memory_size(exec_env);
    if (end > mem_size) {
        uint32_t pages_needed = (end + WASM_PAGE_SIZE - 1) / WASM_PAGE_SIZE;
        if (!wali_enlarge_memory(exec_env, pages_needed)) {
            return -L_ENOMEM;
        }
    }

    proc->mmap_top = end;

    wasm_module_inst_t inst = wasm_runtime_get_module_inst(exec_env);
    uint8_t *mem = (uint8_t *)wasm_runtime_addr_app_to_native(inst, 0);
    mem_size = wali_memory_size(exec_env);
    if (mem && alloc_addr + aligned_len <= mem_size)
        memset(mem + alloc_addr, 0, aligned_len);

    return (int64_t)alloc_addr;
}

static int64_t wali_sys_munmap(wasm_exec_env_t exec_env, uint32_t addr, uint32_t length)
{
    (void)exec_env; (void)addr; (void)length;
    return 0;
}

static int64_t wali_sys_mprotect(wasm_exec_env_t exec_env, uint32_t addr,
                                  uint32_t length, int32_t prot)
{
    (void)exec_env; (void)addr; (void)length; (void)prot;
    return 0;
}

static int64_t wali_sys_madvise(wasm_exec_env_t exec_env, uint32_t addr,
                                 uint32_t length, int32_t advice)
{
    (void)exec_env; (void)addr; (void)length; (void)advice;
    return 0;
}

static int64_t wali_sys_write(wasm_exec_env_t exec_env, int32_t fd,
                               int32_t buf_off, uint32_t count)
{

    wasm_process_t *proc = WALI_PROC(exec_env);
    const uint8_t *buf = wali_get_mem(exec_env, buf_off, count);
    if (!buf)
        return -L_EFAULT;
    return wali_do_write(proc, fd, buf, count);
}

static int64_t wali_sys_read(wasm_exec_env_t exec_env, int32_t fd,
                              int32_t buf_off, uint32_t count)
{

    wasm_process_t *proc = WALI_PROC(exec_env);
    uint8_t *buf = wali_get_mem(exec_env, buf_off, count);
    if (!buf)
        return -L_EFAULT;
    return wali_do_read(proc, fd, buf, count);
}

static int64_t wali_sys_writev(wasm_exec_env_t exec_env, int32_t fd,
                                int32_t iov_off, int32_t iovcnt)
{

    wasm_process_t *proc = WALI_PROC(exec_env);
    wasm_iovec_t *iov = wali_get_mem(exec_env, iov_off, iovcnt * sizeof(wasm_iovec_t));
    if (!iov)
        return -L_EFAULT;

    int64_t total = 0;
    for (int i = 0; i < iovcnt; i++) {
        uint8_t *buf = wali_get_mem(exec_env, iov[i].iov_base, iov[i].iov_len);
        if (!buf)
            return -L_EFAULT;
        int64_t r = wali_do_write(proc, fd, buf, iov[i].iov_len);
        if (r < 0) {
            if (total > 0)
                return total;
            return r;
        }
        total += r;
    }
    return total;
}

static int64_t wali_sys_readv(wasm_exec_env_t exec_env, int32_t fd,
                               int32_t iov_off, int32_t iovcnt)
{
    wasm_process_t *proc = WALI_PROC(exec_env);
    wasm_iovec_t *iov = wali_get_mem(exec_env, iov_off, iovcnt * sizeof(wasm_iovec_t));
    if (!iov)
        return -L_EFAULT;

    int64_t total = 0;
    for (int i = 0; i < iovcnt; i++) {
        uint8_t *buf = wali_get_mem(exec_env, iov[i].iov_base, iov[i].iov_len);
        if (!buf)
            return -L_EFAULT;
        int64_t r = wali_do_read(proc, fd, buf, iov[i].iov_len);
        if (r < 0) {
            if (total > 0)
                return total;
            return r;
        }
        total += r;
        if (r < (int64_t)iov[i].iov_len)
            break;
    }
    return total;
}

static int64_t wali_sys_exit(wasm_exec_env_t exec_env, int32_t code)
{
    wali_trap_exit(exec_env, code);
    return 0;
}

static int64_t wali_sys_exit_group(wasm_exec_env_t exec_env, int32_t code)
{
    wali_trap_exit(exec_env, code);
    return 0;
}

static int64_t wali_sys_set_tid_address(wasm_exec_env_t exec_env, int32_t tidptr)
{

    (void)tidptr;
    wasm_process_t *proc = WALI_PROC(exec_env);
    return (int64_t)proc->pid;
}

static int64_t wali_sys_rt_sigaction(wasm_exec_env_t exec_env, int32_t signum,
                                      int32_t act, int32_t oldact, uint32_t sigsetsize)
{

    (void)exec_env; (void)signum; (void)act; (void)oldact; (void)sigsetsize;
    return 0;
}

static int64_t wali_sys_rt_sigprocmask(wasm_exec_env_t exec_env, int32_t how,
                                        int32_t set, int32_t oldset, uint32_t sigsetsize)
{

    (void)exec_env; (void)how; (void)set; (void)oldset; (void)sigsetsize;
    return 0;
}

static int64_t wali_sys_sigaltstack(wasm_exec_env_t exec_env, int32_t ss, int32_t old_ss)
{
    (void)exec_env; (void)ss; (void)old_ss;
    return 0;
}

static int64_t wali_sys_getpid(wasm_exec_env_t exec_env)
{
    wasm_process_t *proc = WALI_PROC(exec_env);
    return (int64_t)proc->pid;
}

static int64_t wali_sys_gettid(wasm_exec_env_t exec_env)
{
    wasm_process_t *proc = WALI_PROC(exec_env);
    return (int64_t)proc->pid;
}

static int64_t wali_sys_getuid(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    return 0;
}

static int64_t wali_sys_getgid(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    return 0;
}

static int64_t wali_sys_geteuid(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    return 0;
}

static int64_t wali_sys_getegid(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    return 0;
}

static int64_t wali_sys_getppid(wasm_exec_env_t exec_env)
{
    wasm_process_t *proc = WALI_PROC(exec_env);
    proc_entry_t *e = proc_get(proc->pid);
    return e ? (int64_t)e->parent_pid : 0;
}

static int64_t wali_sys_getpgid(wasm_exec_env_t exec_env, int32_t pid)
{
    (void)pid;
    wasm_process_t *proc = WALI_PROC(exec_env);
    return (int64_t)proc->pid;
}

static int64_t wali_sys_setpgid(wasm_exec_env_t exec_env, int32_t pid, int32_t pgid)
{
    (void)exec_env; (void)pid; (void)pgid;
    return 0;
}

static int64_t wali_sys_setsid(wasm_exec_env_t exec_env)
{
    wasm_process_t *proc = WALI_PROC(exec_env);
    return (int64_t)proc->pid;
}

static int64_t wali_sys_sched_yield(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    scheduler_yield();
    return 0;
}

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

static int64_t wali_sys_open(wasm_exec_env_t exec_env, int32_t pathname_off,
                              int32_t flags, int32_t mode)
{

    (void)mode;
    wasm_process_t *proc = WALI_PROC(exec_env);
    const char *pathname = wali_get_string(exec_env, pathname_off);
    if (!pathname)
        return -L_EFAULT;
    return wali_do_open(proc, pathname, flags);
}

static int64_t wali_sys_openat(wasm_exec_env_t exec_env, int32_t dirfd,
                                int32_t pathname_off, int32_t flags, int32_t mode)
{

    (void)mode;
    wasm_process_t *proc = WALI_PROC(exec_env);
    const char *pathname = wali_get_string(exec_env, pathname_off);
    if (!pathname)
        return -L_EFAULT;
    if (dirfd == L_AT_FDCWD || pathname[0] == '/')
        return wali_do_open(proc, pathname, flags);
    return -L_ENOSYS;
}

static int64_t wali_sys_close(wasm_exec_env_t exec_env, int32_t fd)
{

    wasm_process_t *proc = WALI_PROC(exec_env);
    if (fd < 0 || fd >= WASM_MAX_FDS || proc->fds[fd].type == FD_NONE)
        return -L_EBADF;

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
    return 0;
}

static int64_t wali_sys_lseek(wasm_exec_env_t exec_env, int32_t fd,
                               int64_t offset, int32_t whence)
{
    wasm_process_t *proc = WALI_PROC(exec_env);
    if (fd < 0 || fd >= WASM_MAX_FDS)
        return -L_EBADF;
    wasm_fd_t *f = &proc->fds[fd];
    if (f->type == FD_CONSOLE)
        return -L_ESPIPE;
    if (f->type != FD_FILE)
        return -L_EBADF;

    int64_t new_pos;
    switch (whence) {
    case 0: new_pos = offset; break;
    case 1: new_pos = (int64_t)f->file.pos + offset; break;
    case 2: new_pos = (int64_t)f->file.size + offset; break;
    default: return -L_EINVAL;
    }
    if (new_pos < 0)
        return -L_EINVAL;
    f->file.pos = (uint32_t)new_pos;
    return new_pos;
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

static int64_t wali_sys_stat(wasm_exec_env_t exec_env, int32_t pathname_off,
                              int32_t statbuf_off)
{

    wasm_process_t *proc = WALI_PROC(exec_env);
    const char *pathname = wali_get_string(exec_env, pathname_off);
    linux_stat_t *st = wali_get_mem(exec_env, statbuf_off, sizeof(linux_stat_t));
    if (!pathname || !st)
        return -L_EFAULT;
    return wali_do_stat_path(proc, pathname, st);
}

static int64_t wali_sys_lstat(wasm_exec_env_t exec_env, int32_t pathname_off,
                               int32_t statbuf_off)
{
    wasm_process_t *proc = WALI_PROC(exec_env);
    const char *pathname = wali_get_string(exec_env, pathname_off);
    linux_stat_t *st = wali_get_mem(exec_env, statbuf_off, sizeof(linux_stat_t));
    if (!pathname || !st)
        return -L_EFAULT;
    return wali_do_stat_path(proc, pathname, st);
}

static int64_t wali_sys_fstat(wasm_exec_env_t exec_env, int32_t fd, int32_t statbuf_off)
{

    wasm_process_t *proc = WALI_PROC(exec_env);
    linux_stat_t *st = wali_get_mem(exec_env, statbuf_off, sizeof(linux_stat_t));
    if (!st)
        return -L_EFAULT;
    if (fd < 0 || fd >= WASM_MAX_FDS)
        return -L_EBADF;

    wasm_fd_t *f = &proc->fds[fd];
    switch (f->type) {
    case FD_CONSOLE:
        wali_fill_stat(st, 0, L_S_IFCHR | 0620);
        return 0;
    case FD_FILE:
        wali_fill_stat(st, f->file.size, L_S_IFREG | 0644);
        return 0;
    case FD_PIPE_READ:
    case FD_PIPE_WRITE:
        wali_fill_stat(st, 0, L_S_IFIFO | 0600);
        return 0;
    default:
        return -L_EBADF;
    }
}

static int64_t wali_sys_newfstatat(wasm_exec_env_t exec_env, int32_t dirfd,
                                    int32_t pathname_off, int32_t statbuf_off,
                                    int32_t flags)
{
    wasm_process_t *proc = WALI_PROC(exec_env);
    const char *pathname = wali_get_string(exec_env, pathname_off);
    linux_stat_t *st = wali_get_mem(exec_env, statbuf_off, sizeof(linux_stat_t));
    if (!pathname || !st)
        return -L_EFAULT;

    if (pathname[0] == '\0' && (flags & 0x1000)) {
        if (dirfd < 0 || dirfd >= WASM_MAX_FDS)
            return -L_EBADF;
        wasm_fd_t *f = &proc->fds[dirfd];
        if (f->type == FD_CONSOLE)
            wali_fill_stat(st, 0, L_S_IFCHR | 0620);
        else if (f->type == FD_FILE)
            wali_fill_stat(st, f->file.size, L_S_IFREG | 0644);
        else
            return -L_EBADF;
        return 0;
    }

    if (dirfd != L_AT_FDCWD && pathname[0] != '/')
        return -L_ENOSYS;

    return wali_do_stat_path(proc, pathname, st);
}

static int64_t wali_sys_access(wasm_exec_env_t exec_env, int32_t pathname_off,
                                int32_t mode)
{
    (void)mode;
    wasm_process_t *proc = WALI_PROC(exec_env);
    const char *pathname = wali_get_string(exec_env, pathname_off);
    if (!pathname)
        return -L_EFAULT;

    char pathbuf[256];
    if (!wali_resolve_path(proc, pathname, pathbuf, sizeof(pathbuf)))
        return -L_ENAMETOOLONG;

    uint32_t parent_cluster;
    char *filename = NULL;
    if (!vfs_resolve_path(pathbuf, &parent_cluster, &filename)) {
        if (filename) free(filename);
        return -L_ENOENT;
    }
    free(filename);
    return 0;
}

static int64_t wali_sys_faccessat(wasm_exec_env_t exec_env, int32_t dirfd,
                                   int32_t pathname_off, int32_t mode, int32_t flags)
{
    (void)mode; (void)flags;
    wasm_process_t *proc = WALI_PROC(exec_env);
    const char *pathname = wali_get_string(exec_env, pathname_off);
    if (!pathname)
        return -L_EFAULT;
    if (dirfd != L_AT_FDCWD && pathname[0] != '/')
        return -L_ENOSYS;

    char pathbuf[256];
    if (!wali_resolve_path(proc, pathname, pathbuf, sizeof(pathbuf)))
        return -L_ENAMETOOLONG;
    uint32_t parent_cluster;
    char *filename = NULL;
    if (!vfs_resolve_path(pathbuf, &parent_cluster, &filename)) {
        if (filename) free(filename);
        return -L_ENOENT;
    }
    free(filename);
    return 0;
}

static int64_t wali_sys_getcwd(wasm_exec_env_t exec_env, int32_t buf_off, uint32_t size)
{

    wasm_process_t *proc = WALI_PROC(exec_env);
    uint32_t len = strlen(proc->cwd) + 1;
    if (size < len)
        return -L_ERANGE;
    char *buf = wali_get_mem(exec_env, buf_off, len);
    if (!buf)
        return -L_EFAULT;
    memcpy(buf, proc->cwd, len);
    return (int64_t)buf_off;
}

static int64_t wali_sys_chdir(wasm_exec_env_t exec_env, int32_t path_off)
{
    wasm_process_t *proc = WALI_PROC(exec_env);
    const char *path = wali_get_string(exec_env, path_off);
    if (!path)
        return -L_EFAULT;

    char pathbuf[256];
    if (!wali_resolve_path(proc, path, pathbuf, sizeof(pathbuf)))
        return -L_ENAMETOOLONG;

    uint32_t parent_cluster;
    char *filename = NULL;
    if (!vfs_resolve_path(pathbuf, &parent_cluster, &filename)) {
        if (filename) free(filename);
        return -L_ENOENT;
    }
    free(filename);

    strncpy(proc->cwd, pathbuf, sizeof(proc->cwd) - 1);
    proc->cwd[sizeof(proc->cwd) - 1] = '\0';
    return 0;
}

static int64_t wali_sys_fcntl(wasm_exec_env_t exec_env, int32_t fd,
                               int32_t cmd, int64_t arg)
{

    wasm_process_t *proc = WALI_PROC(exec_env);
    if (fd < 0 || fd >= WASM_MAX_FDS || proc->fds[fd].type == FD_NONE)
        return -L_EBADF;

    switch (cmd) {
    case L_F_DUPFD: {
        int new_fd = wali_fd_alloc_from(proc, (int)arg);
        if (new_fd < 0)
            return -L_EMFILE;
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
        return new_fd;
    }
    case L_F_GETFD:
        return (int64_t)proc->fd_flags[fd];
    case L_F_SETFD:
        proc->fd_flags[fd] = (uint32_t)arg;
        return 0;
    case L_F_GETFL:
        if (proc->fds[fd].type == FD_FILE)
            return (int64_t)proc->fds[fd].file.flags;
        return 0;
    case L_F_SETFL:
        if (proc->fds[fd].type == FD_FILE)
            proc->fds[fd].file.flags = (uint32_t)arg;
        return 0;
    default:
        return -L_EINVAL;
    }
}

static int64_t wali_sys_dup(wasm_exec_env_t exec_env, int32_t oldfd)
{
    wasm_process_t *proc = WALI_PROC(exec_env);
    if (oldfd < 0 || oldfd >= WASM_MAX_FDS || proc->fds[oldfd].type == FD_NONE)
        return -L_EBADF;

    int newfd = wali_fd_alloc(proc);
    if (newfd < 0)
        return -L_EMFILE;

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

static int64_t wali_sys_dup2(wasm_exec_env_t exec_env, int32_t oldfd, int32_t newfd)
{
    wasm_process_t *proc = WALI_PROC(exec_env);
    return wali_do_dup2(proc, oldfd, newfd);
}

static int64_t wali_sys_dup3(wasm_exec_env_t exec_env, int32_t oldfd,
                              int32_t newfd, int32_t flags)
{
    (void)flags;
    wasm_process_t *proc = WALI_PROC(exec_env);
    if (oldfd == newfd)
        return -L_EINVAL;
    return wali_do_dup2(proc, oldfd, newfd);
}

static int64_t wali_sys_pipe(wasm_exec_env_t exec_env, int32_t pipefd_off)
{
    wasm_process_t *proc = WALI_PROC(exec_env);
    int32_t *fds = wali_get_mem(exec_env, pipefd_off, 8);
    if (!fds)
        return -L_EFAULT;

    int pipe_id = pipe_alloc();
    if (pipe_id < 0)
        return -L_EMFILE;

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
        return -L_EMFILE;
    }

    proc->fds[rfd].type = FD_PIPE_READ;
    proc->fds[rfd].pipe.pipe_id = pipe_id;
    proc->fds[wfd].type = FD_PIPE_WRITE;
    proc->fds[wfd].pipe.pipe_id = pipe_id;

    fds[0] = rfd;
    fds[1] = wfd;
    return 0;
}

static int64_t wali_sys_pipe2(wasm_exec_env_t exec_env, int32_t pipefd_off, int32_t flags)
{
    (void)flags;
    return wali_sys_pipe(exec_env, pipefd_off);
}

static int64_t wali_sys_getdents64(wasm_exec_env_t exec_env, int32_t fd,
                                    int32_t dirp_off, int32_t count)
{
    wasm_process_t *proc = WALI_PROC(exec_env);
    if (fd < 0 || fd >= WASM_MAX_FDS || proc->fds[fd].type != FD_FILE)
        return -L_EBADF;

    uint8_t *dirp = wali_get_mem(exec_env, dirp_off, count);
    if (!dirp)
        return -L_EFAULT;

    wasm_fd_t *f = &proc->fds[fd];
    if (f->file.pos != 0)
        return 0;

    int nentries = 0;
    char **entries = vfs_list_directory(f->file.parent_cluster, &nentries);
    if (!entries)
        return -L_ENOTDIR;

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
                return -L_EINVAL;
            f->file.pos = 1;
            return pos;
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
    return pos;
}

static int64_t wali_sys_unlink(wasm_exec_env_t exec_env, int32_t pathname_off)
{
    wasm_process_t *proc = WALI_PROC(exec_env);
    const char *pathname = wali_get_string(exec_env, pathname_off);
    if (!pathname)
        return -L_EFAULT;

    char pathbuf[256];
    if (!wali_resolve_path(proc, pathname, pathbuf, sizeof(pathbuf)))
        return -L_ENAMETOOLONG;

    uint32_t parent_cluster;
    char *filename = NULL;
    if (!vfs_resolve_path(pathbuf, &parent_cluster, &filename)) {
        if (filename) free(filename);
        return -L_ENOENT;
    }
    bool ok = vfs_delete_file(parent_cluster, filename);
    free(filename);
    return ok ? 0 : -L_ENOENT;
}

static int64_t wali_sys_unlinkat(wasm_exec_env_t exec_env, int32_t dirfd,
                                  int32_t pathname_off, int32_t flags)
{
    wasm_process_t *proc = WALI_PROC(exec_env);
    const char *pathname = wali_get_string(exec_env, pathname_off);
    if (!pathname)
        return -L_EFAULT;
    if (dirfd != L_AT_FDCWD && pathname[0] != '/')
        return -L_ENOSYS;

    char pathbuf[256];
    if (!wali_resolve_path(proc, pathname, pathbuf, sizeof(pathbuf)))
        return -L_ENAMETOOLONG;

    uint32_t parent_cluster;
    char *filename = NULL;
    if (!vfs_resolve_path(pathbuf, &parent_cluster, &filename)) {
        if (filename) free(filename);
        return -L_ENOENT;
    }

    bool ok;
    if (flags & L_AT_REMOVEDIR)
        ok = vfs_delete_directory(parent_cluster, filename);
    else
        ok = vfs_delete_file(parent_cluster, filename);
    free(filename);
    return ok ? 0 : -L_ENOENT;
}

static int64_t wali_sys_rename(wasm_exec_env_t exec_env, int32_t oldpath_off,
                                int32_t newpath_off)
{
    (void)exec_env; (void)oldpath_off; (void)newpath_off;
    wali_stub_log(SYS_RENAME);
    return -L_ENOSYS;
}

static int64_t wali_sys_renameat2(wasm_exec_env_t exec_env, int32_t olddirfd,
                                   int32_t oldpath_off, int32_t newdirfd,
                                   int32_t newpath_off, int32_t flags)
{
    (void)exec_env; (void)olddirfd; (void)oldpath_off;
    (void)newdirfd; (void)newpath_off; (void)flags;
    wali_stub_log(SYS_RENAMEAT2);
    return -L_ENOSYS;
}

static int64_t wali_sys_mkdir(wasm_exec_env_t exec_env, int32_t pathname_off, int32_t mode)
{
    (void)mode;
    wasm_process_t *proc = WALI_PROC(exec_env);
    const char *pathname = wali_get_string(exec_env, pathname_off);
    if (!pathname)
        return -L_EFAULT;

    char pathbuf[256];
    if (!wali_resolve_path(proc, pathname, pathbuf, sizeof(pathbuf)))
        return -L_ENAMETOOLONG;

    uint32_t parent_cluster;
    char *dirname = NULL;
    if (!vfs_resolve_path(pathbuf, &parent_cluster, &dirname)) {
        if (dirname) free(dirname);
        return -L_ENOENT;
    }
    bool ok = vfs_create_directory(parent_cluster, dirname);
    free(dirname);
    return ok ? 0 : -L_EEXIST;
}

static int64_t wali_sys_mkdirat(wasm_exec_env_t exec_env, int32_t dirfd,
                                 int32_t pathname_off, int32_t mode)
{
    (void)mode;
    wasm_process_t *proc = WALI_PROC(exec_env);
    const char *pathname = wali_get_string(exec_env, pathname_off);
    if (!pathname)
        return -L_EFAULT;
    if (dirfd != L_AT_FDCWD && pathname[0] != '/')
        return -L_ENOSYS;

    char pathbuf[256];
    if (!wali_resolve_path(proc, pathname, pathbuf, sizeof(pathbuf)))
        return -L_ENAMETOOLONG;
    uint32_t parent_cluster;
    char *dirname = NULL;
    if (!vfs_resolve_path(pathbuf, &parent_cluster, &dirname)) {
        if (dirname) free(dirname);
        return -L_ENOENT;
    }
    bool ok = vfs_create_directory(parent_cluster, dirname);
    free(dirname);
    return ok ? 0 : -L_EEXIST;
}

static int64_t wali_sys_rmdir(wasm_exec_env_t exec_env, int32_t pathname_off)
{
    wasm_process_t *proc = WALI_PROC(exec_env);
    const char *pathname = wali_get_string(exec_env, pathname_off);
    if (!pathname)
        return -L_EFAULT;

    char pathbuf[256];
    if (!wali_resolve_path(proc, pathname, pathbuf, sizeof(pathbuf)))
        return -L_ENAMETOOLONG;
    uint32_t parent_cluster;
    char *dirname = NULL;
    if (!vfs_resolve_path(pathbuf, &parent_cluster, &dirname)) {
        if (dirname) free(dirname);
        return -L_ENOENT;
    }
    bool ok = vfs_delete_directory(parent_cluster, dirname);
    free(dirname);
    return ok ? 0 : -L_ENOTEMPTY;
}

static int64_t wali_sys_ftruncate(wasm_exec_env_t exec_env, int32_t fd, int64_t length)
{
    wasm_process_t *proc = WALI_PROC(exec_env);
    if (fd < 0 || fd >= WASM_MAX_FDS || proc->fds[fd].type != FD_FILE)
        return -L_EBADF;
    wasm_fd_t *f = &proc->fds[fd];
    if (!f->file.writable)
        return -L_EINVAL;

    uint32_t new_size = (uint32_t)length;
    if (new_size != f->file.size) {
        uint8_t *new_data = realloc(f->file.data, new_size ? new_size : 1);
        if (!new_data && new_size)
            return -L_ENOMEM;
        if (new_size > f->file.size)
            memset(new_data + f->file.size, 0, new_size - f->file.size);
        f->file.data = new_data;
        f->file.size = new_size;
        if (f->file.pos > new_size)
            f->file.pos = new_size;
        f->file.dirty = true;
    }
    return 0;
}

static int64_t wali_sys_fsync(wasm_exec_env_t exec_env, int32_t fd)
{
    wasm_process_t *proc = WALI_PROC(exec_env);
    if (fd < 0 || fd >= WASM_MAX_FDS)
        return -L_EBADF;
    wasm_fd_t *f = &proc->fds[fd];
    if (f->type == FD_FILE && f->file.dirty && f->file.data) {
        vfs_write_file(f->file.parent_cluster, f->file.filename,
                       f->file.data, f->file.size);
        f->file.dirty = false;
    }
    return 0;
}

static int64_t wali_sys_pread64(wasm_exec_env_t exec_env, int32_t fd,
                                 int32_t buf_off, uint32_t count, int64_t offset)
{
    wasm_process_t *proc = WALI_PROC(exec_env);
    if (fd < 0 || fd >= WASM_MAX_FDS || proc->fds[fd].type != FD_FILE)
        return -L_EBADF;
    uint8_t *buf = wali_get_mem(exec_env, buf_off, count);
    if (!buf)
        return -L_EFAULT;

    wasm_fd_t *f = &proc->fds[fd];
    uint32_t off = (uint32_t)offset;
    if (off >= f->file.size)
        return 0;
    uint32_t avail = f->file.size - off;
    if (count > avail) count = avail;
    memcpy(buf, f->file.data + off, count);
    return (int64_t)count;
}

static int64_t wali_sys_pwrite64(wasm_exec_env_t exec_env, int32_t fd,
                                  int32_t buf_off, uint32_t count, int64_t offset)
{
    wasm_process_t *proc = WALI_PROC(exec_env);
    if (fd < 0 || fd >= WASM_MAX_FDS || proc->fds[fd].type != FD_FILE)
        return -L_EBADF;
    if (!proc->fds[fd].file.writable)
        return -L_EBADF;
    const uint8_t *buf = wali_get_mem(exec_env, buf_off, count);
    if (!buf)
        return -L_EFAULT;

    wasm_fd_t *f = &proc->fds[fd];
    uint32_t off = (uint32_t)offset;
    uint32_t end = off + count;
    if (end > f->file.size) {
        uint8_t *new_data = realloc(f->file.data, end);
        if (!new_data) return -L_ENOMEM;
        memset(new_data + f->file.size, 0, end - f->file.size);
        f->file.data = new_data;
        f->file.size = end;
    }
    memcpy(f->file.data + off, buf, count);
    f->file.dirty = true;
    return (int64_t)count;
}

static int64_t wali_sys_readlink(wasm_exec_env_t exec_env, int32_t pathname_off,
                                  int32_t buf_off, uint32_t bufsiz)
{
    (void)exec_env; (void)pathname_off; (void)buf_off; (void)bufsiz;
    return -L_EINVAL;
}

static int64_t wali_sys_ioctl(wasm_exec_env_t exec_env, int32_t fd,
                               int32_t request, int32_t argp_off)
{

    wasm_process_t *proc = WALI_PROC(exec_env);
    if (fd < 0 || fd >= WASM_MAX_FDS)
        return -L_EBADF;

    if (proc->fds[fd].type != FD_CONSOLE)
        return -L_ENOTTY;

    tty_t *tty = tty_get(proc->tty_id);

    switch (request) {
    case L_TCGETS: {
        linux_termios_t *t = wali_get_mem(exec_env, argp_off, sizeof(linux_termios_t));
        if (!t) return -L_EFAULT;
        memset(t, 0, sizeof(linux_termios_t));
        t->c_iflag = proc->c_iflag;
        t->c_oflag = proc->c_oflag;
        t->c_cflag = proc->c_cflag;
        t->c_lflag = proc->c_lflag;
        memcpy(t->c_cc, proc->c_cc, sizeof(proc->c_cc));
        t->c_ispeed = 38400;
        t->c_ospeed = 38400;
        return 0;
    }
    case L_TCSETS:
    case L_TCSETSW:
    case L_TCSETSF: {
        linux_termios_t *t = wali_get_mem(exec_env, argp_off, sizeof(linux_termios_t));
        if (!t) return -L_EFAULT;
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
        return 0;
    }
    case L_TIOCGWINSZ: {
        linux_winsize_t *ws = wali_get_mem(exec_env, argp_off, sizeof(linux_winsize_t));
        if (!ws) return -L_EFAULT;
        ws->ws_row = tty->rows;
        ws->ws_col = tty->cols;
        ws->ws_xpixel = 0;
        ws->ws_ypixel = 0;
        return 0;
    }
    case L_FIONREAD: {
        int32_t *np = wali_get_mem(exec_env, argp_off, 4);
        if (!np) return -L_EFAULT;
        uint16_t h = tty->input_head;
        uint16_t t2 = tty->input_tail;
        *np = (h >= t2) ? (h - t2) : (TTY_INPUT_BUF_SIZE - t2 + h);
        return 0;
    }
    default:
        return -L_ENOTTY;
    }
}

static int64_t wali_sys_poll(wasm_exec_env_t exec_env, int32_t fds_off,
                              uint64_t nfds, int32_t timeout)
{
    wasm_process_t *proc = WALI_PROC(exec_env);
    linux_pollfd_t *pfds = wali_get_mem(exec_env, fds_off,
                                        (uint32_t)(nfds * sizeof(linux_pollfd_t)));
    if (!pfds)
        return -L_EFAULT;

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
            return -L_EINTR;
    } while (1);

    return (int64_t)ready;
}

static int64_t wali_sys_ppoll(wasm_exec_env_t exec_env, int32_t fds_off,
                               uint64_t nfds, int32_t tmo_off,
                               int32_t sigmask, uint32_t sigsetsize)
{
    (void)sigmask; (void)sigsetsize;
    wasm_process_t *proc = WALI_PROC(exec_env);
    linux_pollfd_t *pfds = wali_get_mem(exec_env, fds_off,
                                        (uint32_t)(nfds * sizeof(linux_pollfd_t)));
    if (!pfds)
        return -L_EFAULT;

    int32_t timeout_ms = -1;
    if (tmo_off) {
        linux_timespec_t *ts = wali_get_mem(exec_env, tmo_off, sizeof(linux_timespec_t));
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
            return -L_EINTR;
    } while (1);

    return (int64_t)ready;
}

static int64_t wali_sys_clock_gettime(wasm_exec_env_t exec_env, int32_t clockid,
                                       int32_t tp_off)
{
    (void)clockid;
    linux_timespec_t *tp = wali_get_mem(exec_env, tp_off, sizeof(linux_timespec_t));
    if (!tp)
        return -L_EFAULT;

    uint64_t ticks = pit_ticks;
    tp->tv_sec = ticks / 1000;
    tp->tv_nsec = (ticks % 1000) * 1000000;
    return 0;
}

static int64_t wali_sys_clock_getres(wasm_exec_env_t exec_env, int32_t clockid,
                                      int32_t res_off)
{
    (void)clockid;
    if (res_off) {
        linux_timespec_t *res = wali_get_mem(exec_env, res_off, sizeof(linux_timespec_t));
        if (res) {
            res->tv_sec = 0;
            res->tv_nsec = 1000000;
        }
    }
    return 0;
}

static int64_t wali_sys_clock_nanosleep(wasm_exec_env_t exec_env, int32_t clockid,
                                         int32_t flags, int32_t request_off,
                                         int32_t remain_off)
{
    (void)clockid; (void)flags; (void)remain_off;
    linux_timespec_t *req = wali_get_mem(exec_env, request_off, sizeof(linux_timespec_t));
    if (!req)
        return -L_EFAULT;

    uint64_t ms = req->tv_sec * 1000 + req->tv_nsec / 1000000;
    uint64_t target = pit_ticks + ms;
    while (pit_ticks < target)
        scheduler_yield();
    return 0;
}

static int64_t wali_sys_nanosleep(wasm_exec_env_t exec_env, int32_t req_off,
                                   int32_t rem_off)
{
    (void)rem_off;
    linux_timespec_t *req = wali_get_mem(exec_env, req_off, sizeof(linux_timespec_t));
    if (!req)
        return -L_EFAULT;

    uint64_t ms = req->tv_sec * 1000 + req->tv_nsec / 1000000;
    uint64_t target = pit_ticks + ms;
    while (pit_ticks < target)
        scheduler_yield();
    return 0;
}

static int64_t wali_sys_gettimeofday(wasm_exec_env_t exec_env, int32_t tv_off,
                                      int32_t tz_off)
{
    (void)tz_off;
    if (tv_off) {
        linux_timeval_t *tv = wali_get_mem(exec_env, tv_off, sizeof(linux_timeval_t));
        if (tv) {
            uint64_t ticks = pit_ticks;
            tv->tv_sec = ticks / 1000;
            tv->tv_usec = (ticks % 1000) * 1000;
        }
    }
    return 0;
}

static int64_t wali_sys_uname(wasm_exec_env_t exec_env, int32_t buf_off)
{

    linux_utsname_t *u = wali_get_mem(exec_env, buf_off, sizeof(linux_utsname_t));
    if (!u)
        return -L_EFAULT;

    memset(u, 0, sizeof(linux_utsname_t));
    strncpy(u->sysname, "Linux", 64);
    strncpy(u->nodename, "wali", 64);
    strncpy(u->release, "6.1.0-wali", 64);
    strncpy(u->version, BUILD_VERSION, 64);
    strncpy(u->machine, "x86_64", 64);
    return 0;
}

static int64_t wali_sys_getrandom(wasm_exec_env_t exec_env, int32_t buf_off,
                                   uint32_t buflen, int32_t flags)
{

    (void)flags;
    uint8_t *buf = wali_get_mem(exec_env, buf_off, buflen);
    if (!buf)
        return -L_EFAULT;

    uint64_t seed = pit_ticks;
    for (uint32_t i = 0; i < buflen; i++) {
        seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
        buf[i] = (uint8_t)(seed >> 33);
    }
    return (int64_t)buflen;
}

static int64_t wali_sys_prlimit64(wasm_exec_env_t exec_env, int32_t pid,
                                   int32_t resource, int32_t new_limit_off,
                                   int32_t old_limit_off)
{

    (void)pid; (void)new_limit_off;
    if (old_limit_off) {
        linux_rlimit_t *rl = wali_get_mem(exec_env, old_limit_off, sizeof(linux_rlimit_t));
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
    return 0;
}

static int64_t wali_sys_getrlimit(wasm_exec_env_t exec_env, int32_t resource,
                                   int32_t rlim_off)
{
    if (rlim_off) {
        linux_rlimit_t *rl = wali_get_mem(exec_env, rlim_off, sizeof(linux_rlimit_t));
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
    return 0;
}

static int64_t wali_sys_umask(wasm_exec_env_t exec_env, int32_t mask)
{
    wasm_process_t *proc = WALI_PROC(exec_env);
    int64_t old = proc->umask;
    proc->umask = mask & 0777;
    return old;
}

static int64_t wali_sys_flock(wasm_exec_env_t exec_env, int32_t fd, int32_t operation)
{
    (void)exec_env; (void)fd; (void)operation;
    return 0;
}

static int64_t wali_sys_chmod(wasm_exec_env_t exec_env, int32_t pathname_off, int32_t mode)
{
    (void)exec_env; (void)pathname_off; (void)mode;
    return 0;
}

static int64_t wali_sys_fchmod(wasm_exec_env_t exec_env, int32_t fd, int32_t mode)
{
    (void)exec_env; (void)fd; (void)mode;
    return 0;
}

static int64_t wali_sys_kill(wasm_exec_env_t exec_env, int32_t pid, int32_t sig)
{
    (void)exec_env; (void)sig;
    proc_entry_t *e = proc_get(pid);
    if (!e)
        return -L_ESRCH;
    e->killed = true;
    return 0;
}

static int64_t wali_sys_futex(wasm_exec_env_t exec_env, int32_t uaddr,
                               int32_t futex_op, int32_t val, int32_t timeout,
                               int32_t uaddr2, int32_t val3)
{
    (void)exec_env; (void)uaddr; (void)futex_op; (void)val;
    (void)timeout; (void)uaddr2; (void)val3;
    return -L_ENOSYS;
}

static int64_t wali_sys_setuid(wasm_exec_env_t exec_env, int32_t uid)
{
    (void)exec_env; (void)uid;
    return 0;
}

static int64_t wali_sys_setgid(wasm_exec_env_t exec_env, int32_t gid)
{
    (void)exec_env; (void)gid;
    return 0;
}

static int64_t wali_sys_prctl(wasm_exec_env_t exec_env, int32_t option,
                               uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5)
{
    (void)exec_env; (void)option; (void)arg2; (void)arg3; (void)arg4; (void)arg5;
    return 0;
}

static int64_t wali_sys_fadvise(wasm_exec_env_t exec_env, int32_t fd,
                                 int64_t offset, int64_t len, int32_t advice)
{
    (void)exec_env; (void)fd; (void)offset; (void)len; (void)advice;
    return 0;
}

static int64_t wali_sys_statx(wasm_exec_env_t exec_env, int32_t dirfd,
                               int32_t pathname_off, int32_t flags,
                               uint32_t mask, int32_t statxbuf_off)
{
    (void)exec_env; (void)dirfd; (void)pathname_off; (void)flags;
    (void)mask; (void)statxbuf_off;
    wali_stub_log(SYS_STATX);
    return -L_ENOSYS;
}

static int64_t wali_sys_tkill(wasm_exec_env_t exec_env, int32_t tid, int32_t sig)
{
    (void)exec_env; (void)tid; (void)sig;
    wali_stub_log(SYS_TKILL);
    return 0;
}

static int64_t wali_sys_mremap(wasm_exec_env_t exec_env, int32_t old_addr,
                                int32_t old_size, int32_t new_size,
                                int32_t flags, int32_t new_addr)
{
    (void)exec_env; (void)old_addr; (void)old_size;
    (void)new_size; (void)flags; (void)new_addr;
    wali_stub_log(SYS_MREMAP);
    return -L_ENOSYS;
}

static int64_t wali_sys_fchdir(wasm_exec_env_t exec_env, int32_t fd)
{
    (void)exec_env; (void)fd;
    wali_stub_log(SYS_FCHDIR);
    return -L_ENOSYS;
}

static int64_t wali_sys_execve(wasm_exec_env_t exec_env, int32_t pathname,
                                int32_t argv, int32_t envp)
{
    (void)exec_env; (void)pathname; (void)argv; (void)envp;
    wali_stub_log(SYS_EXECVE);
    return -L_ENOSYS;
}

static int64_t wali_sys_wait4(wasm_exec_env_t exec_env, int32_t pid,
                               int32_t wstatus_off, int32_t options, int32_t rusage)
{
    (void)options; (void)rusage;
    if (pid <= 0)
        return -L_EINVAL;

    proc_entry_t *e = proc_get(pid);
    if (!e)
        return -L_ESRCH;

    while (e->state != PROC_STOPPED && e->state != PROC_EXITED)
        waitqueue_sleep(&e->ptrace_wq);

    if (wstatus_off) {
        int32_t *ws = wali_get_mem(exec_env, wstatus_off, 4);
        if (ws) {
            if (e->state == PROC_STOPPED)
                *ws = (5 << 8) | 0x7f;  /* SIGTRAP stop */
            else
                *ws = (e->exit_code << 8);
        }
    }
    return (int64_t)pid;
}

static int64_t wali_sys_ptrace(wasm_exec_env_t exec_env, int32_t request,
                                int32_t pid, int32_t addr, int32_t data)
{
    (void)addr;

    switch (request) {
    case PTRACE_SYSCALL: {
        proc_entry_t *e = proc_get(pid);
        if (!e) return -L_ESRCH;
        e->ptrace_syscall = true;
        if (e->state == PROC_STOPPED) {
            e->state = PROC_RUNNING;
            waitqueue_wake_all(&e->exit_wq);
        }
        return 0;
    }
    case PTRACE_CONT: {
        proc_entry_t *e = proc_get(pid);
        if (!e) return -L_ESRCH;
        e->ptrace_syscall = false;
        if (e->state == PROC_STOPPED) {
            e->state = PROC_RUNNING;
            waitqueue_wake_all(&e->exit_wq);
        }
        return 0;
    }
    case PTRACE_GETREGS: {
        proc_entry_t *e = proc_get(pid);
        if (!e) return -L_ESRCH;
        ptrace_info_t *info = wali_get_mem(exec_env, data, sizeof(ptrace_info_t));
        if (!info) return -L_EFAULT;
        *info = e->ptrace_info;
        return 0;
    }
    default:
        return -L_EINVAL;
    }
}

static int64_t wali_sys_stub(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    wali_stub_log(-1);
    return -L_ENOSYS;
}

static int64_t wali_sys_socket(wasm_exec_env_t exec_env, int32_t domain,
                                int32_t type, int32_t protocol)
{
    (void)exec_env; (void)domain; (void)type; (void)protocol;
    wali_stub_log(SYS_SOCKET);
    return -L_ENOSYS;
}

static int64_t wali_sys_connect(wasm_exec_env_t exec_env, int32_t sockfd,
                                 int32_t addr, int32_t addrlen)
{
    (void)exec_env; (void)sockfd; (void)addr; (void)addrlen;
    wali_stub_log(SYS_CONNECT);
    return -L_ENOSYS;
}

static int64_t wali_sys_sendmsg(wasm_exec_env_t exec_env, int32_t sockfd,
                                 int32_t msg, int32_t flags)
{
    (void)exec_env; (void)sockfd; (void)msg; (void)flags;
    wali_stub_log(SYS_SENDMSG);
    return -L_ENOSYS;
}

static int64_t wali_sys_symlink(wasm_exec_env_t exec_env, int32_t target,
                                 int32_t linkpath)
{
    (void)exec_env; (void)target; (void)linkpath;
    wali_stub_log(SYS_SYMLINK);
    return -L_ENOSYS;
}

static int64_t wali_sys_chown(wasm_exec_env_t exec_env, int32_t pathname,
                               int32_t owner, int32_t group)
{
    (void)exec_env; (void)pathname; (void)owner; (void)group;
    wali_stub_log(SYS_CHOWN);
    return -L_ENOSYS;
}

static int64_t wali_sys_fchown(wasm_exec_env_t exec_env, int32_t fd,
                                int32_t owner, int32_t group)
{
    (void)exec_env; (void)fd; (void)owner; (void)group;
    wali_stub_log(SYS_FCHOWN);
    return -L_ENOSYS;
}

static int64_t wali_sys_getrusage(wasm_exec_env_t exec_env, int32_t who,
                                   int32_t usage)
{
    (void)exec_env; (void)who; (void)usage;
    wali_stub_log(SYS_GETRUSAGE);
    return -L_ENOSYS;
}

static int64_t wali_sys_sysinfo(wasm_exec_env_t exec_env, int32_t info)
{
    (void)exec_env; (void)info;
    wali_stub_log(SYS_SYSINFO);
    return -L_ENOSYS;
}

static int64_t wali_sys_sched_setscheduler(wasm_exec_env_t exec_env,
                                            int32_t pid, int32_t policy,
                                            int32_t param)
{
    (void)exec_env; (void)pid; (void)policy; (void)param;
    wali_stub_log(SYS_SCHED_SETSCHEDULER);
    return -L_ENOSYS;
}

static int64_t wali_sys_sched_getaffinity(wasm_exec_env_t exec_env,
                                           int32_t pid, int32_t cpusetsize,
                                           int32_t mask)
{
    (void)exec_env; (void)pid; (void)cpusetsize; (void)mask;
    wali_stub_log(SYS_SCHED_GETAFFINITY);
    return -L_ENOSYS;
}

static int64_t wali_sys_utimes(wasm_exec_env_t exec_env, int32_t filename,
                                int32_t times)
{
    (void)exec_env; (void)filename; (void)times;
    wali_stub_log(SYS_UTIMES);
    return -L_ENOSYS;
}

static int64_t wali_sys_futimesat(wasm_exec_env_t exec_env, int32_t dirfd,
                                   int32_t pathname, int32_t times)
{
    (void)exec_env; (void)dirfd; (void)pathname; (void)times;
    wali_stub_log(SYS_FUTIMESAT);
    return -L_ENOSYS;
}

static int64_t wali_sys_set_robust_list(wasm_exec_env_t exec_env,
                                         int32_t head, int32_t len)
{
    (void)exec_env; (void)head; (void)len;
    wali_stub_log(SYS_SET_ROBUST_LIST);
    return -L_ENOSYS;
}

static int64_t wali_sys_utimensat(wasm_exec_env_t exec_env, int32_t dirfd,
                                   int32_t pathname, int32_t times,
                                   int32_t flags)
{
    (void)exec_env; (void)dirfd; (void)pathname; (void)times; (void)flags;
    wali_stub_log(SYS_UTIMENSAT);
    return -L_ENOSYS;
}

/* Traced wrappers: notify ptrace at syscall entry and exit */

#define DEF_TRACED_0(fn, nr) \
    static int64_t fn##_pt(wasm_exec_env_t e) { \
        PTRACE_ENTER(e,nr,0,0,0,0,0,0); \
        int64_t r=fn(e); PTRACE_EXIT(e,r); return r; }

#define DEF_TRACED_1(fn, nr) \
    static int64_t fn##_pt(wasm_exec_env_t e, int32_t a0) { \
        PTRACE_ENTER(e,nr,a0,0,0,0,0,0); \
        int64_t r=fn(e,a0); PTRACE_EXIT(e,r); return r; }

#define DEF_TRACED_1u(fn, nr) \
    static int64_t fn##_pt(wasm_exec_env_t e, uint32_t a0) { \
        PTRACE_ENTER(e,nr,a0,0,0,0,0,0); \
        int64_t r=fn(e,a0); PTRACE_EXIT(e,r); return r; }

#define DEF_TRACED_2(fn, nr) \
    static int64_t fn##_pt(wasm_exec_env_t e, int32_t a0, int32_t a1) { \
        PTRACE_ENTER(e,nr,a0,a1,0,0,0,0); \
        int64_t r=fn(e,a0,a1); PTRACE_EXIT(e,r); return r; }

#define DEF_TRACED_2ui(fn, nr) \
    static int64_t fn##_pt(wasm_exec_env_t e, uint32_t a0, int32_t a1) { \
        PTRACE_ENTER(e,nr,a0,a1,0,0,0,0); \
        int64_t r=fn(e,a0,a1); PTRACE_EXIT(e,r); return r; }

#define DEF_TRACED_2iu(fn, nr) \
    static int64_t fn##_pt(wasm_exec_env_t e, int32_t a0, uint32_t a1) { \
        PTRACE_ENTER(e,nr,a0,a1,0,0,0,0); \
        int64_t r=fn(e,a0,a1); PTRACE_EXIT(e,r); return r; }

#define DEF_TRACED_2uu(fn, nr) \
    static int64_t fn##_pt(wasm_exec_env_t e, uint32_t a0, uint32_t a1) { \
        PTRACE_ENTER(e,nr,a0,a1,0,0,0,0); \
        int64_t r=fn(e,a0,a1); PTRACE_EXIT(e,r); return r; }

#define DEF_TRACED_3(fn, nr) \
    static int64_t fn##_pt(wasm_exec_env_t e, int32_t a0, int32_t a1, int32_t a2) { \
        PTRACE_ENTER(e,nr,a0,a1,a2,0,0,0); \
        int64_t r=fn(e,a0,a1,a2); PTRACE_EXIT(e,r); return r; }

#define DEF_TRACED_3iiu(fn, nr) \
    static int64_t fn##_pt(wasm_exec_env_t e, int32_t a0, int32_t a1, uint32_t a2) { \
        PTRACE_ENTER(e,nr,a0,a1,a2,0,0,0); \
        int64_t r=fn(e,a0,a1,a2); PTRACE_EXIT(e,r); return r; }

#define DEF_TRACED_3iui(fn, nr) \
    static int64_t fn##_pt(wasm_exec_env_t e, int32_t a0, uint32_t a1, int32_t a2) { \
        PTRACE_ENTER(e,nr,a0,a1,a2,0,0,0); \
        int64_t r=fn(e,a0,a1,a2); PTRACE_EXIT(e,r); return r; }

#define DEF_TRACED_3_lseek(fn, nr) \
    static int64_t fn##_pt(wasm_exec_env_t e, int32_t a0, int64_t a1, int32_t a2) { \
        PTRACE_ENTER(e,nr,a0,a1,a2,0,0,0); \
        int64_t r=fn(e,a0,a1,a2); PTRACE_EXIT(e,r); return r; }

#define DEF_TRACED_3_poll(fn, nr) \
    static int64_t fn##_pt(wasm_exec_env_t e, int32_t a0, uint64_t a1, int32_t a2) { \
        PTRACE_ENTER(e,nr,a0,a1,a2,0,0,0); \
        int64_t r=fn(e,a0,a1,a2); PTRACE_EXIT(e,r); return r; }

#define DEF_TRACED_3_fcntl(fn, nr) \
    static int64_t fn##_pt(wasm_exec_env_t e, int32_t a0, int32_t a1, int64_t a2) { \
        PTRACE_ENTER(e,nr,a0,a1,a2,0,0,0); \
        int64_t r=fn(e,a0,a1,a2); PTRACE_EXIT(e,r); return r; }

#define DEF_TRACED_4(fn, nr) \
    static int64_t fn##_pt(wasm_exec_env_t e, int32_t a0, int32_t a1, int32_t a2, int32_t a3) { \
        PTRACE_ENTER(e,nr,a0,a1,a2,a3,0,0); \
        int64_t r=fn(e,a0,a1,a2,a3); PTRACE_EXIT(e,r); return r; }

#define DEF_TRACED_4_iuiI(fn, nr) \
    static int64_t fn##_pt(wasm_exec_env_t e, int32_t a0, uint32_t a1, int32_t a2, int64_t a3) { \
        PTRACE_ENTER(e,nr,a0,a1,a2,a3,0,0); \
        int64_t r=fn(e,a0,a1,a2,a3); PTRACE_EXIT(e,r); return r; }

#define DEF_TRACED_4_iiuI(fn, nr) \
    static int64_t fn##_pt(wasm_exec_env_t e, int32_t a0, int32_t a1, uint32_t a2, int64_t a3) { \
        PTRACE_ENTER(e,nr,a0,a1,a2,a3,0,0); \
        int64_t r=fn(e,a0,a1,a2,a3); PTRACE_EXIT(e,r); return r; }

#define DEF_TRACED_4_iIIi(fn, nr) \
    static int64_t fn##_pt(wasm_exec_env_t e, int32_t a0, int64_t a1, int64_t a2, int32_t a3) { \
        PTRACE_ENTER(e,nr,a0,a1,a2,a3,0,0); \
        int64_t r=fn(e,a0,a1,a2,a3); PTRACE_EXIT(e,r); return r; }

#define DEF_TRACED_4_iuiI2(fn, nr) \
    static int64_t fn##_pt(wasm_exec_env_t e, int32_t a0, uint32_t a1, int32_t a2, uint32_t a3) { \
        PTRACE_ENTER(e,nr,a0,a1,a2,a3,0,0); \
        int64_t r=fn(e,a0,a1,a2,a3); PTRACE_EXIT(e,r); return r; }

#define DEF_TRACED_5(fn, nr) \
    static int64_t fn##_pt(wasm_exec_env_t e, int32_t a0, int32_t a1, int32_t a2, int32_t a3, int32_t a4) { \
        PTRACE_ENTER(e,nr,a0,a1,a2,a3,a4,0); \
        int64_t r=fn(e,a0,a1,a2,a3,a4); PTRACE_EXIT(e,r); return r; }

#define DEF_TRACED_5_ppoll(fn, nr) \
    static int64_t fn##_pt(wasm_exec_env_t e, int32_t a0, uint64_t a1, int32_t a2, int32_t a3, uint32_t a4) { \
        PTRACE_ENTER(e,nr,a0,a1,a2,a3,a4,0); \
        int64_t r=fn(e,a0,a1,a2,a3,a4); PTRACE_EXIT(e,r); return r; }

#define DEF_TRACED_5_prctl(fn, nr) \
    static int64_t fn##_pt(wasm_exec_env_t e, int32_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4) { \
        PTRACE_ENTER(e,nr,a0,a1,a2,a3,a4,0); \
        int64_t r=fn(e,a0,a1,a2,a3,a4); PTRACE_EXIT(e,r); return r; }

#define DEF_TRACED_6(fn, nr) \
    static int64_t fn##_pt(wasm_exec_env_t e, int32_t a0, int32_t a1, int32_t a2, int32_t a3, int32_t a4, int32_t a5) { \
        PTRACE_ENTER(e,nr,a0,a1,a2,a3,a4,a5); \
        int64_t r=fn(e,a0,a1,a2,a3,a4,a5); PTRACE_EXIT(e,r); return r; }

#define DEF_TRACED_6_mmap(fn, nr) \
    static int64_t fn##_pt(wasm_exec_env_t e, uint32_t a0, uint32_t a1, int32_t a2, int32_t a3, int32_t a4, int64_t a5) { \
        PTRACE_ENTER(e,nr,a0,a1,a2,a3,a4,a5); \
        int64_t r=fn(e,a0,a1,a2,a3,a4,a5); PTRACE_EXIT(e,r); return r; }

#define DEF_TRACED_2_iI(fn, nr) \
    static int64_t fn##_pt(wasm_exec_env_t e, int32_t a0, int64_t a1) { \
        PTRACE_ENTER(e,nr,a0,a1,0,0,0,0); \
        int64_t r=fn(e,a0,a1); PTRACE_EXIT(e,r); return r; }

DEF_TRACED_3iiu(wali_sys_read, SYS_READ)
DEF_TRACED_3iiu(wali_sys_write, SYS_WRITE)
DEF_TRACED_3(wali_sys_open, SYS_OPEN)
DEF_TRACED_1(wali_sys_close, SYS_CLOSE)
DEF_TRACED_2(wali_sys_stat, SYS_STAT)
DEF_TRACED_2(wali_sys_fstat, SYS_FSTAT)
DEF_TRACED_2(wali_sys_lstat, SYS_LSTAT)
DEF_TRACED_3_poll(wali_sys_poll, SYS_POLL)
DEF_TRACED_3_lseek(wali_sys_lseek, SYS_LSEEK)
DEF_TRACED_6_mmap(wali_sys_mmap, SYS_MMAP)
DEF_TRACED_3(wali_sys_mprotect, SYS_MPROTECT)
DEF_TRACED_2uu(wali_sys_munmap, SYS_MUNMAP)
DEF_TRACED_1u(wali_sys_brk, SYS_BRK)
DEF_TRACED_4(wali_sys_rt_sigaction, SYS_RT_SIGACTION)
DEF_TRACED_4(wali_sys_rt_sigprocmask, SYS_RT_SIGPROCMASK)
DEF_TRACED_3(wali_sys_ioctl, SYS_IOCTL)
DEF_TRACED_4_iiuI(wali_sys_pread64, SYS_PREAD64)
DEF_TRACED_4_iiuI(wali_sys_pwrite64, SYS_PWRITE64)
DEF_TRACED_3(wali_sys_readv, SYS_READV)
DEF_TRACED_3(wali_sys_writev, SYS_WRITEV)
DEF_TRACED_2(wali_sys_access, SYS_ACCESS)
DEF_TRACED_1(wali_sys_pipe, SYS_PIPE)
DEF_TRACED_0(wali_sys_sched_yield, SYS_SCHED_YIELD)
DEF_TRACED_3(wali_sys_madvise, SYS_MADVISE)
DEF_TRACED_1(wali_sys_dup, SYS_DUP)
DEF_TRACED_2(wali_sys_dup2, SYS_DUP2)
DEF_TRACED_2(wali_sys_nanosleep, SYS_NANOSLEEP)
DEF_TRACED_0(wali_sys_getpid, SYS_GETPID)
DEF_TRACED_1(wali_sys_exit, SYS_EXIT)
DEF_TRACED_2(wali_sys_kill, SYS_KILL)
DEF_TRACED_1(wali_sys_uname, SYS_UNAME)
DEF_TRACED_3_fcntl(wali_sys_fcntl, SYS_FCNTL)
DEF_TRACED_2(wali_sys_flock, SYS_FLOCK)
DEF_TRACED_1(wali_sys_fsync, SYS_FSYNC)
DEF_TRACED_2_iI(wali_sys_ftruncate, SYS_FTRUNCATE)
DEF_TRACED_2iu(wali_sys_getcwd, SYS_GETCWD)
DEF_TRACED_1(wali_sys_chdir, SYS_CHDIR)
DEF_TRACED_2(wali_sys_rename, SYS_RENAME)
DEF_TRACED_2(wali_sys_mkdir, SYS_MKDIR)
DEF_TRACED_1(wali_sys_rmdir, SYS_RMDIR)
DEF_TRACED_1(wali_sys_unlink, SYS_UNLINK)
DEF_TRACED_3iui(wali_sys_readlink, SYS_READLINK)
DEF_TRACED_2(wali_sys_chmod, SYS_CHMOD)
DEF_TRACED_2(wali_sys_fchmod, SYS_FCHMOD)
DEF_TRACED_1(wali_sys_umask, SYS_UMASK)
DEF_TRACED_2(wali_sys_gettimeofday, SYS_GETTIMEOFDAY)
DEF_TRACED_2(wali_sys_getrlimit, SYS_GETRLIMIT)
DEF_TRACED_0(wali_sys_getuid, SYS_GETUID)
DEF_TRACED_0(wali_sys_getgid, SYS_GETGID)
DEF_TRACED_1(wali_sys_setuid, SYS_SETUID)
DEF_TRACED_1(wali_sys_setgid, SYS_SETGID)
DEF_TRACED_0(wali_sys_geteuid, SYS_GETEUID)
DEF_TRACED_0(wali_sys_getegid, SYS_GETEGID)
DEF_TRACED_2(wali_sys_setpgid, SYS_SETPGID)
DEF_TRACED_0(wali_sys_getppid, SYS_GETPPID)
DEF_TRACED_0(wali_sys_setsid, SYS_SETSID)
DEF_TRACED_1(wali_sys_getpgid, SYS_GETPGID)
DEF_TRACED_2(wali_sys_sigaltstack, SYS_SIGALTSTACK)
DEF_TRACED_5_prctl(wali_sys_prctl, SYS_PRCTL)
DEF_TRACED_0(wali_sys_gettid, SYS_GETTID)
DEF_TRACED_6(wali_sys_futex, SYS_FUTEX)
DEF_TRACED_3(wali_sys_getdents64, SYS_GETDENTS64)
DEF_TRACED_1(wali_sys_set_tid_address, SYS_SET_TID_ADDRESS)
DEF_TRACED_4_iIIi(wali_sys_fadvise, SYS_FADVISE)
DEF_TRACED_2(wali_sys_clock_gettime, SYS_CLOCK_GETTIME)
DEF_TRACED_2(wali_sys_clock_getres, SYS_CLOCK_GETRES)
DEF_TRACED_4(wali_sys_clock_nanosleep, SYS_CLOCK_NANOSLEEP)
DEF_TRACED_1(wali_sys_exit_group, SYS_EXIT_GROUP)
DEF_TRACED_4(wali_sys_openat, SYS_OPENAT)
DEF_TRACED_3(wali_sys_mkdirat, SYS_MKDIRAT)
DEF_TRACED_4(wali_sys_newfstatat, SYS_NEWFSTATAT)
DEF_TRACED_3(wali_sys_unlinkat, SYS_UNLINKAT)
DEF_TRACED_5(wali_sys_renameat2, SYS_RENAMEAT2)
DEF_TRACED_4(wali_sys_faccessat, SYS_FACCESSAT)
DEF_TRACED_3(wali_sys_dup3, SYS_DUP3)
DEF_TRACED_2(wali_sys_pipe2, SYS_PIPE2)
DEF_TRACED_4(wali_sys_prlimit64, SYS_PRLIMIT64)
DEF_TRACED_5_ppoll(wali_sys_ppoll, SYS_PPOLL)
DEF_TRACED_3iui(wali_sys_getrandom, SYS_GETRANDOM)
DEF_TRACED_5(wali_sys_statx, SYS_STATX)
DEF_TRACED_2(wali_sys_tkill, SYS_TKILL)
DEF_TRACED_5(wali_sys_mremap, SYS_MREMAP)
DEF_TRACED_1(wali_sys_fchdir, SYS_FCHDIR)
DEF_TRACED_3(wali_sys_execve, SYS_EXECVE)
DEF_TRACED_4(wali_sys_wait4, SYS_WAIT4)
DEF_TRACED_3(wali_sys_socket, SYS_SOCKET)
DEF_TRACED_3(wali_sys_connect, SYS_CONNECT)
DEF_TRACED_3(wali_sys_sendmsg, SYS_SENDMSG)
DEF_TRACED_2(wali_sys_symlink, SYS_SYMLINK)
DEF_TRACED_3(wali_sys_chown, SYS_CHOWN)
DEF_TRACED_3(wali_sys_fchown, SYS_FCHOWN)
DEF_TRACED_2(wali_sys_getrusage, SYS_GETRUSAGE)
DEF_TRACED_1(wali_sys_sysinfo, SYS_SYSINFO)
DEF_TRACED_3(wali_sys_sched_setscheduler, SYS_SCHED_SETSCHEDULER)
DEF_TRACED_3(wali_sys_sched_getaffinity, SYS_SCHED_GETAFFINITY)
DEF_TRACED_2(wali_sys_utimes, SYS_UTIMES)
DEF_TRACED_3(wali_sys_futimesat, SYS_FUTIMESAT)
DEF_TRACED_2(wali_sys_set_robust_list, SYS_SET_ROBUST_LIST)
DEF_TRACED_4(wali_sys_utimensat, SYS_UTIMENSAT)
DEF_TRACED_4(wali_sys_ptrace, SYS_PTRACE)

static NativeSymbol wali_symbols[] = {
    /* Aux functions */
    { "__init",             (void *)wali_init,              "()i",          NULL },
    { "__deinit",           (void *)wali_deinit,            "()i",          NULL },
    { "__proc_exit",        (void *)wali_proc_exit,         "(i)",          NULL },
    { "__cl_get_argc",      (void *)wali_cl_get_argc,       "()i",          NULL },
    { "__cl_get_argv_len",  (void *)wali_cl_get_argv_len,   "(i)i",         NULL },
    { "__cl_copy_argv",     (void *)wali_cl_copy_argv,      "(ii)i",        NULL },
    { "__get_init_envfile", (void *)wali_get_init_envfile,   "(ii)i",        NULL },
    { "sigsetjmp",          (void *)wali_sigsetjmp,         "(ii)i",        NULL },
    { "setjmp",             (void *)wali_setjmp,            "(i)i",         NULL },
    { "longjmp",            (void *)wali_longjmp,           "(ii)",         NULL },
    { "__wasm_thread_spawn",(void *)wali_thread_spawn,      "(ii)i",        NULL },

    /* Syscalls (traced via ptrace wrappers) */
    { "SYS_read",           (void *)wali_sys_read_pt,          "(iii)I",       NULL },
    { "SYS_write",          (void *)wali_sys_write_pt,         "(iii)I",       NULL },
    { "SYS_open",           (void *)wali_sys_open_pt,          "(iii)I",       NULL },
    { "SYS_close",          (void *)wali_sys_close_pt,         "(i)I",         NULL },
    { "SYS_stat",           (void *)wali_sys_stat_pt,          "(ii)I",        NULL },
    { "SYS_fstat",          (void *)wali_sys_fstat_pt,         "(ii)I",        NULL },
    { "SYS_lstat",          (void *)wali_sys_lstat_pt,         "(ii)I",        NULL },
    { "SYS_poll",           (void *)wali_sys_poll_pt,          "(iIi)I",       NULL },
    { "SYS_lseek",          (void *)wali_sys_lseek_pt,         "(iIi)I",       NULL },
    { "SYS_mmap",           (void *)wali_sys_mmap_pt,          "(iiiiiI)I",    NULL },
    { "SYS_mprotect",       (void *)wali_sys_mprotect_pt,      "(iii)I",       NULL },
    { "SYS_munmap",         (void *)wali_sys_munmap_pt,        "(ii)I",        NULL },
    { "SYS_brk",            (void *)wali_sys_brk_pt,           "(i)I",         NULL },
    { "SYS_rt_sigaction",   (void *)wali_sys_rt_sigaction_pt,  "(iiii)I",      NULL },
    { "SYS_rt_sigprocmask", (void *)wali_sys_rt_sigprocmask_pt,"(iiii)I",     NULL },
    { "SYS_ioctl",          (void *)wali_sys_ioctl_pt,         "(iii)I",       NULL },
    { "SYS_pread64",        (void *)wali_sys_pread64_pt,       "(iiiI)I",      NULL },
    { "SYS_pwrite64",       (void *)wali_sys_pwrite64_pt,      "(iiiI)I",      NULL },
    { "SYS_readv",          (void *)wali_sys_readv_pt,         "(iii)I",       NULL },
    { "SYS_writev",         (void *)wali_sys_writev_pt,        "(iii)I",       NULL },
    { "SYS_access",         (void *)wali_sys_access_pt,        "(ii)I",        NULL },
    { "SYS_pipe",           (void *)wali_sys_pipe_pt,          "(i)I",         NULL },
    { "SYS_sched_yield",    (void *)wali_sys_sched_yield_pt,   "()I",          NULL },
    { "SYS_madvise",        (void *)wali_sys_madvise_pt,       "(iii)I",       NULL },
    { "SYS_dup",            (void *)wali_sys_dup_pt,           "(i)I",         NULL },
    { "SYS_dup2",           (void *)wali_sys_dup2_pt,          "(ii)I",        NULL },
    { "SYS_nanosleep",      (void *)wali_sys_nanosleep_pt,     "(ii)I",        NULL },
    { "SYS_getpid",         (void *)wali_sys_getpid_pt,        "()I",          NULL },
    { "SYS_exit",           (void *)wali_sys_exit_pt,          "(i)I",         NULL },
    { "SYS_kill",           (void *)wali_sys_kill_pt,          "(ii)I",        NULL },
    { "SYS_uname",          (void *)wali_sys_uname_pt,         "(i)I",         NULL },
    { "SYS_fcntl",          (void *)wali_sys_fcntl_pt,         "(iiI)I",       NULL },
    { "SYS_flock",          (void *)wali_sys_flock_pt,         "(ii)I",        NULL },
    { "SYS_fsync",          (void *)wali_sys_fsync_pt,         "(i)I",         NULL },
    { "SYS_fdatasync",      (void *)wali_sys_fsync_pt,         "(i)I",         NULL },
    { "SYS_ftruncate",      (void *)wali_sys_ftruncate_pt,     "(iI)I",        NULL },
    { "SYS_getcwd",         (void *)wali_sys_getcwd_pt,        "(ii)I",        NULL },
    { "SYS_chdir",          (void *)wali_sys_chdir_pt,         "(i)I",         NULL },
    { "SYS_rename",         (void *)wali_sys_rename_pt,        "(ii)I",        NULL },
    { "SYS_mkdir",          (void *)wali_sys_mkdir_pt,         "(ii)I",        NULL },
    { "SYS_rmdir",          (void *)wali_sys_rmdir_pt,         "(i)I",         NULL },
    { "SYS_unlink",         (void *)wali_sys_unlink_pt,        "(i)I",         NULL },
    { "SYS_readlink",       (void *)wali_sys_readlink_pt,      "(iii)I",       NULL },
    { "SYS_chmod",          (void *)wali_sys_chmod_pt,         "(ii)I",        NULL },
    { "SYS_fchmod",         (void *)wali_sys_fchmod_pt,        "(ii)I",        NULL },
    { "SYS_umask",          (void *)wali_sys_umask_pt,         "(i)I",         NULL },
    { "SYS_gettimeofday",   (void *)wali_sys_gettimeofday_pt,  "(ii)I",        NULL },
    { "SYS_getrlimit",      (void *)wali_sys_getrlimit_pt,     "(ii)I",        NULL },
    { "SYS_getuid",         (void *)wali_sys_getuid_pt,        "()I",          NULL },
    { "SYS_getgid",         (void *)wali_sys_getgid_pt,        "()I",          NULL },
    { "SYS_setuid",         (void *)wali_sys_setuid_pt,        "(i)I",         NULL },
    { "SYS_setgid",         (void *)wali_sys_setgid_pt,        "(i)I",         NULL },
    { "SYS_geteuid",        (void *)wali_sys_geteuid_pt,       "()I",          NULL },
    { "SYS_getegid",        (void *)wali_sys_getegid_pt,       "()I",          NULL },
    { "SYS_setpgid",        (void *)wali_sys_setpgid_pt,       "(ii)I",        NULL },
    { "SYS_getppid",        (void *)wali_sys_getppid_pt,       "()I",          NULL },
    { "SYS_setsid",         (void *)wali_sys_setsid_pt,        "()I",          NULL },
    { "SYS_getpgid",        (void *)wali_sys_getpgid_pt,       "(i)I",         NULL },
    { "SYS_sigaltstack",    (void *)wali_sys_sigaltstack_pt,   "(ii)I",        NULL },
    { "SYS_prctl",          (void *)wali_sys_prctl_pt,         "(iIIII)I",     NULL },
    { "SYS_gettid",         (void *)wali_sys_gettid_pt,        "()I",          NULL },
    { "SYS_futex",          (void *)wali_sys_futex_pt,         "(iiiiii)I",    NULL },
    { "SYS_getdents64",     (void *)wali_sys_getdents64_pt,    "(iii)I",       NULL },
    { "SYS_set_tid_address",(void *)wali_sys_set_tid_address_pt,"(i)I",        NULL },
    { "SYS_fadvise",        (void *)wali_sys_fadvise_pt,       "(iIIi)I",      NULL },
    { "SYS_clock_gettime",  (void *)wali_sys_clock_gettime_pt, "(ii)I",        NULL },
    { "SYS_clock_getres",   (void *)wali_sys_clock_getres_pt,  "(ii)I",        NULL },
    { "SYS_clock_nanosleep",(void *)wali_sys_clock_nanosleep_pt,"(iiii)I",     NULL },
    { "SYS_exit_group",     (void *)wali_sys_exit_group_pt,    "(i)I",         NULL },
    { "SYS_openat",         (void *)wali_sys_openat_pt,        "(iiii)I",      NULL },
    { "SYS_mkdirat",        (void *)wali_sys_mkdirat_pt,       "(iii)I",       NULL },
    { "SYS_newfstatat",     (void *)wali_sys_newfstatat_pt,    "(iiii)I",      NULL },
    { "SYS_unlinkat",       (void *)wali_sys_unlinkat_pt,      "(iii)I",       NULL },
    { "SYS_renameat2",      (void *)wali_sys_renameat2_pt,     "(iiiii)I",     NULL },
    { "SYS_faccessat",      (void *)wali_sys_faccessat_pt,     "(iiii)I",      NULL },
    { "SYS_faccessat2",     (void *)wali_sys_faccessat_pt,     "(iiii)I",      NULL },
    { "SYS_dup3",           (void *)wali_sys_dup3_pt,          "(iii)I",       NULL },
    { "SYS_pipe2",          (void *)wali_sys_pipe2_pt,         "(ii)I",        NULL },
    { "SYS_prlimit64",      (void *)wali_sys_prlimit64_pt,     "(iiii)I",      NULL },
    { "SYS_ppoll",          (void *)wali_sys_ppoll_pt,         "(iIiii)I",     NULL },
    { "SYS_getrandom",      (void *)wali_sys_getrandom_pt,     "(iii)I",       NULL },
    { "SYS_statx",          (void *)wali_sys_statx_pt,         "(iiiii)I",     NULL },
    { "SYS_tkill",          (void *)wali_sys_tkill_pt,         "(ii)I",        NULL },
    { "SYS_mremap",         (void *)wali_sys_mremap_pt,        "(iiiii)I",     NULL },
    { "SYS_fchdir",         (void *)wali_sys_fchdir_pt,        "(i)I",         NULL },
    { "SYS_execve",         (void *)wali_sys_execve_pt,        "(iii)I",       NULL },
    { "SYS_wait4",          (void *)wali_sys_wait4_pt,         "(iiii)I",      NULL },
    { "SYS_ptrace",         (void *)wali_sys_ptrace_pt,        "(iiii)I",      NULL },
    { "SYS_socket",         (void *)wali_sys_socket_pt,        "(iii)I",       NULL },
    { "SYS_connect",        (void *)wali_sys_connect_pt,       "(iii)I",       NULL },
    { "SYS_sendmsg",        (void *)wali_sys_sendmsg_pt,       "(iii)I",       NULL },
    { "SYS_symlink",        (void *)wali_sys_symlink_pt,       "(ii)I",        NULL },
    { "SYS_chown",          (void *)wali_sys_chown_pt,         "(iii)I",       NULL },
    { "SYS_fchown",         (void *)wali_sys_fchown_pt,        "(iii)I",       NULL },
    { "SYS_getrusage",      (void *)wali_sys_getrusage_pt,     "(ii)I",        NULL },
    { "SYS_sysinfo",        (void *)wali_sys_sysinfo_pt,       "(i)I",         NULL },
    { "SYS_sched_setscheduler",(void *)wali_sys_sched_setscheduler_pt,"(iii)I",NULL },
    { "SYS_sched_getaffinity",(void *)wali_sys_sched_getaffinity_pt,"(iii)I",  NULL },
    { "SYS_utimes",         (void *)wali_sys_utimes_pt,        "(ii)I",        NULL },
    { "SYS_futimesat",      (void *)wali_sys_futimesat_pt,     "(iii)I",       NULL },
    { "SYS_set_robust_list",(void *)wali_sys_set_robust_list_pt,"(ii)I",       NULL },
    { "SYS_utimensat",      (void *)wali_sys_utimensat_pt,     "(iiii)I",      NULL },
};

void wasm_register_wali_natives(void)
{
    wasm_runtime_register_natives("wali", wali_symbols,
                                  sizeof(wali_symbols) / sizeof(NativeSymbol));
}
