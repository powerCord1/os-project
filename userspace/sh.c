#include "api.h"

static int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *(unsigned char *)a - *(unsigned char *)b;
}

static char to_upper(char c)
{
    if (c >= 'a' && c <= 'z')
        return c - ('a' - 'A');
    return c;
}

void _start(void)
{
    char line[256];
    char *argv[16];

    while (1) {
        puts("$ ");
        int len = read_line(line, sizeof(line));
        if (len <= 0)
            continue;

        if (strcmp(line, "q") == 0)
            break;

        int argc = 0;
        char *p = line;
        while (*p && argc < 16) {
            while (*p == ' ')
                p++;
            if (!*p)
                break;
            argv[argc++] = p;
            while (*p && *p != ' ')
                p++;
            if (*p)
                *p++ = '\0';
        }

        if (argc == 0)
            continue;

        char path[64] = "/";
        int pi = 1;
        for (int i = 0; argv[0][i] && pi < 60; i++)
            path[pi++] = to_upper(argv[0][i]);
        path[pi++] = '.';
        path[pi++] = 'W';
        path[pi++] = 'M';
        path[pi] = '\0';

        int pid = spawn_cmd(path, argv + 1, argc - 1);
        if (pid < 0) {
            puts(argv[0]);
            puts(": not found\n");
            continue;
        }

        int code = waitpid(pid);
        if (code != 0 && code != 130) {
            puts("exit code: ");
            print_num(code);
            puts("\n");
        }
    }
}
