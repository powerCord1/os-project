#pragma once

#include <stdio.h>

void __attribute__((noreturn)) abort(void);

#ifdef NDEBUG
#define assert(x) ((void)0)
#else
#define assert(x)                                                              \
    do {                                                                       \
        if (!(x)) {                                                            \
            printf("assert failed: %s (%s:%d)\n", #x, __FILE__, __LINE__);    \
            abort();                                                           \
        }                                                                      \
    } while (0)
#endif
