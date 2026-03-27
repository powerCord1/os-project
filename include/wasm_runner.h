#pragma once

#include <stdint.h>

#define SPAWN_MAX_FD_SETUP 8

typedef enum {
    FD_SETUP_NONE,
    FD_SETUP_PIPE_READ,
    FD_SETUP_PIPE_WRITE,
    FD_SETUP_FILE_READ,
    FD_SETUP_FILE_WRITE,
    FD_SETUP_FILE_APPEND
} fd_setup_type_t;

typedef struct {
    fd_setup_type_t type;
    int target_fd;
    int pipe_id;
    char path[64];
} fd_setup_entry_t;

typedef struct {
    int32_t child_pid;
    void *child_proc;               /* wasm_process_t * */
    void *parent_module;            /* WASMModule * (shared) */
    void *parent_inst;              /* parent's WASMModuleInstance * */
    uint8_t *parent_stack_snapshot; /* malloc'd copy of parent's wasm_stack */
    uint32_t parent_stack_used;     /* bytes used in parent's wasm_stack */
    uint32_t parent_stack_size;     /* total wasm_stack allocation size */
    int64_t parent_frame_offset;    /* cur_frame offset from stack bottom */
    uint32_t parent_mem_size;       /* parent's linear memory size in bytes */
    uint8_t *parent_stack_bottom;   /* original parent wasm_stack.bottom */
    uint8_t *parent_mem_snapshot;   /* malloc'd copy of parent's linear memory */
    uint32_t parent_global_size;    /* parent's global data size */
    uint8_t *parent_global_snapshot; /* malloc'd copy of parent's globals */
} fork_args_t;

void wasm_runtime_setup(void);
int wasm_run_file(const char *path, int argc, char **argv);
int32_t wasm_spawn(const char *path, int argc, char **argv, int32_t parent_pid);
int32_t wasm_spawn_redirected(const char *path, int argc, char **argv,
                              int32_t parent_pid, fd_setup_entry_t *setups,
                              int setup_count);
