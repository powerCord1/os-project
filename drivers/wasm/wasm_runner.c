#include <debug.h>
#include <fs.h>
#include <heap.h>
#include <keyboard.h>
#include <stdio.h>
#include <string.h>
#include <wasm_api.h>
#include <wasm_runner.h>

#include "wasm3.h"
#include "m3_env.h"

int wasm_run_file(const char *path, int argc, char **argv)
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
