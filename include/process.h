#pragma once

#include <stdbool.h>
#include <stdint.h>

#define PROC_MAX 16

typedef enum {
    PROC_FREE,
    PROC_RUNNING,
    PROC_EXITED
} proc_state_t;

typedef struct wasm_process wasm_process_t;

typedef struct {
    int32_t pid;
    int32_t parent_pid;
    proc_state_t state;
    int32_t exit_code;
    volatile bool killed;
    uint64_t thread_id;
    wasm_process_t *wasm_proc;
} proc_entry_t;

void proc_table_init(void);
int32_t proc_alloc(int32_t parent_pid);
proc_entry_t *proc_get(int32_t pid);
void proc_free(int32_t pid);
int32_t proc_foreground_pid(void);
void proc_set_foreground(int32_t pid);
