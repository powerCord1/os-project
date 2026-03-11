#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <waitqueue.h>

#define PIPE_BUF_SIZE 4096
#define PIPE_MAX 16

typedef struct {
    uint8_t buf[PIPE_BUF_SIZE];
    volatile uint16_t head, tail;
    int read_refs, write_refs;
    bool active;
    waitqueue_t read_wq;
    waitqueue_t write_wq;
} pipe_t;

int pipe_alloc(void);
int pipe_read(int pipe_id, uint8_t *buf, int count);
int pipe_write(int pipe_id, const uint8_t *buf, int count);
void pipe_ref_read(int pipe_id);
void pipe_ref_write(int pipe_id);
void pipe_unref_read(int pipe_id);
void pipe_unref_write(int pipe_id);
pipe_t *pipe_get(int pipe_id);
