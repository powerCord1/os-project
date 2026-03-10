#include <debug.h>
#include <fs.h>
#include <heap.h>
#include <keyboard.h>
#include <process.h>
#include <scheduler.h>
#include <stdio.h>
#include <string.h>
#include <wasm_api.h>
#include <wasm_runner.h>

#include "wasm3.h"
#include "m3_env.h"

typedef struct {
    char path[256];
    int argc;
    char argv[WASM_MAX_ARGC][WASM_MAX_ARG_LEN];
    int32_t pid;
} spawn_args_t;

static int wasm_run_module(const char *path, int argc, char **argv, int32_t pid)
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

    IM3Environment env = m3_NewEnvironment();
    IM3Runtime runtime = m3_NewRuntime(env, 8192, NULL);
    IM3Module module = NULL;

    M3Result result = m3_ParseModule(env, &module, wasm_bytes, size);
    if (result) {
        printf("Parse error: %s\n", result);
        m3_FreeRuntime(runtime);
        m3_FreeEnvironment(env);
        free(wasm_bytes);
        wasm_process_destroy(proc);
        return -1;
    }

    result = m3_LoadModule(runtime, module);
    if (result) {
        printf("Load error: %s\n", result);
        m3_FreeModule(module);
        m3_FreeRuntime(runtime);
        m3_FreeEnvironment(env);
        free(wasm_bytes);
        wasm_process_destroy(proc);
        return -1;
    }

    wasm_link_api(module, proc);

    IM3Function func;
    result = m3_FindFunction(&func, runtime, "_start");
    if (result)
        result = m3_FindFunction(&func, runtime, "main");

    if (result) {
        printf("No _start or main: %s\n", result);
        m3_FreeRuntime(runtime);
        m3_FreeEnvironment(env);
        free(wasm_bytes);
        wasm_process_destroy(proc);
        return -1;
    }

    kbd_buffer_init();
    result = m3_Call(func, 0, NULL);

    int ret = 0;
    if (result) {
        if (result == m3Err_trapExit) {
            ret = proc->exit_code;
        } else {
            M3ErrorInfo info;
            m3_GetErrorInfo(runtime, &info);
            printf("Trap: %s", result);
            if (info.message)
                printf(" (%s)", info.message);
            printf("\n");
            ret = -1;
        }
    }

    m3_FreeRuntime(runtime);
    m3_FreeEnvironment(env);
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
    int ret = wasm_run_module(path, argc, argv, pid);

    proc_entry_t *entry = proc_get(pid);
    if (entry) {
        entry->state = PROC_EXITED;
        entry->exit_code = ret;
    }
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

    int ret = wasm_run_module(args->path, args->argc, argv_ptrs, pid);

    proc_entry_t *entry = proc_get(pid);
    if (entry) {
        entry->state = PROC_EXITED;
        entry->exit_code = ret;
        if (proc_foreground_pid() == pid)
            proc_set_foreground(entry->parent_pid);
    }

    free(args);
}

int32_t wasm_spawn(const char *path, int argc, char **argv, int32_t parent_pid)
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

    thread_t *t = thread_create(wasm_spawn_entry, args);
    proc_entry_t *entry = proc_get(pid);
    if (entry) {
        entry->thread_id = t->id;
    }
    proc_set_foreground(pid);

    return pid;
}
