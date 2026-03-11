#include "api.h"

void _start(void)
{
    int argc = get_argc();
    for (int i = 1; i < argc; i++) {
        char buf[256];
        get_argv(i, buf, sizeof(buf));
        puts(buf);
        if (i < argc - 1)
            putchar(' ');
    }
    putchar('\n');
}
