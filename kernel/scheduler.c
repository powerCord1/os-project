#include <debug.h>
#include <heap.h>
#include <interrupts.h>
#include <lock.h>
#include <scheduler.h>
#include <smp.h>
#include <string.h>

#define THREAD_STACK_SIZE 16384

static thread_t *ready_list = NULL;
static uint64_t next_thread_id = 0;
static bool scheduler_running = false;
static spinlock_t sched_lock = {0, "sched"};

static thread_t *get_current_thread(void)
{
    cpu_t *cpu = smp_get_current_cpu();
    return cpu ? cpu->current_thread : NULL;
}

static void set_current_thread(thread_t *t)
{
    cpu_t *cpu = smp_get_current_cpu();
    if (cpu)
        cpu->current_thread = t;
}

void scheduler_init()
{
    log_info("Initializing scheduler");

    thread_t *initial_thread = malloc(sizeof(thread_t));
    initial_thread->id = next_thread_id++;
    initial_thread->state = THREAD_STATE_RUNNING;
    initial_thread->stack_base = NULL;
    initial_thread->next = initial_thread;

    ready_list = initial_thread;

    // BSP's current_thread is set once SMP init sets up GS base.
    // For now store it so smp_init can pick it up.
    set_current_thread(initial_thread);
}

static void thread_wrapper(void (*entry)(void *), void *arg)
{
    enable_interrupts();
    entry(arg);

    disable_interrupts();
    get_current_thread()->state = THREAD_STATE_TERMINATED;
    scheduler_yield();

    while (1)
        __asm__ volatile("hlt");
}

thread_t *thread_create(void (*entry)(void *), void *arg)
{
    thread_t *thread = malloc(sizeof(thread_t));
    if (!thread)
        return NULL;

    spinlock_acquire_irq(&sched_lock);
    thread->id = next_thread_id++;
    spinlock_release_irq(&sched_lock);

    thread->state = THREAD_STATE_READY;
    thread->stack_base = malloc(THREAD_STACK_SIZE);
    if (!thread->stack_base) {
        free(thread);
        return NULL;
    }

    uint64_t *stack = (uint64_t *)(((uintptr_t)thread->stack_base
        + THREAD_STACK_SIZE) & ~(uintptr_t)0xF);

    uint64_t thread_rsp = (uint64_t)stack - 8;

    *(--stack) = 0x10;                     // SS
    *(--stack) = thread_rsp;               // RSP
    *(--stack) = 0x202;                    // RFLAGS (IF enabled)
    *(--stack) = 0x08;                     // CS
    *(--stack) = (uint64_t)thread_wrapper; // RIP

    *(--stack) = (uint64_t)entry; // RDI
    *(--stack) = (uint64_t)arg;   // RSI
    *(--stack) = 0;               // RDX
    *(--stack) = 0;               // RCX
    *(--stack) = 0;               // RBX
    *(--stack) = 0;               // RAX
    *(--stack) = 0;               // RBP
    *(--stack) = 0;               // R8
    *(--stack) = 0;               // R9
    *(--stack) = 0;               // R10
    *(--stack) = 0;               // R11
    *(--stack) = 0;               // R12
    *(--stack) = 0;               // R13
    *(--stack) = 0;               // R14
    *(--stack) = 0;               // R15

    thread->rsp = (uint64_t)stack;

    spinlock_acquire_irq(&sched_lock);
    thread->next = ready_list->next;
    ready_list->next = thread;
    spinlock_release_irq(&sched_lock);

    return thread;
}

uint64_t scheduler_schedule(uint64_t current_rsp)
{
    if (!scheduler_running)
        return current_rsp;

    thread_t *cur = get_current_thread();
    if (!cur)
        return current_rsp;

    spinlock_acquire_irq(&sched_lock);

    cur->rsp = current_rsp;

    thread_t *next = cur->next;
    while (next->state != THREAD_STATE_READY &&
           next->state != THREAD_STATE_RUNNING) {
        next = next->next;
        if (next == cur)
            break;
    }

    if (next == cur && cur->state != THREAD_STATE_READY &&
        cur->state != THREAD_STATE_RUNNING) {
        spinlock_release_irq(&sched_lock);
        return current_rsp;
    }

    if (cur->state == THREAD_STATE_RUNNING)
        cur->state = THREAD_STATE_READY;

    set_current_thread(next);
    next->state = THREAD_STATE_RUNNING;

    spinlock_release_irq(&sched_lock);

    return next->rsp;
}

void scheduler_yield()
{
    __asm__ volatile("int $0x20");
}

void scheduler_start()
{
    log_info("Starting scheduler");
    scheduler_running = true;
}

void thread_cancel(uint64_t id)
{
    spinlock_acquire_irq(&sched_lock);
    thread_t *thread = ready_list;
    bool found = false;
    if (thread != NULL) {
        do {
            if (thread->id == id) {
                thread->state = THREAD_STATE_TERMINATED;
                found = true;
                break;
            }
            thread = thread->next;
        } while (thread != ready_list);
    }
    spinlock_release_irq(&sched_lock);

    if (found && get_current_thread()->id == id) {
        scheduler_yield();
        log_err("Thread %lu failed to yield after cancellation", id);
        while (1)
            __asm__ volatile("hlt");
    }
}

uint64_t scheduler_get_current_id(void)
{
    thread_t *cur = get_current_thread();
    return cur ? cur->id : 0;
}

void scheduler_block_current(void)
{
    thread_t *cur = get_current_thread();
    if (cur)
        cur->state = THREAD_STATE_BLOCKED;
}

void scheduler_unblock_current(void)
{
    thread_t *cur = get_current_thread();
    if (cur && cur->state == THREAD_STATE_BLOCKED)
        cur->state = THREAD_STATE_RUNNING;
}

void scheduler_unblock(uint64_t id)
{
    spinlock_acquire_irq(&sched_lock);
    thread_t *thread = ready_list;
    if (thread) {
        do {
            if (thread->id == id && thread->state == THREAD_STATE_BLOCKED) {
                thread->state = THREAD_STATE_READY;
                break;
            }
            thread = thread->next;
        } while (thread != ready_list);
    }
    spinlock_release_irq(&sched_lock);
}

void wait_for_thread(uint64_t id)
{
    while (true) {
        spinlock_acquire_irq(&sched_lock);
        thread_t *thread = ready_list;
        bool still_exists = false;

        if (thread != NULL) {
            do {
                if (thread->id == id &&
                    thread->state != THREAD_STATE_TERMINATED) {
                    still_exists = true;
                    break;
                }
                thread = thread->next;
            } while (thread != ready_list);
        }

        spinlock_release_irq(&sched_lock);

        if (!still_exists)
            break;

        scheduler_yield();
    }
}

void wait_for_all_threads()
{
    spinlock_acquire_irq(&sched_lock);
    thread_t *start_node = ready_list;
    thread_t *current = start_node;

    if (current == NULL) {
        spinlock_release_irq(&sched_lock);
        return;
    }

    do {
        uint64_t id = current->id;
        if (id != 0) {
            spinlock_release_irq(&sched_lock);
            wait_for_thread(id);
            spinlock_acquire_irq(&sched_lock);
        }
        current = current->next;
    } while (current != start_node);

    spinlock_release_irq(&sched_lock);
}

void scheduler_set_initial_thread(thread_t *thread)
{
    set_current_thread(thread);
}

thread_t *scheduler_get_initial_thread(void)
{
    return ready_list;
}
