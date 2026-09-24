#pragma once

#define SHELL_STREAM_ABI 1u
typedef struct {
    char cwd[96];
    void (*putc)(char c, void *ctx);
    void *ctx;
} ShellStream;
typedef struct {
    u32 abi;
    int (*run)(ShellStream *stream, const char *line);
} ShellStreamOps;
