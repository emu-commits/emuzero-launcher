// SPDX-License-Identifier: MIT
#define _GNU_SOURCE
#include "sys.h"
#include "paths.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

int sys_read(const char *path, char *buf, size_t n)
{
	char p[512];
	FILE *f = fopen(path_join(p, sizeof(p), path), "r");
	if (!f) return -1;
	int ok = fgets(buf, (int)n, f) != NULL;
	fclose(f);
	if (!ok) return -1;
	buf[strcspn(buf, "\n")] = 0;
	return 0;
}

long sys_read_long(const char *path, long def)
{
	char b[32], *end;
	if (sys_read(path, b, sizeof(b))) return def;
	long v = strtol(b, &end, 10);
	return end == b ? def : v;
}

int sys_run(char *const argv[], char *out, size_t n)
{
	int fd[2];
	if (n) out[0] = 0;
	if (pipe2(fd, O_CLOEXEC)) return -1;
	pid_t pid = fork();
	if (pid < 0) { close(fd[0]); close(fd[1]); return -1; }
	if (pid == 0) {
		int nul = open("/dev/null", O_RDONLY);
		if (nul >= 0) dup2(nul, 0);
		dup2(fd[1], 1);
		dup2(fd[1], 2);
		execvp(argv[0], argv);
		_exit(127);
	}
	close(fd[1]);
	size_t got = 0;
	char drop[256];
	for (;;) {
		ssize_t r = got + 1 < n ? read(fd[0], out + got, n - 1 - got) : read(fd[0], drop, sizeof(drop));
		if (r < 0 && errno == EINTR) continue;
		if (r <= 0) break;
		if (got + 1 < n) got += (size_t)r;
	}
	if (n) out[got] = 0;
	close(fd[0]);
	int st;
	while (waitpid(pid, &st, 0) < 0)
		if (errno != EINTR) return -1;
	return WIFEXITED(st) ? WEXITSTATUS(st) : 128 + WTERMSIG(st);
}
