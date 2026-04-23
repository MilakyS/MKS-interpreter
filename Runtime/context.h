#ifndef MKS_RUNTIME_CONTEXT_H
#define MKS_RUNTIME_CONTEXT_H

#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>
#include <time.h>

#include "../GC/gc.h"

#ifndef MKS_PATH_MAX
#define MKS_PATH_MAX 4096
#endif

typedef struct MKSContext {
    GarbageCollector gc;

    char lexer_error_hint[128];

    char current_file[MKS_PATH_MAX];
    const char *current_source;
    int current_line;

    int debug_mode;

    void *module_native_registry;
    void *module_loaded_modules;
    void *module_programs;
    struct Environment *module_parent_env;

    void *watch_head;
    void *extension_tables;
    struct Environment *fs_module_env;

    size_t env_shape_epoch;
    int eval_depth;

    int profiler_enabled;
    unsigned long profiler_counts[64];
    struct timespec profiler_start;

    uint64_t random_state;

    jmp_buf error_jmp;
    int error_active;
    int error_status;
} MKSContext;

MKSContext *mks_context_current(void);
void mks_context_set_current(MKSContext *ctx);
void mks_context_init(MKSContext *ctx, size_t initial_gc_threshold);
void mks_context_dispose(MKSContext *ctx);
void mks_context_abort(int status);
MKSContext *mks_context_default(void);

#endif
