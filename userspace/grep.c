#include "api.h"

static int strstr_match(const char *haystack, const char *needle, int nlen)
{
    for (int i = 0; haystack[i]; i++) {
        int j;
        for (j = 0; j < nlen; j++) {
            if (haystack[i + j] != needle[j])
                break;
        }
        if (j == nlen)
            return 1;
    }
    return 0;
}

void _start(void)
{
    char pattern[256];
    if (get_argc() < 2 || get_argv(1, pattern, sizeof(pattern)) < 0) {
        puts("usage: grep <pattern>\n");
        exit(1);
    }
    int plen = strlen(pattern);

    char buf[512];
    int line_pos = 0;
    int found = 0;

    while (1) {
        char c;
        int n = read(0, &c, 1);
        if (n <= 0) {
            if (line_pos > 0) {
                buf[line_pos] = '\0';
                if (strstr_match(buf, pattern, plen)) {
                    write(1, buf, line_pos);
                    write(1, "\n", 1);
                    found = 1;
                }
            }
            break;
        }
        if (c == '\n') {
            buf[line_pos] = '\0';
            if (strstr_match(buf, pattern, plen)) {
                write(1, buf, line_pos);
                write(1, "\n", 1);
                found = 1;
            }
            line_pos = 0;
        } else if (line_pos < (int)sizeof(buf) - 1) {
            buf[line_pos++] = c;
        }
    }

    exit(found ? 0 : 1);
}
