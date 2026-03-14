#include <heap.h>
#include <interrupts.h>
#include <lock.h>
#include <scheduler.h>
#include <waitqueue.h>

void waitqueue_init(waitqueue_t *wq)
{
    wq->head = NULL;
    wq->lock = (spinlock_t){0, "waitqueue"};
}

void waitqueue_sleep(waitqueue_t *wq)
{
    waitqueue_begin_sleep(wq);
    waitqueue_end_sleep(wq);
}

static void wq_enqueue(waitqueue_t *wq, wq_node_t *node)
{
    if (!wq->head) {
        wq->head = node;
    } else {
        wq_node_t *tail = wq->head;
        while (tail->next)
            tail = tail->next;
        tail->next = node;
    }
}

void waitqueue_begin_sleep(waitqueue_t *wq)
{
    wq_node_t *node = malloc(sizeof(wq_node_t));
    node->thread_id = scheduler_get_current_id();
    node->next = NULL;

    spinlock_acquire_irq(&wq->lock);
    wq_enqueue(wq, node);
    scheduler_block_current();
}

void waitqueue_end_sleep(waitqueue_t *wq)
{
    spinlock_release_irq(&wq->lock);
    __asm__ volatile("hlt");
    scheduler_yield();
}

void waitqueue_cancel_sleep(waitqueue_t *wq)
{
    uint64_t id = scheduler_get_current_id();
    wq_node_t **pp = &wq->head;
    while (*pp) {
        if ((*pp)->thread_id == id) {
            wq_node_t *node = *pp;
            *pp = node->next;
            free(node);
            break;
        }
        pp = &(*pp)->next;
    }
    scheduler_unblock_current();
    spinlock_release_irq(&wq->lock);
}

void waitqueue_wake_one(waitqueue_t *wq)
{
    spinlock_acquire_irq(&wq->lock);
    if (wq->head) {
        wq_node_t *node = wq->head;
        wq->head = node->next;
        scheduler_unblock(node->thread_id);
        free(node);
    }
    spinlock_release_irq(&wq->lock);
}

void waitqueue_wake_all(waitqueue_t *wq)
{
    spinlock_acquire_irq(&wq->lock);
    while (wq->head) {
        wq_node_t *node = wq->head;
        wq->head = node->next;
        scheduler_unblock(node->thread_id);
        free(node);
    }
    spinlock_release_irq(&wq->lock);
}
