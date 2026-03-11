#include "api.h"

void _start(void)
{
    if (get_argc() < 2) {
        puts("Usage: cat <file>\n");
        exit(1);
    }

    char path[256];
    get_argv(1, path, sizeof(path));

    int fd = open_path(path, O_RDONLY);
    if (fd < 0) {
        puts("Failed to open: ");
        puts(path);
        putchar('\n');
        exit(1);
    }

    char buf[512];
    int n;
    while ((n = read(fd, buf, sizeof(buf))) > 0)
        print(buf, n);

    close(fd);
}
