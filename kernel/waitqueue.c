#include <heap.h>
#include <interrupts.h>
#include <scheduler.h>
#include <waitqueue.h>

void waitqueue_init(waitqueue_t *wq)
{
    wq->head = NULL;
}

void waitqueue_sleep(waitqueue_t *wq)
{
    disable_interrupts();

    wq_node_t *node = malloc(sizeof(wq_node_t));
    node->thread_id = scheduler_get_current_id();
    node->next = NULL;

    if (!wq->head) {
        wq->head = node;
    } else {
        wq_node_t *tail = wq->head;
        while (tail->next)
            tail = tail->next;
        tail->next = node;
    }

    scheduler_block_current();
    enable_interrupts();
    scheduler_yield();
}

void waitqueue_wake_one(waitqueue_t *wq)
{
    disable_interrupts();
    if (wq->head) {
        wq_node_t *node = wq->head;
        wq->head = node->next;
        scheduler_unblock(node->thread_id);
        free(node);
    }
    enable_interrupts();
}

void waitqueue_wake_all(waitqueue_t *wq)
{
    disable_interrupts();
    while (wq->head) {
        wq_node_t *node = wq->head;
        wq->head = node->next;
        scheduler_unblock(node->thread_id);
        free(node);
    }
    enable_interrupts();
}
