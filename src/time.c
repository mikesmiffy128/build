#include <time.h>

#include <intdefs.h>

vlong time_now(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

// vi: sw=4 ts=4 noet tw=80 cc=80
