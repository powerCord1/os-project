void thread_test();
void cancel_test_threads();
void test_thread1(void *arg);
void test_thread2(void *arg);

static const menu_t tests[] = {
    {"Thread test", &thread_test},
    {"Thread cancel test", &cancel_test_threads},
};