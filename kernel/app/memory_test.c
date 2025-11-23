#include <app.h>
#include <keyboard.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

void memory_test_main()
{
    printf("Running memory test...\n");

    // memset
    char memset_buf[10];
    memset(memset_buf, 'A', 10);
    bool memset_ok = true;
    for (int i = 0; i < 10; i++) {
        if (memset_buf[i] != 'A') {
            memset_ok = false;
            break;
        }
    }
    printf("memset: %s\n", memset_ok ? "PASS" : "FAIL");

    // memcpy
    char memcpy_src[] = "Hello";
    char memcpy_dst[6];
    memcpy(memcpy_dst, memcpy_src, 6);
    printf("memcpy: %s\n",
           strcmp(memcpy_src, memcpy_dst) == 0 ? "PASS" : "FAIL");

    // memcmp
    char memcmp_buf1[] = "Test";
    char memcmp_buf2[] = "Test";
    char memcmp_buf3[] = "Fail";
    printf("memcmp (equal): %s\n",
           memcmp(memcmp_buf1, memcmp_buf2, 5) == 0 ? "PASS" : "FAIL");
    printf("memcmp (unequal): %s\n",
           memcmp(memcmp_buf1, memcmp_buf3, 5) != 0 ? "PASS" : "FAIL");

    // memmove
    char memmove_buf[] = "123456789";
    memmove(memmove_buf + 2, memmove_buf, 5); // overlapping
    printf("memmove (overlap): %s\n",
           strcmp(memmove_buf, "121234589") == 0 ? "PASS" : "FAIL");

    printf("Memory test finished. Press any key to exit.\n");
    kbd_get_key(true);
}
