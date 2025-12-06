#include <debug.h>
#include <math.h>
#include <pit.h>
#include <stdint.h>

static unsigned long int next = 1;

void srand(unsigned int seed)
{
    next = seed;
}

size_t rand(void)
{
    next = next * 1103515245 + 12345;
    return (unsigned int)((next / 65536) % 2147483647);
}

size_t random_range(size_t min, size_t max)
{
    if (next == 1) {
        srand(pit_ticks);
    }
    if (min > max) {
        log_err("random_range: min cannot be greater than max");
        return 0;
    }
    return min + rand() % (max - min + 1);
}