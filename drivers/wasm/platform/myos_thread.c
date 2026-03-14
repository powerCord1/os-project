#include "platform_api_vmcore.h"
#include "platform_api_extension.h"

#include <heap.h>
#include <scheduler.h>
#include <string.h>
#include <waitqueue.h>

static bool is_thread_sys_inited = false;

typedef struct os_thread_data {
    struct os_thread_data *next;
    korp_tid tid;
    thread_start_routine_t start_routine;
    void *arg;
    void *ret;
    bool done;
    waitqueue_t join_wq;
} os_thread_data;

static spinlock_t thread_data_lock = { 0, "thread_data" };
static os_thread_data *thread_data_list = NULL;

static void
thread_data_list_add(os_thread_data *td)
{
    spinlock_acquire(&thread_data_lock);
    td->next = thread_data_list;
    thread_data_list = td;
    spinlock_release(&thread_data_lock);
}

static void
thread_data_list_remove(os_thread_data *td)
{
    spinlock_acquire(&thread_data_lock);
    if (thread_data_list == td) {
        thread_data_list = td->next;
    } else {
        os_thread_data *p = thread_data_list;
        while (p && p->next != td)
            p = p->next;
        if (p)
            p->next = td->next;
    }
    spinlock_release(&thread_data_lock);
}

static os_thread_data *
thread_data_list_lookup(korp_tid tid)
{
    spinlock_acquire(&thread_data_lock);
    os_thread_data *p = thread_data_list;
    while (p) {
        if (p->tid == tid) {
            spinlock_release(&thread_data_lock);
            return p;
        }
        p = p->next;
    }
    spinlock_release(&thread_data_lock);
    return NULL;
}

int
os_thread_sys_init(void)
{
    if (is_thread_sys_inited)
        return BHT_OK;
    is_thread_sys_inited = true;
    return BHT_OK;
}

void
os_thread_sys_destroy(void)
{
    is_thread_sys_inited = false;
}

static void
os_thread_wrapper(void *arg)
{
    os_thread_data *td = (os_thread_data *)arg;
    td->tid = scheduler_get_current_id();
    thread_data_list_add(td);

    td->ret = td->start_routine(td->arg);
    td->done = true;
    waitqueue_wake_all(&td->join_wq);

    thread_data_list_remove(td);
    free(td);
}

int
os_thread_create(korp_tid *p_tid, thread_start_routine_t start, void *arg,
                 unsigned int stack_size)
{
    return os_thread_create_with_prio(p_tid, start, arg, stack_size,
                                      BH_THREAD_DEFAULT_PRIORITY);
}

int
os_thread_create_with_prio(korp_tid *p_tid, thread_start_routine_t start,
                           void *arg, unsigned int stack_size, int prio)
{
    (void)stack_size;
    (void)prio;

    if (!p_tid)
        return BHT_ERROR;

    os_thread_data *td = malloc(sizeof(os_thread_data));
    if (!td)
        return BHT_ERROR;

    memset(td, 0, sizeof(os_thread_data));
    td->start_routine = start;
    td->arg = arg;
    td->done = false;
    waitqueue_init(&td->join_wq);

    thread_t *t = thread_create(os_thread_wrapper, td);
    if (!t) {
        free(td);
        return BHT_ERROR;
    }

    td->tid = t->id;
    *p_tid = t->id;
    return BHT_OK;
}

korp_tid
os_self_thread(void)
{
    return scheduler_get_current_id();
}

int
os_thread_join(korp_tid thread, void **value_ptr)
{
    os_thread_data *td = thread_data_list_lookup(thread);
    if (!td) {
        if (value_ptr)
            *value_ptr = NULL;
        return BHT_OK;
    }

    while (!td->done)
        waitqueue_sleep(&td->join_wq);

    if (value_ptr)
        *value_ptr = td->ret;

    return BHT_OK;
}

int
os_mutex_init(korp_mutex *mutex)
{
    mutex->lock = 0;
    mutex->name = "wamr_mutex";
    return BHT_OK;
}

int
os_mutex_destroy(korp_mutex *mutex)
{
    (void)mutex;
    return BHT_OK;
}

int
os_mutex_lock(korp_mutex *mutex)
{
    spinlock_acquire(mutex);
    return 0;
}

int
os_mutex_unlock(korp_mutex *mutex)
{
    spinlock_release(mutex);
    return 0;
}

int
os_cond_init(korp_cond *cond)
{
    waitqueue_init(&cond->wq);
    cond->lock.lock = 0;
    cond->lock.name = "wamr_cond";
    return BHT_OK;
}

int
os_cond_destroy(korp_cond *cond)
{
    (void)cond;
    return BHT_OK;
}

int
os_cond_wait(korp_cond *cond, korp_mutex *mutex)
{
    spinlock_release(mutex);
    waitqueue_sleep(&cond->wq);
    spinlock_acquire(mutex);
    return BHT_OK;
}

int
os_cond_reltimedwait(korp_cond *cond, korp_mutex *mutex, uint64 useconds)
{
    (void)useconds;
    return os_cond_wait(cond, mutex);
}

int
os_cond_signal(korp_cond *cond)
{
    waitqueue_wake_one(&cond->wq);
    return BHT_OK;
}

int
os_cond_broadcast(korp_cond *cond)
{
    waitqueue_wake_all(&cond->wq);
    return BHT_OK;
}

uint8 *
os_thread_get_stack_boundary(void)
{
    return NULL;
}

int
os_thread_detach(korp_tid thread)
{
    (void)thread;
    return BHT_OK;
}

void
os_thread_exit(void *retval)
{
    (void)retval;
    os_thread_data *td = thread_data_list_lookup(os_self_thread());
    if (td) {
        td->ret = retval;
        td->done = true;
        waitqueue_wake_all(&td->join_wq);
        thread_data_list_remove(td);
        free(td);
    }
    thread_cancel(scheduler_get_current_id());
    scheduler_yield();
    while (1)
        __asm__ volatile("hlt");
}

void
os_thread_jit_write_protect_np(bool enabled)
{
    (void)enabled;
}
