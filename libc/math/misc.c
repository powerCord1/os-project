#include <stdbool.h>
#include <stddef.h>

#include <debug.h>
#include <math.h>
#include <pit.h>

int abs(int n)
{
    return (n < 0) ? -n : n;
}

double fabs(double x)
{
    return (x < 0.0) ? -x : x;
}

float fabsf(float x)
{
    return (x < 0.0f) ? -x : x;
}

double fmod(double x, double y)
{
    if (y == 0.0) {
        log_err("fmod: division by zero");
        return 0.0;
    }
    return x - (long)(x / y) * y;
}

double ceil(double x)
{
    long i = (long)x;
    if (x > 0.0 && x != (double)i)
        return (double)(i + 1);
    return (double)i;
}

float ceilf(float x)
{
    return (float)ceil((double)x);
}

double floor(double x)
{
    long i = (long)x;
    if (x < 0.0 && x != (double)i)
        return (double)(i - 1);
    return (double)i;
}

float floorf(float x)
{
    return (float)floor((double)x);
}

double trunc(double x)
{
    return (double)(long)x;
}

float truncf(float x)
{
    return (float)(long)x;
}

double sqrt(double x)
{
    if (x < 0.0)
        return NAN;
    if (x == 0.0)
        return 0.0;
    double guess = x / 2.0;
    for (int i = 0; i < 50; i++) {
        guess = (guess + x / guess) / 2.0;
    }
    return guess;
}

float sqrtf(float x)
{
    return (float)sqrt((double)x);
}

double rint(double x)
{
    if (x >= 0.0)
        return floor(x + 0.5);
    return ceil(x - 0.5);
}

float rintf(float x)
{
    return (float)rint((double)x);
}

double copysign(double x, double y)
{
    double ax = fabs(x);
    return (y < 0.0) ? -ax : ax;
}

float copysignf(float x, float y)
{
    float ax = fabsf(x);
    return (y < 0.0f) ? -ax : ax;
}