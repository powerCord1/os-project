#include <debug.h>
#include <fs.h>
#include <heap.h>
#include <keyboard.h>
#include <pipe.h>
#include <process.h>
#include <scheduler.h>
#include <stdio.h>
#include <string.h>
#include <tty.h>
#include <wasm_api.h>
#include <wasm_runner.h>

typedef struct {
    char path[256];
    int argc;
    char argv[WASM_MAX_ARGC][WASM_MAX_ARG_LEN];
    int32_t pid;
    fd_setup_entry_t fd_setup[SPAWN_MAX_FD_SETUP];
    int fd_setup_count;
} spawn_args_t;

static void apply_fd_setup(wasm_process_t *proc, fd_setup_entry_t *setups, int count)
{
    for (int i = 0; i < count; i++) {
        int tfd = setups[i].target_fd;
        if (tfd < 0 || tfd >= WASM_MAX_FDS)
            continue;

        wasm_fd_t *f = &proc->fds[tfd];
        switch (setups[i].type) {
        case FD_SETUP_PIPE_READ:
            f->type = FD_PIPE_READ;
            f->pipe.pipe_id = setups[i].pipe_id;
            break;
        case FD_SETUP_PIPE_WRITE:
            f->type = FD_PIPE_WRITE;
            f->pipe.pipe_id = setups[i].pipe_id;
            break;
        case FD_SETUP_FILE_READ: {
            uint32_t parent_cluster;
            char *filename = NULL;
            if (!vfs_resolve_path(setups[i].path, &parent_cluster, &filename)) {
                if (filename) free(filename);
                break;
            }
            uint32_t size = 0;
            uint8_t *data = vfs_read_file(parent_cluster, filename, &size);
            if (!data) {
                free(filename);
                break;
            }
            f->type = FD_FILE;
            f->file.data = data;
            f->file.size = size;
            f->file.pos = 0;
            f->file.writable = false;
            f->file.dirty = false;
            f->file.flags = WASM_O_RDONLY;
            f->file.parent_cluster = parent_cluster;
            strncpy(f->file.filename, filename, 11);
            f->file.filename[11] = '\0';
            free(filename);
            break;
        }
        case FD_SETUP_FILE_WRITE:
        case FD_SETUP_FILE_APPEND: {
            uint32_t parent_cluster;
            char *filename = NULL;
            if (!vfs_resolve_path(setups[i].path, &parent_cluster, &filename)) {
                if (filename) free(filename);
                break;
            }
            uint32_t size = 0;
            uint8_t *data = vfs_read_file(parent_cluster, filename, &size);
            if (!data) {
                data = malloc(1);
                size = 0;
            }
            bool append = (setups[i].type == FD_SETUP_FILE_APPEND);
            f->type = FD_FILE;
            f->file.data = data;
            f->file.size = append ? size : 0;
            f->file.pos = append ? size : 0;
            f->file.writable = true;
            f->file.dirty = !append;
            f->file.flags = WASM_O_WRONLY | WASM_O_CREAT | (append ? WASM_O_APPEND : WASM_O_TRUNC);
            f->file.parent_cluster = parent_cluster;
            strncpy(f->file.filename, filename, 11);
            f->file.filename[11] = '\0';
            free(filename);
            break;
        }
        default:
            break;
        }
    }
}

void wasm_runtime_setup(void)
{
    RuntimeInitArgs args;
    memset(&args, 0, sizeof(args));
    args.mem_alloc_type = Alloc_With_Allocator;
    args.mem_alloc_option.allocator.malloc_func = (void *)malloc;
    args.mem_alloc_option.allocator.realloc_func = (void *)realloc;
    args.mem_alloc_option.allocator.free_func = (void *)free;

    if (!wasm_runtime_full_init(&args)) {
        printf("WAMR init failed\n");
        return;
    }

    wasm_register_env_natives();
    wasm_register_wali_natives();
}

static int wasm_run_module(const char *path, int argc, char **argv, int32_t pid,
                           fd_setup_entry_t *fd_setups, int fd_setup_count)
{
    vfs_mount_t *mount = vfs_get_mounted_fs();
    if (!mount) {
        printf("No filesystem mounted.\n");
        return -1;
    }

    uint32_t parent_cluster;
    char *filename = NULL;
    if (!vfs_resolve_path(path, &parent_cluster, &filename)) {
        printf("Invalid path: %s\n", path);
        if (filename)
            free(filename);
        return -1;
    }

    uint32_t size = 0;
    uint8_t *wasm_bytes = vfs_read_file(parent_cluster, filename, &size);
    free(filename);
    if (!wasm_bytes) {
        printf("Failed to read '%s'\n", path);
        return -1;
    }

    wasm_process_t *proc = wasm_process_create(argc, argv);
    proc->pid = pid;

    proc_entry_t *entry = proc_get(pid);
    if (entry)
        entry->wasm_proc = proc;

    char err[128];
    wasm_module_t module = wasm_runtime_load(wasm_bytes, size, err, sizeof(err));
    if (!module) {
        printf("Load error: %s\n", err);
        free(wasm_bytes);
        wasm_process_destroy(proc);
        return -1;
    }

    wasm_module_inst_t inst = wasm_runtime_instantiate(module, 32768, 65536, err, sizeof(err));
    if (!inst) {
        printf("Instantiate error: %s\n", err);
        wasm_runtime_unload(module);
        free(wasm_bytes);
        wasm_process_destroy(proc);
        return -1;
    }

    wasm_exec_env_t exec_env = wasm_runtime_create_exec_env(inst, 32768);
    if (!exec_env) {
        printf("Failed to create exec env\n");
        wasm_runtime_deinstantiate(inst);
        wasm_runtime_unload(module);
        free(wasm_bytes);
        wasm_process_destroy(proc);
        return -1;
    }

    wasm_runtime_set_user_data(exec_env, proc);

    if (fd_setups && fd_setup_count > 0)
        apply_fd_setup(proc, fd_setups, fd_setup_count);

    wasm_function_inst_t func = wasm_runtime_lookup_function(inst, "_start");
    if (!func)
        func = wasm_runtime_lookup_function(inst, "main");

    if (!func) {
        printf("No _start or main found\n");
        wasm_runtime_destroy_exec_env(exec_env);
        wasm_runtime_deinstantiate(inst);
        wasm_runtime_unload(module);
        free(wasm_bytes);
        wasm_process_destroy(proc);
        return -1;
    }

    bool did_attach_kbd = false;
    if (proc->fds[0].type == FD_CONSOLE) {
        tty_t *t = tty_get(proc->tty_id);
        tty_input_flush(t);
        tty_sync_from_fb(t);
        if (!t->keyboard_attached) {
            tty_attach_keyboard(t);
            did_attach_kbd = true;
        }
    }

    bool ok = wasm_runtime_call_wasm(exec_env, func, 0, NULL);

    if (proc->fds[0].type == FD_CONSOLE && did_attach_kbd) {
        tty_detach_keyboard(tty_get(proc->tty_id));
        kbd_buffer_init();
    }

    int ret = 0;
    if (!ok) {
        const char *exc = wasm_runtime_get_exception(inst);
        if (exc && strstr(exc, "wali exit")) {
            ret = proc->exit_code;
        } else {
            printf("Trap: %s\n", exc ? exc : "unknown");
            ret = -1;
        }
        wasm_runtime_clear_exception(inst);
    }

    wasm_runtime_destroy_exec_env(exec_env);
    wasm_runtime_deinstantiate(inst);
    wasm_runtime_unload(module);
    free(wasm_bytes);
    wasm_process_destroy(proc);
    return ret;
}

int wasm_run_file(const char *path, int argc, char **argv)
{
    int32_t pid = proc_alloc(0);
    if (pid < 0) {
        printf("Process table full\n");
        return -1;
    }

    proc_set_foreground(pid);
    int ret = wasm_run_module(path, argc, argv, pid, NULL, 0);

    proc_mark_exited(pid, ret);
    proc_set_foreground(0);
    proc_free(pid);
    return ret;
}

static void wasm_spawn_entry(void *arg)
{
    spawn_args_t *args = (spawn_args_t *)arg;
    int32_t pid = args->pid;

    char *argv_ptrs[WASM_MAX_ARGC];
    for (int i = 0; i < args->argc; i++)
        argv_ptrs[i] = args->argv[i];

    int ret = wasm_run_module(args->path, args->argc, argv_ptrs, pid,
                              args->fd_setup, args->fd_setup_count);

    proc_mark_exited(pid, ret);
    proc_entry_t *entry = proc_get(pid);
    if (entry) {
        if (proc_foreground_pid() == pid)
            proc_set_foreground(entry->parent_pid);
    }

    free(args);
}

static int32_t spawn_common(const char *path, int argc, char **argv,
                            int32_t parent_pid, fd_setup_entry_t *setups,
                            int setup_count, bool set_foreground)
{
    int32_t pid = proc_alloc(parent_pid);
    if (pid < 0)
        return -1;

    spawn_args_t *args = malloc(sizeof(spawn_args_t));
    strncpy(args->path, path, sizeof(args->path) - 1);
    args->path[sizeof(args->path) - 1] = '\0';
    args->argc = argc < WASM_MAX_ARGC ? argc : WASM_MAX_ARGC;
    for (int i = 0; i < args->argc; i++) {
        strncpy(args->argv[i], argv[i], WASM_MAX_ARG_LEN - 1);
        args->argv[i][WASM_MAX_ARG_LEN - 1] = '\0';
    }
    args->pid = pid;
    args->fd_setup_count = 0;
    if (setups && setup_count > 0) {
        int n = setup_count < SPAWN_MAX_FD_SETUP ? setup_count : SPAWN_MAX_FD_SETUP;
        memcpy(args->fd_setup, setups, n * sizeof(fd_setup_entry_t));
        args->fd_setup_count = n;
        for (int i = 0; i < n; i++) {
            if (setups[i].type == FD_SETUP_PIPE_READ)
                pipe_ref_read(setups[i].pipe_id);
            else if (setups[i].type == FD_SETUP_PIPE_WRITE)
                pipe_ref_write(setups[i].pipe_id);
        }
    }

    thread_t *t = thread_create(wasm_spawn_entry, args);
    proc_entry_t *entry = proc_get(pid);
    if (entry)
        entry->thread_id = t->id;
    if (set_foreground)
        proc_set_foreground(pid);

    return pid;
}

int32_t wasm_spawn(const char *path, int argc, char **argv, int32_t parent_pid)
{
    return spawn_common(path, argc, argv, parent_pid, NULL, 0, true);
}

int32_t wasm_spawn_redirected(const char *path, int argc, char **argv,
                              int32_t parent_pid, fd_setup_entry_t *setups,
                              int setup_count)
{
    return spawn_common(path, argc, argv, parent_pid, setups, setup_count, false);
}
