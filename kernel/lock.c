#include <cpu.h>
#include <lock.h>
#include <prediction.h>

void spinlock_acquire(spinlock_t *lp)
{
    while (unlikely(__atomic_test_and_set(&lp->lock, __ATOMIC_ACQUIRE))) {
        cpu_pause();
    }
}

void spinlock_release(spinlock_t *lp)
{
    __atomic_clear(&lp->lock, __ATOMIC_RELEASE);
}