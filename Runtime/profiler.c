#define _POSIX_C_SOURCE 200809L
#include "profiler.h"
#include "context.h"
#include "../VM/vm.h"
#include <stdio.h>
#include <time.h>

static const char *vm_hotspot_name(VMProfileHotspot hotspot) {
    switch (hotspot) {
        case VM_HOT_GLOBAL_READ: return "global_reads";
        case VM_HOT_GLOBAL_WRITE: return "global_writes";
        case VM_HOT_FIELD_READ: return "field_reads";
        case VM_HOT_FIELD_WRITE: return "field_writes";
        case VM_HOT_NAMED_CALL: return "named_calls";
        case VM_HOT_METHOD_CALL: return "method_calls";
        case VM_HOT_NATIVE_CALL: return "native_calls";
        case VM_HOT_IMPORT: return "imports";
        case VM_HOT_MAX: break;
    }
    return "unknown";
}

void profiler_enable(void) {
    MKSContext *ctx = mks_context_current();
    ctx->profiler_enabled = 1;
    for (int i = 0; i < 64; i++) ctx->profiler_counts[i] = 0;
    for (int i = 0; i < 64; i++) ctx->profiler_vm_opcode_counts[i] = 0;
    for (int i = 0; i < 16; i++) ctx->profiler_vm_hot_counts[i] = 0;
    clock_gettime(CLOCK_MONOTONIC, &ctx->profiler_start);
}

int profiler_is_enabled(void) {
    return mks_context_current()->profiler_enabled;
}

void profiler_on_eval(ASTNodeType type) {
    MKSContext *ctx = mks_context_current();
    if (!ctx->profiler_enabled) return;
    if (type >= 0 && type < 64) ctx->profiler_counts[type]++;
}

void profiler_on_vm_opcode(int opcode) {
    MKSContext *ctx = mks_context_current();
    if (!ctx->profiler_enabled) return;
    if (opcode >= 0 && opcode < 64) ctx->profiler_vm_opcode_counts[opcode]++;
}

void profiler_on_vm_hotspot(VMProfileHotspot hotspot) {
    MKSContext *ctx = mks_context_current();
    if (!ctx->profiler_enabled) return;
    if (hotspot >= 0 && hotspot < VM_HOT_MAX) ctx->profiler_vm_hot_counts[hotspot]++;
}

void profiler_report(void) {
    MKSContext *ctx = mks_context_current();
    if (!ctx->profiler_enabled) return;
    struct timespec t_end;
    clock_gettime(CLOCK_MONOTONIC, &t_end);
    double dur_ms = (t_end.tv_sec - ctx->profiler_start.tv_sec) * 1000.0
                  + (t_end.tv_nsec - ctx->profiler_start.tv_nsec) / 1.0e6;

    unsigned long total = 0;
    for (int i = 0; i < 64; i++) total += ctx->profiler_counts[i];
    unsigned long vm_total = 0;
    for (int i = 0; i < 64; i++) vm_total += ctx->profiler_vm_opcode_counts[i];

    printf("\n[MKS Profile]\n");
    printf("  time: %.3f ms\n", dur_ms);
    printf("  ast nodes executed: %lu\n", total);
    printf("  hot nodes (count > 0):\n");
    for (int i = 0; i < 64; i++) {
        if (ctx->profiler_counts[i] > 0) {
            printf("    type %d : %lu\n", i, ctx->profiler_counts[i]);
        }
    }
    printf("  vm opcodes executed: %lu\n", vm_total);
    printf("  hot opcodes (count > 0):\n");
    for (int i = 0; i < 64; i++) {
        if (ctx->profiler_vm_opcode_counts[i] > 0) {
            printf("    %s : %lu\n", vm_opcode_name((OpCode)i), ctx->profiler_vm_opcode_counts[i]);
        }
    }
    printf("  vm hotspots (count > 0):\n");
    for (int i = 0; i < VM_HOT_MAX; i++) {
        if (ctx->profiler_vm_hot_counts[i] > 0) {
            printf("    %s : %lu\n", vm_hotspot_name((VMProfileHotspot)i), ctx->profiler_vm_hot_counts[i]);
        }
    }
}
