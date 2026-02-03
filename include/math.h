#include <stdbool.h>
#include <stddef.h>

#define M_PI 3.14159265358979323846

// Return the absolute value of `n`
int abs(int n);

// Return the absolute value of a floating-point number
double fabs(double x);

// Calculate the floating-point remainder of `x / y`
double fmod(double x, double y);

// `sin` implementation using Taylor series expansion
double sin(double x);

// `cos` implementation using Taylor series expansion
double cos(double x);

// Seeded random number generator
void srand(unsigned int seed);

// Random number generator
size_t rand(void);

// Return a random number in between `min` and `max`
size_t random_range(size_t min, size_t max);