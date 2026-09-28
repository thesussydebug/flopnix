#pragma once

#define SHELL_TERM_ABI 1u
typedef struct {
    u32 abi;
    int (*exec)(const char *line);
    void (*print)(const char *s);
    int (*fx)(int mode);
    const char *(*hist)(int i);
} ShellTermOps;
