#include <math.h>
#include <stdbool.h>

int round_to_even(int n, bool roundUp) {
	if ((n & 1) != 0) return roundUp ? n + 1 : n - 1;
	return n;
}

double floor(double n) {
    // TODO: add floor function
}