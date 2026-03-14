#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "wasm_export.h"

#define WASM_MAX_FDS 64
#define WASM_MAX_ARGC 16
#define WASM_MAX_ARG_LEN 256
#define WASM_MAX_JMPBUFS 16

#define WASM_O_RDONLY 0x0001
#define WASM_O_WRONLY 0x0002
#define WASM_O_RDWR   0x0003
#define WASM_O_CREAT  0x0100
#define WASM_O_TRUNC  0x0200
#define WASM_O_APPEND 0x0400

#define WASM_SEEK_SET 0
#define WASM_SEEK_CUR 1
#define WASM_SEEK_END 2

typedef enum {
    FD_NONE,
    FD_CONSOLE,
    FD_FILE,
    FD_PIPE_READ,
    FD_PIPE_WRITE
} fd_type_t;

typedef struct {
    fd_type_t type;
    union {
        struct {
            uint8_t *data;
            uint32_t size, pos, flags;
            bool writable, dirty;
            uint32_t parent_cluster;
            char filename[12];
        } file;
        struct {
            int pipe_id;
        } pipe;
    };
} wasm_fd_t;

typedef struct {
    int32_t wasm_buf_addr;      /* WASM-side jmp_buf address (key) */
    void *saved_frame;          /* WASM frame of setjmp's caller (prev_frame) */
    void *saved_stack_top;      /* wasm_stack.top to restore (= native frame start) */
    void *saved_sp;             /* caller frame's sp at setjmp time */
    bool active;
} wali_jmpbuf_entry_t;

typedef struct wasm_process {
    int argc;
    char argv[WASM_MAX_ARGC][WASM_MAX_ARG_LEN];
    int32_t exit_code;
    int32_t pid;
    int tty_id;
    wasm_fd_t fds[WASM_MAX_FDS];
    uint32_t fd_flags[WASM_MAX_FDS];

    uint32_t brk_addr;
    uint32_t mmap_top;
    char cwd[256];

    uint32_t c_iflag, c_oflag, c_cflag, c_lflag;
    uint8_t c_cc[20];
    uint32_t umask;

    uint32_t sigactions[64];
    uint64_t sig_mask;
    uint64_t sig_pending;

    int64_t itimer_interval_us;
    int64_t itimer_value_us;
    uint64_t itimer_next_tick;

    wali_jmpbuf_entry_t jmpbufs[WASM_MAX_JMPBUFS];

    /* Runtime pointers for fork access (set by wasm_run_module) */
    void *wasm_module;      /* wasm_module_t — shared, refcounted */
    void *wasm_inst;        /* wasm_module_inst_t — parent's instance */
    void *wasm_exec_env;    /* wasm_exec_env_t — parent's exec env */
    uint8_t *wasm_bytes;    /* original module bytecode */
    bool is_fork_child;     /* true if this proc was created by fork */

    /* Execve state: set by wali_sys_execve, consumed by runner */
    uint8_t *execve_wasm_bytes;   /* new module bytecode (malloc'd) */
    uint32_t execve_wasm_size;    /* size of new module bytecode */
    bool execve_pending;          /* true when execve is requested */
} wasm_process_t;

wasm_process_t *wasm_process_create(int argc, char **argv);
wasm_process_t *wasm_process_deep_copy(wasm_process_t *src);
void wasm_process_destroy(wasm_process_t *proc);
void wasm_register_env_natives(void);
void wasm_register_wali_natives(void);
void wasm_fork_child_entry(void *arg);
