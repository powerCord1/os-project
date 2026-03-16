#pragma once

#include <stdbool.h>
#include <stdint.h>

struct fs_driver;

#define DEVFS_MAX_DEVICES 16

typedef struct {
    const char *name;
    int (*open)(void **state);
    int64_t (*read)(void *state, uint8_t *buf, uint32_t count, uint64_t offset, bool nonblock);
    int64_t (*write)(void *state, const uint8_t *buf, uint32_t count, uint64_t offset);
    int64_t (*ioctl)(void *state, int32_t req, void *argp, uint32_t size);
    bool (*poll_readable)(void *state);
    bool (*poll_writable)(void *state);
    void (*close)(void *state);
} devfs_device_t;

void devfs_init(void);
int devfs_register(const devfs_device_t *dev);
int devfs_lookup(const char *name);
const devfs_device_t *devfs_get(int dev_id);
int devfs_count(void);
const char *devfs_name(int dev_id);

extern struct fs_driver devfs_driver;

void fb_dev_init(void);
void input_dev_init(void);
void input_dev_push_event(uint8_t scancode, bool pressed);
