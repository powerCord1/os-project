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

#include "wasm_runtime.h"
#include "wasm_interp.h"


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
            uint32_t size = 0;
            uint8_t *data = vfs_read(setups[i].path, &size);
            if (!data)
                break;
            f->type = FD_FILE;
            f->file.data = data;
            f->file.size = size;
            f->file.pos = 0;
            f->file.writable = false;
            f->file.dirty = false;
            f->file.flags = WASM_O_RDONLY;
            break;
        }
        case FD_SETUP_FILE_WRITE:
        case FD_SETUP_FILE_APPEND: {
            uint32_t size = 0;
            uint8_t *data = vfs_read(setups[i].path, &size);
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
    uint32_t size = 0;
    uint8_t *wasm_bytes = vfs_read(path, &size);
    if (!wasm_bytes) {
        printf("Failed to read '%s'\n", path);
        return -1;
    }

    proc_entry_t *entry = proc_get(pid);

    wasm_process_t *proc = wasm_process_create(argc, argv);
    if (!proc) {
        printf("Failed to allocate process\n");
        free(wasm_bytes);
        return -1;
    }
    proc->pid = pid;

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

    wasm_module_inst_t inst = wasm_runtime_instantiate(module, 131072, 65536, err, sizeof(err));
    if (!inst) {
        printf("Instantiate error: %s\n", err);
        wasm_runtime_unload(module);
        free(wasm_bytes);
        wasm_process_destroy(proc);

        return -1;
    }

    wasm_exec_env_t exec_env = wasm_runtime_create_exec_env(inst, 131072);
    if (!exec_env) {
        printf("Failed to create exec env\n");
        wasm_runtime_deinstantiate(inst);
        wasm_runtime_unload(module);
        free(wasm_bytes);
        wasm_process_destroy(proc);

        return -1;
    }

    wasm_runtime_set_user_data(exec_env, proc);

    /* Store runtime pointers for fork access */
    proc->wasm_module = module;
    proc->wasm_inst = inst;
    proc->wasm_exec_env = exec_env;
    proc->wasm_bytes = wasm_bytes;

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

execve_check:
    if (proc->fds[0].type == FD_CONSOLE && did_attach_kbd) {
        tty_detach_keyboard(tty_get(proc->tty_id));
        kbd_buffer_init();
        did_attach_kbd = false;
    }

    int ret = 0;
    if (!ok) {
        const char *exc = wasm_runtime_get_exception(inst);
        if (exc && strstr(exc, "wali execve")) {
            wasm_runtime_clear_exception(inst);

            /* Tear down old instance */
            wasm_runtime_destroy_exec_env(exec_env);
            wasm_runtime_deinstantiate(inst);
            wasm_runtime_unload(module);
            free(wasm_bytes);

            /* Load new module */
            wasm_bytes = proc->execve_wasm_bytes;
            size = proc->execve_wasm_size;
            proc->execve_pending = false;

            module = wasm_runtime_load(wasm_bytes, size, err, sizeof(err));
            if (!module) {
                printf("execve: load error: %s\n", err);
                free(wasm_bytes);
                wasm_process_destroy(proc);
                return -1;
            }

            inst = wasm_runtime_instantiate(module, 131072, 65536, err, sizeof(err));
            if (!inst) {
                printf("execve: instantiate error: %s\n", err);
                wasm_runtime_unload(module);
                free(wasm_bytes);
                wasm_process_destroy(proc);
                return -1;
            }

            exec_env = wasm_runtime_create_exec_env(inst, 131072);
            if (!exec_env) {
                printf("execve: exec_env error\n");
                wasm_runtime_deinstantiate(inst);
                wasm_runtime_unload(module);
                free(wasm_bytes);
                wasm_process_destroy(proc);
                return -1;
            }

            /* Reset process state */
            proc->brk_addr = 0;
            proc->mmap_top = 0;
            for (int i = 0; i < WASM_MAX_JMPBUFS; i++)
                proc->jmpbufs[i].active = false;

            proc->wasm_module = module;
            proc->wasm_inst = inst;
            proc->wasm_exec_env = exec_env;
            proc->wasm_bytes = wasm_bytes;

            wasm_runtime_set_user_data(exec_env, proc);

            func = wasm_runtime_lookup_function(inst, "_start");
            if (!func)
                func = wasm_runtime_lookup_function(inst, "main");
            if (!func) {
                printf("execve: no _start or main found\n");
                wasm_runtime_destroy_exec_env(exec_env);
                wasm_runtime_deinstantiate(inst);
                wasm_runtime_unload(module);
                free(wasm_bytes);
                wasm_process_destroy(proc);
                return -1;
            }

            /* Re-attach keyboard if stdin is console */
            if (proc->fds[0].type == FD_CONSOLE) {
                tty_t *t = tty_get(proc->tty_id);
                tty_input_flush(t);
                tty_sync_from_fb(t);
                if (!t->keyboard_attached) {
                    tty_attach_keyboard(t);
                    did_attach_kbd = true;
                }
            }

            ok = wasm_runtime_call_wasm(exec_env, func, 0, NULL);
            goto execve_check;
        } else if (exc && strstr(exc, "wali exit")) {
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

void wasm_fork_child_entry(void *arg)
{
    fork_args_t *fargs = (fork_args_t *)arg;
    wasm_process_t *child_proc = (wasm_process_t *)fargs->child_proc;
    int32_t child_pid = fargs->child_pid;
    WASMModuleInstance *parent_inst = (WASMModuleInstance *)fargs->parent_inst;

    char err[128];

    /* 1. Re-instantiate from the shared module */
    wasm_module_inst_t child_inst_handle = wasm_runtime_instantiate(
        (wasm_module_t)fargs->parent_module, 131072, 65536, err, sizeof(err));
    if (!child_inst_handle) {
        printf("fork child: instantiate error: %s\n", err);
        goto fail_noninst;
    }
    WASMModuleInstance *child_inst = (WASMModuleInstance *)child_inst_handle;

    /* 2. Copy parent's linear memory from snapshot */
    if (fargs->parent_mem_snapshot && child_inst->memory_count > 0) {
        WASMMemoryInstance *child_mem = child_inst->memories[0];

        /* Grow child memory to match parent snapshot size */
        uint32_t needed_pages = (fargs->parent_mem_size + 65535) / 65536;
        if (needed_pages > child_mem->cur_page_count) {
            uint32_t grow = needed_pages - child_mem->cur_page_count;
            wasm_runtime_enlarge_memory(child_inst_handle, grow);
        }

        uint32_t copy_size = fargs->parent_mem_size;
        if (copy_size > child_mem->memory_data_size)
            copy_size = (uint32_t)child_mem->memory_data_size;
        memcpy(child_mem->memory_data, fargs->parent_mem_snapshot, copy_size);
        free(fargs->parent_mem_snapshot);
        fargs->parent_mem_snapshot = NULL;
    }

    /* 3. Copy parent's global data from snapshot */
    if (fargs->parent_global_snapshot && child_inst->global_data_size > 0) {
        uint32_t gsize = fargs->parent_global_size;
        if (gsize > child_inst->global_data_size)
            gsize = child_inst->global_data_size;
        memcpy(child_inst->global_data, fargs->parent_global_snapshot, gsize);
        free(fargs->parent_global_snapshot);
        fargs->parent_global_snapshot = NULL;
    }

    /* 4. Create child exec_env with same stack size */
    wasm_exec_env_t child_exec_env = wasm_runtime_create_exec_env(
        child_inst_handle, fargs->parent_stack_size);
    if (!child_exec_env) {
        printf("fork child: create exec_env failed\n");
        goto fail_inst;
    }

    /* 5. Copy parent's wasm_stack snapshot into child's */
    uint8_t *child_bottom = child_exec_env->wasm_stack.bottom;
    uint8_t *parent_bottom = fargs->parent_stack_snapshot;
    memcpy(child_bottom, parent_bottom, fargs->parent_stack_used);
    child_exec_env->wasm_stack.top = child_bottom + fargs->parent_stack_used;

    /* 6. Fix up all frame pointers */
    ptrdiff_t delta = child_bottom - fargs->parent_stack_bottom;
    WASMFunctionInstance *parent_funcs = parent_inst->e->functions;
    WASMFunctionInstance *child_funcs = child_inst->e->functions;

    WASMInterpFrame *child_cur_frame =
        (WASMInterpFrame *)(child_bottom + fargs->parent_frame_offset);

    WASMInterpFrame *frame = child_cur_frame;
    while (frame) {
        /* Fix function pointer via index remap */
        if (frame->function) {
            uint32_t func_idx = (uint32_t)(frame->function - parent_funcs);
            frame->function = child_funcs + func_idx;
        }
        /* Fix stack pointers (all within wasm_stack buffer) */
        frame->sp_bottom = (uint32 *)((uint8 *)frame->sp_bottom + delta);
        frame->sp_boundary = (uint32 *)((uint8 *)frame->sp_boundary + delta);
        frame->sp = (uint32 *)((uint8 *)frame->sp + delta);
        frame->csp_bottom = (WASMBranchBlock *)((uint8 *)frame->csp_bottom + delta);
        frame->csp_boundary = (WASMBranchBlock *)((uint8 *)frame->csp_boundary + delta);
        frame->csp = (WASMBranchBlock *)((uint8 *)frame->csp + delta);
        /* Fix frame_sp inside each active WASMBranchBlock */
        for (WASMBranchBlock *b = frame->csp_bottom; b < frame->csp; b++) {
            if (b->frame_sp)
                b->frame_sp = (uint32 *)((uint8 *)b->frame_sp + delta);
        }
        /* ip points into shared module bytecode — no fixup needed */
        /* prev_frame fixup */
        if (frame->prev_frame) {
            frame->prev_frame = (WASMInterpFrame *)((uint8 *)frame->prev_frame + delta);
        }
        frame = frame->prev_frame;
    }

    /* 7. Push fork return value 0 onto the caller's operand stack */
    *(child_cur_frame->sp++) = 0;  /* low 32 bits of int64 return = 0 */
    *(child_cur_frame->sp++) = 0;  /* high 32 bits of int64 return = 0 */

    /* 8. Set up child exec_env */
    wasm_exec_env_set_cur_frame(child_exec_env, child_cur_frame);

    child_proc->wasm_module = fargs->parent_module;
    child_proc->wasm_inst = child_inst_handle;
    child_proc->wasm_exec_env = child_exec_env;
    child_proc->is_fork_child = true;
    wasm_runtime_set_user_data(child_exec_env, child_proc);

    /* 9. Resume the interpreter from the copied frame.
     *    Set fork_resume flag, then call _start which enters
     *    wasm_interp_call_func_bytecode. The flag makes it skip
     *    normal frame setup and RECOVER_CONTEXT from cur_frame. */
    child_exec_env->fork_resume = true;

    wasm_function_inst_t start_func = wasm_runtime_lookup_function(
        child_inst_handle, "_start");
    if (!start_func)
        start_func = wasm_runtime_lookup_function(child_inst_handle, "main");

    free(fargs->parent_stack_snapshot);
    free(fargs);

    bool ok = false;
    if (start_func)
        ok = wasm_runtime_call_wasm(child_exec_env, start_func, 0, NULL);

execve_check:;
    /* 10. Handle exit / execve */
    int ret = 0;
    const char *exc = wasm_runtime_get_exception(child_inst_handle);
    if (exc && strstr(exc, "wali execve")) {
        wasm_runtime_clear_exception(child_inst_handle);

        /* Tear down old instance */
        wasm_runtime_destroy_exec_env(child_exec_env);
        wasm_runtime_deinstantiate(child_inst_handle);
        if (!child_proc->is_fork_child && child_proc->wasm_module)
            wasm_runtime_unload(child_proc->wasm_module);

        /* Load new module */
        uint8_t *new_bytes = child_proc->execve_wasm_bytes;
        uint32_t new_size = child_proc->execve_wasm_size;
        child_proc->execve_pending = false;

        wasm_module_t new_module = wasm_runtime_load(new_bytes, new_size, err, sizeof(err));
        if (!new_module) {
            printf("execve: load error: %s\n", err);
            free(new_bytes);
            goto execve_fail;
        }

        wasm_module_inst_t new_inst = wasm_runtime_instantiate(new_module, 131072, 65536, err, sizeof(err));
        if (!new_inst) {
            printf("execve: instantiate error: %s\n", err);
            wasm_runtime_unload(new_module);
            free(new_bytes);
            goto execve_fail;
        }

        wasm_exec_env_t new_exec_env = wasm_runtime_create_exec_env(new_inst, 131072);
        if (!new_exec_env) {
            printf("execve: exec_env error\n");
            wasm_runtime_deinstantiate(new_inst);
            wasm_runtime_unload(new_module);
            free(new_bytes);
            goto execve_fail;
        }

        /* Reset process state for new program */
        child_proc->brk_addr = 0;
        child_proc->mmap_top = 0;
        for (int i = 0; i < WASM_MAX_JMPBUFS; i++)
            child_proc->jmpbufs[i].active = false;

        /* Store new pointers */
        child_proc->wasm_module = new_module;
        child_proc->wasm_inst = new_inst;
        child_proc->wasm_exec_env = new_exec_env;
        child_proc->wasm_bytes = new_bytes;
        child_proc->is_fork_child = false;  /* now owns its own module */

        wasm_runtime_set_user_data(new_exec_env, child_proc);

        /* Update local vars for next iteration */
        child_inst_handle = new_inst;
        child_exec_env = new_exec_env;

        start_func = wasm_runtime_lookup_function(new_inst, "_start");
        if (!start_func)
            start_func = wasm_runtime_lookup_function(new_inst, "main");

        if (!start_func) {
            printf("execve: no _start or main found\n");
            goto execve_cleanup;
        }

        ok = wasm_runtime_call_wasm(child_exec_env, start_func, 0, NULL);
        goto execve_check;
    } else if (exc) {
        if (strstr(exc, "wali exit")) {
            ret = child_proc->exit_code;
        } else {
            printf("fork child trap: %s\n", exc);
            ret = -1;
        }
        wasm_runtime_clear_exception(child_inst_handle);
    }

execve_cleanup:
    /* Cleanup */
    wasm_runtime_destroy_exec_env(child_exec_env);
    wasm_runtime_deinstantiate(child_inst_handle);
    if (!child_proc->is_fork_child) {
        /* After execve, child owns its module and bytecode */
        if (child_proc->wasm_module)
            wasm_runtime_unload(child_proc->wasm_module);
        if (child_proc->wasm_bytes)
            free(child_proc->wasm_bytes);
    }
    /* else: parent owns the module and bytecode — don't free */
    wasm_process_destroy(child_proc);
    proc_mark_exited(child_pid, ret);
    return;

execve_fail:
    wasm_process_destroy(child_proc);
    proc_mark_exited(child_pid, -1);
    return;

fail_inst:
    wasm_runtime_deinstantiate(child_inst_handle);
fail_noninst:
    free(fargs->parent_mem_snapshot);
    free(fargs->parent_global_snapshot);
    free(fargs->parent_stack_snapshot);
    free(fargs);
    wasm_process_destroy(child_proc);
    proc_mark_exited(child_pid, -1);
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
    if (pid < 0) {
        printf("spawn: proc_alloc failed\n");
        return -1;
    }

    spawn_args_t *args = malloc(sizeof(spawn_args_t));
    if (!args) {
        printf("spawn: malloc failed (%u bytes)\n", (unsigned)sizeof(spawn_args_t));
        proc_free(pid);
        return -1;
    }
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
    if (!t) {
        printf("spawn: thread_create failed\n");
        proc_free(pid);
        free(args);
        return -1;
    }
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
