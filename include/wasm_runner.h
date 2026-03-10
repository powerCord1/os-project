#pragma once

#include <stdint.h>

int wasm_run_file(const char *path, int argc, char **argv);
int32_t wasm_spawn(const char *path, int argc, char **argv, int32_t parent_pid);
