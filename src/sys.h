// SPDX-License-Identifier: MIT
// Small system helpers: read a value from a file, run one of NimbleZero's programs.
#pragma once
#include <stddef.h>

/* first line of root + path into buf (newline removed); 0 on success */
int sys_read(const char *path, char *buf, size_t n);
/* the number in the first line of root + path, or def */
long sys_read_long(const char *path, long def);
/* Run argv[0] (looked up in PATH) with argv, no shell. Its output (stdout and stderr) goes
 * into out (cut to n - 1 bytes), stdin is /dev/null. Returns the exit status, 128 + signal
 * when it was killed, or -1 when it could not be run. */
int sys_run(char *const argv[], char *out, size_t n);
