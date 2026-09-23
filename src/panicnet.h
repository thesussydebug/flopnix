#pragma once
#include "kapi.h"

#define PANIC_MONITOR_ABI 0x324d5046u
typedef struct {
    u32 edi, esi, ebp, esp, ebx, edx, ecx, eax;
    u32 vec, err, eip, cs, eflags;
    u32 ss, cr0, cr2, cr3, cr4;
} PanicCpu;
typedef struct {
    u32 abi;
    void (*capture)(const u32 *frame);
    void (*enter)(void (*describe)(FaultRec *));
    void (*emergency)(u32,u32,u32,u32,u32,u32,u32,u32,u32,u32);
} PanicMonitor;

typedef struct {
    u32 abi;
    const PanicMonitor **monitor;
    const u32 **frame;
    const u32 *controls, *tss;
    const KextInfo *modules;
    const int *module_count;
    const u32 *module_ids, *private_base, *private_size, *arena_rw;
    const char *log, *trace;
    const u32 *layout, *heap_top, *grow_base, *grow_top;
    const FaultRec *faults;
    const u32 *fault_count;
    u32 kernel_end;
    const void *threads, *buffers;
    u32 thread_bytes;
} PanicCore;
