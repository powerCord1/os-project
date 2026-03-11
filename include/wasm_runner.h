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

int wasm_run_file(const char *path, int argc, char **argv);
int32_t wasm_spawn(const char *path, int argc, char **argv, int32_t parent_pid);
int32_t wasm_spawn_redirected(const char *path, int argc, char **argv,
                              int32_t parent_pid, fd_setup_entry_t *setups,
                              int setup_count);
