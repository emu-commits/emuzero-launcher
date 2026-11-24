// SPDX-License-Identifier: MIT
// tstart PROG ARGS... : exec PROG with T0_NS = CLOCK_MONOTONIC just before execve, so PROG can
// report its time to first frame including the dynamic loader and library start-up.
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

int main(int argc, char **argv)
{
	struct timespec t; char v[32];
	if (argc < 2) { fprintf(stderr, "usage: tstart PROG [ARGS...]\n"); return 2; }
	clock_gettime(CLOCK_MONOTONIC, &t);
	snprintf(v, sizeof(v), "%lld", (long long)t.tv_sec * 1000000000LL + t.tv_nsec);
	setenv("T0_NS", v, 1);
	execvp(argv[1], argv + 1);
	perror(argv[1]);
	return 127;
}
