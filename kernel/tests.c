#include <stddef.h>

#include <debug.h>
#include <menu.h>
#include <scheduler.h>
#include <tests.h>
#include <timer.h>

thread_t *thread1;
thread_t *thread2;

void thread_test()
{
    thread1 = thread_create(test_thread1, NULL);
    thread2 = thread_create(test_thread2, NULL);
}

void cancel_test_threads()
{
    thread_cancel(thread1->id);
    thread_cancel(thread2->id);
}

void test_thread1(void *arg)
{
    (void)arg;
    while (1) {
        log_info("Thread 1 running");
        wait_ms(1000);
    }
}

void test_thread2(void *arg)
{
    (void)arg;
    while (1) {
        log_info("Thread 2 running");
        wait_ms(1000);
    }
}