#include <stdbool.h>
#include <stddef.h>

#include <debug.h>
#include <math.h>
#include <pit.h>

size_t floordiv2(size_t n)
{
    if ((n & 1) != 0) {
        n--;
    }
    return n / 2;
}

int abs(int n)
{
    return (n < 0) ? -n : n;
}

double fabs(double x)
{
    return (x < 0.0) ? -x : x;
}

double fmod(double x, double y)
{
    if (y == 0.0) {
        log_err("fmod: division by zero");
        return 0.0;
    }
    return x - (long)(x / y) * y;
}