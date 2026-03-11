#include "api.h"

void _start(void)
{
    puts("Hello from WebAssembly!\n");
    puts("Running in ring 0, no context switch.\n");

    unsigned long long ticks = get_ticks();
    puts("PIT ticks: ");
    print_num((int)ticks);
    putchar('\n');
}
