#include <debug.h>
#include <heap.h>
#include <interrupts.h>
#include <scheduler.h>
#include <string.h>

#define THREAD_STACK_SIZE 16384 // 16 KB

static thread_t *current_thread = NULL;
static thread_t *ready_list = NULL;
static uint64_t next_thread_id = 0;
static bool scheduler_running = false;

void scheduler_init()
{
    log_info("Initializing scheduler");

    // Create the "initial" thread which represents the current execution flow
    // (kernel main)
    thread_t *initial_thread = malloc(sizeof(thread_t));
    initial_thread->id = next_thread_id++;
    initial_thread->state = THREAD_STATE_RUNNING;
    initial_thread->stack_base = NULL;
    initial_thread->next = initial_thread; // Circular list

    current_thread = initial_thread;
    ready_list = initial_thread;
}

static void thread_wrapper(void (*entry)(void *), void *arg)
{
    enable_interrupts(); // Threads should start with interrupts enabled
    entry(arg);

    disable_interrupts();
    current_thread->state = THREAD_STATE_TERMINATED;
    scheduler_yield();

    while (1) {
        __asm__ volatile("hlt");
    }
}

thread_t *thread_create(void (*entry)(void *), void *arg)
{
    thread_t *thread = malloc(sizeof(thread_t));
    thread->id = next_thread_id++;
    thread->state = THREAD_STATE_READY;
    thread->stack_base = malloc(THREAD_STACK_SIZE);

    // Set up the initial stack
    uint64_t *stack =
        (uint64_t *)((uint8_t *)thread->stack_base + THREAD_STACK_SIZE);

    // Push initial context onto stack
    // We want it to look like it was interrupted at the start of thread_wrapper

    // CPU state (pushed by hardware on interrupt)
    *(--stack) = 0x10;                     // SS
    *(--stack) = (uint64_t)stack;          // RSP
    *(--stack) = 0x202;                    // RFLAGS (IF enabled)
    *(--stack) = 0x08;                     // CS
    *(--stack) = (uint64_t)thread_wrapper; // RIP

    // Pushed by IRQ_HANDLER macro (15 registers)
    *(--stack) = (uint64_t)entry; // RDI (1st arg to thread_wrapper)
    *(--stack) = (uint64_t)arg;   // RSI (2nd arg to thread_wrapper)
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

    // Add to circular ready list
    disable_interrupts();
    thread->next = ready_list->next;
    ready_list->next = thread;
    enable_interrupts();

    return thread;
}

uint64_t scheduler_schedule(uint64_t current_rsp)
{
    if (!scheduler_running || !current_thread) {
        return current_rsp;
    }

    current_thread->rsp = current_rsp;

    // Pick next ready thread
    thread_t *next = current_thread->next;
    while (next->state != THREAD_STATE_READY &&
           next->state != THREAD_STATE_RUNNING) {
        next = next->next;
        if (next == current_thread) {
            break; // All other threads are not ready
        }
    }

    current_thread = next;
    current_thread->state = THREAD_STATE_RUNNING;

    return current_thread->rsp;
}

void scheduler_yield()
{
    __asm__ volatile("int $0x20"); // Trigger PIT IRQ
}

void scheduler_start()
{
    log_info("Starting scheduler");
    scheduler_running = true;
}

void thread_cancel(uint64_t id)
{
    disable_interrupts();
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

    if (found && current_thread->id == id) {
        scheduler_yield();
        // Should not reach here
        log_err("Thread %lu failed to yield after cancellation", id);
        while (1) {
            __asm__ volatile("hlt");
        }
    }
    enable_interrupts();
}