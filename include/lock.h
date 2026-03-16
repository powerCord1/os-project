#pragma once

typedef struct {
    volatile int lock;
    char *name;
    unsigned long flags;
} spinlock_t;

void spinlock_acquire(spinlock_t *lp);
void spinlock_release(spinlock_t *lp);
void spinlock_acquire_irq(spinlock_t *lp);
void spinlock_release_irq(spinlock_t *lp);