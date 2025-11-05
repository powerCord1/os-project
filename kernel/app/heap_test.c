#include <app.h>
#include <heap.h>
#include <keyboard.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

void heap_test_init()
{
    printf("Running heap test...\n");

    char *test_str = (char *)malloc(20);
    if (test_str) {
        strcpy(test_str, "Heap test successful!");
        printf("%s\n", test_str);
        free(test_str);
    } else {
        printf("Failed to allocate memory.\n");
    }

    printf("Heap test finished.\n");
    printf("Press any key exit\n");
    kbd_get_last_char(true);
}
