#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <waitqueue.h>

#define PROC_MAX 16

#define PTRACE_TRACEME    0
#define PTRACE_CONT       7
#define PTRACE_GETREGS   12
#define PTRACE_SYSCALL   24

#define PTRACE_MAX_ARGS   6

typedef enum {
    PROC_FREE,
    PROC_RUNNING,
    PROC_STOPPED,
    PROC_EXITED
} proc_state_t;

typedef struct {
    int32_t syscall_nr;
    int32_t _pad;
    int64_t args[PTRACE_MAX_ARGS];
    int64_t ret;
    int32_t at_entry;
} ptrace_info_t;

typedef struct wasm_process wasm_process_t;

typedef struct {
    int32_t pid;
    int32_t parent_pid;
    proc_state_t state;
    int32_t exit_code;
    volatile bool killed;
    uint64_t thread_id;
    wasm_process_t *wasm_proc;
    waitqueue_t exit_wq;

    volatile uint64_t sig_pending;

    int32_t tracer_pid;
    bool ptrace_syscall;
    ptrace_info_t ptrace_info;
    waitqueue_t ptrace_wq;
} proc_entry_t;

void proc_table_init(void);
int32_t proc_alloc(int32_t parent_pid);
proc_entry_t *proc_get(int32_t pid);
void proc_free(int32_t pid);
int32_t proc_foreground_pid(void);
void proc_set_foreground(int32_t pid);
void proc_mark_exited(int32_t pid, int32_t exit_code);
void wali_check_timers(void);
