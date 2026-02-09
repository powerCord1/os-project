typedef struct {
    volatile int lock;
    char *name;
} spinlock_t;

void spinlock_acquire(spinlock_t *lp);
void spinlock_release(spinlock_t *lp);