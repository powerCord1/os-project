#pragma once

#include <wasm_api.h>

int procfs_open(wasm_process_t *proc, const char *path);
