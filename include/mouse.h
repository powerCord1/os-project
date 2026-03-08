#pragma once

#include <stdint.h>

void mouse_init();
uint64_t mouse_handler(uint64_t rsp, void *ctx);
