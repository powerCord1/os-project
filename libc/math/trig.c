#include <math.h>

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