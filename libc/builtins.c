#include <stdint.h>

int __popcountdi2(int64_t a)
{
    uint64_t x = (uint64_t)a;
    int count = 0;
    while (x) {
        count += x & 1;
        x >>= 1;
    }
    return count;
}
