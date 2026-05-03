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
    int vm_mode;
    int vm_dump_bytecode;
    void *vm_chunk_registry;

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
    int profiler_level;
    unsigned long profiler_counts[64];
    unsigned long profiler_vm_opcode_counts[64];
    unsigned long profiler_vm_hot_counts[16];
    struct timespec profiler_start;
    void *profiler_data;  // MksProfiler*

    uint64_t random_state;

    int cli_argc;
    char **cli_argv;

    jmp_buf error_jmp;
    int error_active;
    int abort_requested;
    int error_status;
} MKSContext;

MKSContext *mks_context_current(void);
void mks_context_set_current(MKSContext *ctx);
void mks_context_init(MKSContext *ctx, size_t initial_gc_threshold);
void mks_context_dispose(MKSContext *ctx);
void mks_context_abort(int status);
void mks_context_set_cli_args(MKSContext *ctx, int argc, char **argv);
MKSContext *mks_context_default(void);

#endif
