#include "api.h"

void _start(void)
{
    const char *path = "/";
    char pathbuf[256];
    if (get_argc() >= 2) {
        get_argv(1, pathbuf, sizeof(pathbuf));
        path = pathbuf;
    }

    char buf[4096];
    int count = readdir_path(path, buf, sizeof(buf));
    if (count < 0) {
        puts("Failed to list: ");
        puts(path);
        putchar('\n');
        exit(1);
    }

    int pos = 0;
    for (int i = 0; i < count; i++) {
        int len = strlen(buf + pos);
        puts(buf + pos);
        putchar('\n');
        pos += len + 1;
    }
}
