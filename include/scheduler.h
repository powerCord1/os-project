#pragma once

#include <stdint.h>

typedef enum {
    THREAD_STATE_READY,
    THREAD_STATE_RUNNING,
    THREAD_STATE_BLOCKED,
    THREAD_STATE_TERMINATED
} thread_state_t;

typedef struct thread {
    uint64_t rsp; // Stack pointer - MUST be first
    uint64_t id;
    thread_state_t state;
    void *stack_base;
    struct thread *next;
} thread_t;

void scheduler_init();
thread_t *thread_create(void (*entry)(void *), void *arg);
void scheduler_yield();
uint64_t scheduler_schedule(uint64_t current_rsp);
void scheduler_start();
void thread_cancel(uint64_t id);
