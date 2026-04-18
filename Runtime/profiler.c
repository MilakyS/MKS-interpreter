#define _POSIX_C_SOURCE 200809L
#include "profiler.h"
#include <stdio.h>
#include <time.h>

int mks_profiler_enabled = 0;
static unsigned long counts[64];
static struct timespec t_start;

void profiler_enable(void) {
    mks_profiler_enabled = 1;
    for (int i = 0; i < 64; i++) counts[i] = 0;
    clock_gettime(CLOCK_MONOTONIC, &t_start);
}

int profiler_is_enabled(void) {
    return mks_profiler_enabled;
}

void profiler_on_eval(ASTNodeType type) {
    if (!mks_profiler_enabled) return;
    if (type >= 0 && type < 64) counts[type]++;
}

void profiler_report(void) {
    if (!mks_profiler_enabled) return;
    struct timespec t_end;
    clock_gettime(CLOCK_MONOTONIC, &t_end);
    double dur_ms = (t_end.tv_sec - t_start.tv_sec) * 1000.0
                  + (t_end.tv_nsec - t_start.tv_nsec) / 1.0e6;

    unsigned long total = 0;
    for (int i = 0; i < 64; i++) total += counts[i];

    printf("\n[MKS Profile]\n");
    printf("  time: %.3f ms\n", dur_ms);
    printf("  ast nodes executed: %lu\n", total);
    printf("  hot nodes (count > 0):\n");
    for (int i = 0; i < 64; i++) {
        if (counts[i] > 0) {
            printf("    type %d : %lu\n", i, counts[i]);
        }
    }
}
