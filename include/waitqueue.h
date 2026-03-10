#pragma once

#include <stdint.h>

typedef struct wq_node {
    uint64_t thread_id;
    struct wq_node *next;
} wq_node_t;

typedef struct {
    wq_node_t *head;
} waitqueue_t;

void waitqueue_init(waitqueue_t *wq);
void waitqueue_sleep(waitqueue_t *wq);
void waitqueue_wake_one(waitqueue_t *wq);
void waitqueue_wake_all(waitqueue_t *wq);
