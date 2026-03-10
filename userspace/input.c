#include "api.h"

void _start(void)
{
    puts("Type something: ");
    char buf[256];
    int len = read_line(buf, sizeof(buf));
    puts("You typed: ");
    print(buf, len);
    putchar('\n');
}
