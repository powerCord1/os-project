#include <stdbool.h>
#include <stddef.h>

#include <debug.h>
#include <math.h>
#include <pit.h>

size_t round_to_even(size_t n, bool roundUp)
{
    if ((n & 1) != 0) {
        return roundUp ? n + 1 : n - 1;
    }
    return n;
}

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

// used by sin() for range reduction
static double reduce_angle(double angle)
{
    double twopi = 2.0 * PI;
    while (angle >= twopi) {
        angle -= twopi;
    }
    while (angle < 0) {
        angle += twopi;
    }

    return angle;
}

// sin implementation using Taylor series expansion
double sin(double x)
{
    x = reduce_angle(x);

    double result = 0.0;
    double term = x;
    int n = 1;

    for (int i = 0; i < 10; ++i) {
        result += term;
        n += 2;
        term *= -(x * x) / (double)(n * (n - 1));
    }

    return result;
}

// cos implementation using sin()
double cos(double x)
{
    return sin(PI / 2.0 - x);
}

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