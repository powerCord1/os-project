#include <math.h>
#include <stdbool.h>
#include <stddef.h>

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