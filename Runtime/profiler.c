#define _POSIX_C_SOURCE 200809L
#include "profiler.h"
#include "context.h"
#include "../VM/vm.h"
#include "../GC/gc.h"
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>

static uint64_t timespec_to_ns(struct timespec ts) {
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

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

static MksProfiler *get_profiler(void) {
    MKSContext *ctx = mks_context_current();
    if (!ctx->profiler_data) {
        ctx->profiler_data = malloc(sizeof(MksProfiler));
        memset(ctx->profiler_data, 0, sizeof(MksProfiler));
    }
    return (MksProfiler *)ctx->profiler_data;
}

void profiler_enable(ProfileLevel level) {
    MKSContext *ctx = mks_context_current();
    ctx->profiler_enabled = 1;
    ctx->profiler_level = level;

    MksProfiler *prof = get_profiler();
    memset(prof, 0, sizeof(MksProfiler));

    clock_gettime(CLOCK_MONOTONIC, &prof->start_time);
    ctx->profiler_start = prof->start_time;

    for (int i = 0; i < 96; i++) ctx->profiler_counts[i] = 0;
    for (int i = 0; i < 96; i++) ctx->profiler_vm_opcode_counts[i] = 0;
    for (int i = 0; i < 16; i++) ctx->profiler_vm_hot_counts[i] = 0;
}

int profiler_is_enabled(void) {
    return mks_context_current()->profiler_enabled;
}

ProfileLevel profiler_get_level(void) {
    return (ProfileLevel)mks_context_current()->profiler_level;
}

void profiler_on_eval(ASTNodeType type) {
    MKSContext *ctx = mks_context_current();
    if (!ctx->profiler_enabled) return;
    if (type >= 0 && type < 64) ctx->profiler_counts[type]++;
}

void profiler_on_vm_opcode(int opcode) {
    MKSContext *ctx = mks_context_current();
    if (!ctx->profiler_enabled) return;
    if (opcode >= 0 && opcode < 96) {
        ctx->profiler_vm_opcode_counts[opcode]++;
        MksProfiler *prof = get_profiler();
        prof->opcodes[opcode].count++;
        prof->instructions++;
    }
}

void profiler_on_vm_opcode_timed(int opcode, uint64_t duration_ns) {
    MKSContext *ctx = mks_context_current();
    if (!ctx->profiler_enabled || ctx->profiler_level != PROFILE_DETAILED) return;
    if (opcode >= 0 && opcode < 78) {
        MksProfiler *prof = get_profiler();
        prof->opcodes[opcode].total_ns += duration_ns;
        if (duration_ns > prof->opcodes[opcode].max_ns) {
            prof->opcodes[opcode].max_ns = (uint32_t)duration_ns;
        }
    }
}

void profiler_on_vm_hotspot(VMProfileHotspot hotspot) {
    MKSContext *ctx = mks_context_current();
    if (!ctx->profiler_enabled) return;
    if (hotspot >= 0 && hotspot < VM_HOT_MAX) {
        ctx->profiler_vm_hot_counts[hotspot]++;
        MksProfiler *prof = get_profiler();
        prof->hotspot_counts[hotspot]++;
    }
}

void profiler_on_function_call(void) {
    MKSContext *ctx = mks_context_current();
    if (!ctx->profiler_enabled) return;
    MksProfiler *prof = get_profiler();
    prof->total_function_calls++;
}

void profiler_on_method_call(void) {
    MKSContext *ctx = mks_context_current();
    if (!ctx->profiler_enabled) return;
    MksProfiler *prof = get_profiler();
    prof->total_method_calls++;
}

void profiler_on_native_call(void) {
    MKSContext *ctx = mks_context_current();
    if (!ctx->profiler_enabled) return;
    MksProfiler *prof = get_profiler();
    prof->total_native_calls++;
}

void profiler_on_allocation(int type, uint64_t size) {
    (void)size;
    MKSContext *ctx = mks_context_current();
    if (!ctx->profiler_enabled) return;
    MksProfiler *prof = get_profiler();
    prof->allocations++;
    if (type >= 0 && type < 16) {
        prof->alloc_by_type[type]++;
    }
}

void profiler_on_builder_start(void) {
    MKSContext *ctx = mks_context_current();
    if (!ctx->profiler_enabled) return;
    MksProfiler *prof = get_profiler();
    prof->builder_loops_optimized++;
}

void profiler_on_builder_append(uint64_t bytes) {
    MKSContext *ctx = mks_context_current();
    if (!ctx->profiler_enabled) return;
    MksProfiler *prof = get_profiler();
    prof->builder_appends++;
    prof->bytes_appended_via_builder += bytes;
}

void profiler_on_builder_finish(void) {
    MKSContext *ctx = mks_context_current();
    if (!ctx->profiler_enabled) return;
    MksProfiler *prof = get_profiler();
    prof->builder_materializations++;
}

void profiler_on_module_load(int from_cache) {
    MKSContext *ctx = mks_context_current();
    if (!ctx->profiler_enabled) return;
    MksProfiler *prof = get_profiler();
    if (from_cache) {
        prof->modules_from_cache++;
    } else {
        prof->modules_loaded++;
    }
}

void profiler_on_array_access(void) {
    MKSContext *ctx = mks_context_current();
    if (!ctx->profiler_enabled) return;
    MksProfiler *prof = get_profiler();
    prof->array_index_ops++;
}

void profiler_on_field_access(void) {
    MKSContext *ctx = mks_context_current();
    if (!ctx->profiler_enabled) return;
    MksProfiler *prof = get_profiler();
    prof->field_access_ops++;
}

void profiler_on_loop_iteration(void) {
    MKSContext *ctx = mks_context_current();
    if (!ctx->profiler_enabled) return;
    MksProfiler *prof = get_profiler();
    prof->loop_iterations_total++;
}

void profiler_on_slot_type_hit(void) {
    MKSContext *ctx = mks_context_current();
    if (!ctx->profiler_enabled) return;
    MksProfiler *prof = get_profiler();
    prof->slot_type_hits++;
}

void profiler_on_slot_type_miss(void) {
    MKSContext *ctx = mks_context_current();
    if (!ctx->profiler_enabled) return;
    MksProfiler *prof = get_profiler();
    prof->slot_type_misses++;
}

void profiler_on_branch_taken(void) {
    MKSContext *ctx = mks_context_current();
    if (!ctx->profiler_enabled) return;
    MksProfiler *prof = get_profiler();
    prof->branch_taken++;
}

void profiler_on_branch_not_taken(void) {
    MKSContext *ctx = mks_context_current();
    if (!ctx->profiler_enabled) return;
    MksProfiler *prof = get_profiler();
    prof->branch_not_taken++;
}

static void profiler_finalize(void) {
    MKSContext *ctx = mks_context_current();
    if (!ctx->profiler_enabled) return;

    MksProfiler *prof = get_profiler();
    clock_gettime(CLOCK_MONOTONIC, &prof->end_time);

    uint64_t start_ns = timespec_to_ns(prof->start_time);
    uint64_t end_ns = timespec_to_ns(prof->end_time);
    prof->total_time_ns = end_ns - start_ns;

    GarbageCollector *gc = &ctx->gc;
    prof->peak_heap_bytes = gc->threshold;
    prof->final_heap_bytes = gc->allocated_bytes;
}

static void format_bytes(uint64_t bytes, char *buf, size_t bufsize) {
    if (bytes < 1024) {
        snprintf(buf, bufsize, "%luB", bytes);
    } else if (bytes < 1024 * 1024) {
        snprintf(buf, bufsize, "%.1fKB", bytes / 1024.0);
    } else {
        snprintf(buf, bufsize, "%.1fMB", bytes / (1024.0 * 1024.0));
    }
}

static void profiler_report_summary(MksProfiler *prof) {
    double total_ms = prof->total_time_ns / 1.0e6;

    printf("\n[MKS Profile Summary]\n");
    printf("total_time=%.1f ms\n", total_ms);
    printf("instructions=%lu\n", prof->instructions);
    printf("allocations=%lu\n", prof->allocations);

    char heap_str[32], peak_str[32];
    format_bytes(prof->final_heap_bytes, heap_str, sizeof(heap_str));
    format_bytes(prof->peak_heap_bytes, peak_str, sizeof(peak_str));
    printf("final_heap=%s peak_heap=%s\n", heap_str, peak_str);

    if (prof->total_function_calls > 0) {
        printf("function_calls=%lu method_calls=%lu native_calls=%lu\n",
               prof->total_function_calls, prof->total_method_calls,
               prof->total_native_calls);
    }

    if (prof->builder_loops_optimized > 0) {
        uint64_t concat_avoided = prof->builder_appends - prof->builder_materializations;
        printf("\nString Builder: loops=%lu appends=%lu bytes=%lu concat_avoided=%lu\n",
               prof->builder_loops_optimized, prof->builder_appends,
               prof->bytes_appended_via_builder, concat_avoided);
    }

    GarbageCollector *gc = &mks_context_current()->gc;
    if (gc->collections > 0 || gc->young_collections > 0) {
        printf("\n[GC]\n");
        printf("young_collections=%zu full_collections=%zu\n",
               gc->young_collections, gc->collections);
        printf("freed_objects=%zu freed_bytes=%zu\n",
               gc->freed_objects, gc->freed_bytes);
        printf("promoted_to_survivor=%zu promoted_to_stable=%zu\n",
               gc->promoted_to_survivor, gc->promoted_to_stable);
        printf("barrier_calls=%zu old_to_young_edges=%zu\n",
               gc->barrier_calls, gc->old_to_young_edges);
    }
}

static void profiler_report_opcodes(MksProfiler *prof) {
    printf("\n[Top Opcodes]\n");

    typedef struct {
        int index;
        uint64_t count;
    } OpcodeCount;

    OpcodeCount sorted[78];
    int count = 0;

    for (int i = 0; i < 78; i++) {
        if (prof->opcodes[i].count > 0) {
            sorted[count].index = i;
            sorted[count].count = prof->opcodes[i].count;
            count++;
        }
    }

    for (int i = 0; i < count - 1; i++) {
        for (int j = i + 1; j < count; j++) {
            if (sorted[j].count > sorted[i].count) {
                OpcodeCount tmp = sorted[i];
                sorted[i] = sorted[j];
                sorted[j] = tmp;
            }
        }
    }

    int limit = count < 15 ? count : 15;
    for (int i = 0; i < limit; i++) {
        int idx = sorted[i].index;
        uint64_t cnt = sorted[i].count;
        double pct = (prof->instructions > 0) ? (100.0 * cnt / prof->instructions) : 0;
        printf("  %-30s count=%-10lu (%.1f%%)\n",
               vm_opcode_name((OpCode)idx), cnt, pct);
    }
}

static void profiler_report_hotspots(MksProfiler *prof) {
    if (prof->total_method_calls == 0 && prof->total_native_calls == 0 &&
        prof->hotspot_counts[VM_HOT_GLOBAL_READ] == 0) {
        return;
    }

    printf("\n[Hotspots]\n");
    for (int i = 0; i < VM_HOT_MAX; i++) {
        if (prof->hotspot_counts[i] > 0) {
            double pct = (prof->instructions > 0) ?
                (100.0 * prof->hotspot_counts[i] / prof->instructions) : 0;
            printf("  %-25s count=%-10lu (%.1f%%)\n",
                   vm_hotspot_name((VMProfileHotspot)i),
                   prof->hotspot_counts[i], pct);
        }
    }
}

static void profiler_report_allocations(MksProfiler *prof) {
    if (prof->allocations == 0) return;

    printf("\n[Allocations]\n");
    const char *type_names[] = {
        "UNKNOWN", "STRING", "ARRAY", "OBJECT", "FUNCTION", "BLUEPRINT",
        "ENV", "CLOSURE", "STRING_BUILDER", "MODULE", "EXTENSION",
        "WATCH", "PROMISE", "STREAM", "INTERNAL", "COUNT"
    };

    for (int i = 1; i < 15; i++) {
        if (prof->alloc_by_type[i] > 0) {
            printf("  %-20s count=%lu\n", type_names[i], prof->alloc_by_type[i]);
        }
    }
}

static void profiler_report_json(MksProfiler *prof) {
    double total_ms = prof->total_time_ns / 1.0e6;

    printf("{\n");
    printf("  \"summary\": {\n");
    printf("    \"total_time_ms\": %.2f,\n", total_ms);
    printf("    \"instructions\": %lu,\n", prof->instructions);
    printf("    \"allocations\": %lu,\n", prof->allocations);
    printf("    \"final_heap_bytes\": %lu,\n", prof->final_heap_bytes);
    printf("    \"peak_heap_bytes\": %lu\n", prof->peak_heap_bytes);
    printf("  },\n");

    printf("  \"opcodes\": {\n");
    int first = 1;
    for (int i = 0; i < 78; i++) {
        if (prof->opcodes[i].count > 0) {
            if (!first) printf(",\n");
            printf("    \"%s\": {\"count\": %lu}", vm_opcode_name((OpCode)i),
                   prof->opcodes[i].count);
            first = 0;
        }
    }
    printf("\n  },\n");

    printf("  \"hotspots\": {\n");
    first = 1;
    for (int i = 0; i < VM_HOT_MAX; i++) {
        if (prof->hotspot_counts[i] > 0) {
            if (!first) printf(",\n");
            printf("    \"%s\": %lu", vm_hotspot_name((VMProfileHotspot)i),
                   prof->hotspot_counts[i]);
            first = 0;
        }
    }
    printf("\n  },\n");

    GarbageCollector *gc = &mks_context_current()->gc;
    printf("  \"gc\": {\n");
    printf("    \"young_collections\": %zu,\n", gc->young_collections);
    printf("    \"full_collections\": %zu,\n", gc->collections);
    printf("    \"freed_objects\": %zu,\n", gc->freed_objects);
    printf("    \"freed_bytes\": %zu,\n", gc->freed_bytes);
    printf("    \"promoted_to_survivor\": %zu,\n", gc->promoted_to_survivor);
    printf("    \"promoted_to_stable\": %zu\n", gc->promoted_to_stable);
    printf("  }\n");
    printf("}\n");
}

static void profiler_report_predictions(MksProfiler *prof) {
    if (prof->slot_type_hits == 0 && prof->slot_type_misses == 0 &&
        prof->branch_taken == 0 && prof->branch_not_taken == 0) {
        return;
    }

    printf("\n[Prediction Summary]\n");

    if (prof->slot_type_hits > 0 || prof->slot_type_misses > 0) {
        uint64_t total = prof->slot_type_hits + prof->slot_type_misses;
        double hit_pct = (total > 0) ? (100.0 * prof->slot_type_hits / total) : 0;
        printf("slot_type_hits=%lu slot_type_misses=%lu (%.1f%% hit rate)\n",
               prof->slot_type_hits, prof->slot_type_misses, hit_pct);
    }

    if (prof->branch_taken > 0 || prof->branch_not_taken > 0) {
        uint64_t total = prof->branch_taken + prof->branch_not_taken;
        double taken_pct = (total > 0) ? (100.0 * prof->branch_taken / total) : 0;
        printf("branch_taken=%lu branch_not_taken=%lu (%.1f%% taken)\n",
               prof->branch_taken, prof->branch_not_taken, taken_pct);
    }

    if (prof->ic_hits > 0 || prof->ic_misses > 0) {
        uint64_t total = prof->ic_hits + prof->ic_misses;
        double hit_pct = (total > 0) ? (100.0 * prof->ic_hits / total) : 0;
        printf("ic_hits=%lu ic_misses=%lu (%.1f%% hit rate)\n",
               prof->ic_hits, prof->ic_misses, hit_pct);
    }
}

static void profiler_report_hotspots_mode(MksProfiler *prof) {
    printf("\n[Top Hotspots]\n");

    typedef struct {
        const char *name;
        uint64_t count;
        int is_opcode;
        int index;
    } Hotspot;

    Hotspot all[78 + VM_HOT_MAX];
    int count = 0;

    for (int i = 0; i < 78; i++) {
        if (prof->opcodes[i].count > 100) {
            all[count].name = vm_opcode_name((OpCode)i);
            all[count].count = prof->opcodes[i].count;
            all[count].is_opcode = 1;
            all[count].index = i;
            count++;
        }
    }

    for (int i = 0; i < VM_HOT_MAX; i++) {
        if (prof->hotspot_counts[i] > 0) {
            all[count].name = vm_hotspot_name((VMProfileHotspot)i);
            all[count].count = prof->hotspot_counts[i];
            all[count].is_opcode = 0;
            all[count].index = i;
            count++;
        }
    }

    for (int i = 0; i < count - 1; i++) {
        for (int j = i + 1; j < count; j++) {
            if (all[j].count > all[i].count) {
                Hotspot tmp = all[i];
                all[i] = all[j];
                all[j] = tmp;
            }
        }
    }

    int limit = count < 20 ? count : 20;
    for (int i = 0; i < limit; i++) {
        double pct = (prof->instructions > 0) ?
            (100.0 * all[i].count / prof->instructions) : 0;
        printf("  %-35s %10lu (%.1f%%)\n", all[i].name, all[i].count, pct);
    }
}

void profiler_report(void) {
    MKSContext *ctx = mks_context_current();
    if (!ctx->profiler_enabled) return;

    profiler_finalize();
    MksProfiler *prof = get_profiler();

    switch ((ProfileLevel)ctx->profiler_level) {
        case PROFILE_COMPACT:
            profiler_report_summary(prof);
            profiler_report_opcodes(prof);
            profiler_report_hotspots(prof);
            profiler_report_predictions(prof);
            break;

        case PROFILE_DETAILED:
            profiler_report_summary(prof);
            printf("\n[VM Opcodes]\n");
            profiler_report_opcodes(prof);
            profiler_report_hotspots(prof);
            profiler_report_allocations(prof);
            profiler_report_predictions(prof);
            break;

        case PROFILE_JSON:
            profiler_report_json(prof);
            break;

        case PROFILE_HOTSPOTS:
            profiler_report_hotspots_mode(prof);
            break;

        default:
            break;
    }

    if (ctx->profiler_data) {
        free(ctx->profiler_data);
        ctx->profiler_data = NULL;
    }
}
