#ifndef _PLATFORM_INTERNAL_H
#define _PLATFORM_INTERNAL_H

#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <heap.h>
#include <lock.h>
#include <waitqueue.h>
#include <scheduler.h>

#ifndef BH_PLATFORM_MYOS
#define BH_PLATFORM_MYOS
#endif

#define BH_APPLET_PRESERVED_STACK_SIZE (2 * 1024)
#define BH_THREAD_DEFAULT_PRIORITY 7

#ifndef LONG_MAX
#define LONG_MAX 0x7FFFFFFFFFFFFFFFL
#endif

#ifndef UINT32_MAX
#define UINT32_MAX 0xFFFFFFFFU
#endif

#ifndef INT32_MAX
#define INT32_MAX 0x7FFFFFFF
#endif

#ifndef INT32_MIN
#define INT32_MIN (-INT32_MAX - 1)
#endif

#ifndef UINT64_MAX
#define UINT64_MAX 0xFFFFFFFFFFFFFFFFULL
#endif

typedef uint64_t korp_tid;
typedef spinlock_t korp_mutex;
typedef unsigned int korp_sem;

typedef struct {
    int dummy;
} korp_rwlock;

typedef struct korp_cond {
    waitqueue_t wq;
    spinlock_t lock;
} korp_cond;

typedef int os_file_handle;
typedef void *os_dir_stream;
typedef int os_raw_file_handle;
typedef int os_poll_file_handle;
typedef unsigned int os_nfds_t;
typedef int os_timespec;

#define os_printf printf
#define os_vprintf vprintf

static inline os_file_handle
os_get_invalid_handle(void)
{
    return -1;
}

static inline int
os_getpagesize(void)
{
    return 4096;
}

static inline int
isdigit(int c)
{
    return c >= '0' && c <= '9';
}

static inline int
isalpha(int c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static inline int
isspace(int c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r'
           || c == '\f' || c == '\v';
}

static inline int
isxdigit(int c)
{
    return isdigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static inline int
isupper(int c)
{
    return c >= 'A' && c <= 'Z';
}

static inline int
isalnum(int c)
{
    return isalpha(c) || isdigit(c);
}

static inline int
isprint(int c)
{
    return c >= ' ' && c <= '~';
}

void abort(void);

static inline long
labs(long x)
{
    return x < 0 ? -x : x;
}

#endif
