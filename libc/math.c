#include <math.h>
#include <stddef.h>
#include <stdbool.h>

size_t round_to_even(size_t n, bool roundUp) {
	if ((n & 1) != 0) return roundUp ? n + 1 : n - 1;
	return n;
}

size_t floordiv2(size_t n) {
    if ((n & 1) != 0) n--;
	return n / 2;
}