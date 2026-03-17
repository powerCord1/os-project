#include <menu.h>

void thread_test();
void cancel_test_threads();
void test_thread1(void *arg);
void test_thread2(void *arg);
void ssp_test();
void element_test();
void pit_test();
void sin_test();
void random_test();
void colour_test();
void heap_test();
void bmp_test();
void page_fault_test();
void invalid_opcode_test();
void list_acpi_devices();

static const menu_t tests[] = {
    {"Thread test", &thread_test},
    {"Thread cancel test", &cancel_test_threads},
    {"Element test", &element_test},
    {"Smash the stack", &ssp_test},
    {"PIT test", &pit_test},
    {"RNG test", &random_test},
    {"RGB test", &colour_test},
    {"BMP test", &bmp_test},
    {"Trigger page fault", &page_fault_test},
    {"Execute invalid opcode", &invalid_opcode_test},
    {"Sin wave test", &sin_test},
    {"Heap test", &heap_test},
    {"List all ACPI devices", &list_acpi_devices},
};