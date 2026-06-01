#include <cpu.h>
#include <interrupts.h>
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

void spinlock_acquire_irq(spinlock_t *lp)
{
    unsigned long flags;
    __asm__ volatile("pushfq; pop %0" : "=r"(flags));
    disable_interrupts();
    while (unlikely(__atomic_test_and_set(&lp->lock, __ATOMIC_ACQUIRE))) {
        cpu_pause();
    }
    lp->flags = flags;
}

void spinlock_release_irq(spinlock_t *lp)
{
    unsigned long flags = lp->flags;
    __atomic_clear(&lp->lock, __ATOMIC_RELEASE);
    if (flags & 0x200)
        enable_interrupts();
}