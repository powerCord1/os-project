#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "wasm3.h"

#define WASM_MAX_FDS 16
#define WASM_MAX_ARGC 16
#define WASM_MAX_ARG_LEN 256

#define WASM_O_RDONLY 0x0001
#define WASM_O_WRONLY 0x0002
#define WASM_O_RDWR   0x0003
#define WASM_O_CREAT  0x0100
#define WASM_O_TRUNC  0x0200
#define WASM_O_APPEND 0x0400

#define WASM_SEEK_SET 0
#define WASM_SEEK_CUR 1
#define WASM_SEEK_END 2

typedef struct {
    bool in_use;
    uint8_t *data;
    uint32_t size;
    uint32_t pos;
    uint32_t flags;
    bool writable;
    bool dirty;
    uint32_t parent_cluster;
    char filename[12];
} wasm_fd_t;

typedef struct {
    int argc;
    char argv[WASM_MAX_ARGC][WASM_MAX_ARG_LEN];
    int32_t exit_code;
    wasm_fd_t fds[WASM_MAX_FDS];
} wasm_process_t;

wasm_process_t *wasm_process_create(int argc, char **argv);
void wasm_process_destroy(wasm_process_t *proc);
void wasm_link_api(IM3Module module, wasm_process_t *proc);
