#define _POSIX_C_SOURCE 200809L
#include "profiler.h"
#include "context.h"
#include <stdio.h>
#include <time.h>

void profiler_enable(void) {
    MKSContext *ctx = mks_context_current();
    ctx->profiler_enabled = 1;
    for (int i = 0; i < 64; i++) ctx->profiler_counts[i] = 0;
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

void profiler_report(void) {
    MKSContext *ctx = mks_context_current();
    if (!ctx->profiler_enabled) return;
    struct timespec t_end;
    clock_gettime(CLOCK_MONOTONIC, &t_end);
    double dur_ms = (t_end.tv_sec - ctx->profiler_start.tv_sec) * 1000.0
                  + (t_end.tv_nsec - ctx->profiler_start.tv_nsec) / 1.0e6;

    unsigned long total = 0;
    for (int i = 0; i < 64; i++) total += ctx->profiler_counts[i];

    printf("\n[MKS Profile]\n");
    printf("  time: %.3f ms\n", dur_ms);
    printf("  ast nodes executed: %lu\n", total);
    printf("  hot nodes (count > 0):\n");
    for (int i = 0; i < 64; i++) {
        if (ctx->profiler_counts[i] > 0) {
            printf("    type %d : %lu\n", i, ctx->profiler_counts[i]);
        }
    }
}
