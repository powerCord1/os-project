#pragma once

#include <stdbool.h>
#include <stddef.h>

#define M_PI 3.14159265358979323846
#define NAN __builtin_nanf("")
#define INFINITY __builtin_inff()

#define isnan(x) __builtin_isnan(x)
#define isinf(x) __builtin_isinf(x)
#define signbit(x) __builtin_signbit(x)

int abs(int n);
double fabs(double x);
float fabsf(float x);
double fmod(double x, double y);
double sin(double x);
double cos(double x);
void srand(unsigned int seed);
size_t rand(void);
size_t random_range(size_t min, size_t max);

double ceil(double x);
float ceilf(float x);
double floor(double x);
float floorf(float x);
double trunc(double x);
float truncf(float x);
double sqrt(double x);
float sqrtf(float x);
double rint(double x);
float rintf(float x);
double copysign(double x, double y);
float copysignf(float x, float y);