#ifndef MKS_PROFILER_H
#define MKS_PROFILER_H

#include "../Parser/AST.h"
#include <stdint.h>
#include <time.h>

typedef enum {
    VM_HOT_GLOBAL_READ,
    VM_HOT_GLOBAL_WRITE,
    VM_HOT_FIELD_READ,
    VM_HOT_FIELD_WRITE,
    VM_HOT_NAMED_CALL,
    VM_HOT_METHOD_CALL,
    VM_HOT_NATIVE_CALL,
    VM_HOT_IMPORT,
    VM_HOT_MAX
} VMProfileHotspot;

typedef enum {
    PROFILE_DISABLED,
    PROFILE_COMPACT,    // --profile
    PROFILE_DETAILED,   // --vm-profile
    PROFILE_JSON,       // --profile-json
    PROFILE_HOTSPOTS    // --profile-hot
} ProfileLevel;

typedef struct {
    uint64_t count;
    uint64_t total_ns;
    uint32_t max_ns;
} OpcodeStats;

typedef struct {
    uint64_t total_time_ns;
    uint64_t parse_time_ns;
    uint64_t compile_time_ns;
    uint64_t vm_time_ns;
    uint64_t gc_time_ns;
    uint64_t native_time_ns;
    uint64_t instructions;
    uint64_t allocations;
    uint64_t peak_heap_bytes;
    uint64_t final_heap_bytes;

    OpcodeStats opcodes[96];  // Headroom for future opcodes

    uint64_t total_function_calls;
    uint64_t total_method_calls;
    uint64_t total_native_calls;

    uint64_t alloc_by_type[16];

    uint64_t loop_count;
    uint64_t loop_iterations_total;

    uint64_t builder_loops_optimized;
    uint64_t builder_appends;
    uint64_t builder_materializations;
    uint64_t bytes_appended_via_builder;

    uint64_t modules_loaded;
    uint64_t modules_from_cache;

    uint64_t array_index_ops;
    uint64_t field_access_ops;
    uint64_t method_lookups;

    uint64_t orbit_young_objects;
    uint64_t orbit_survivor_objects;
    uint64_t orbit_stable_objects;
    uint64_t orbit_hot_objects;

    uint64_t hotspot_counts[VM_HOT_MAX];

    /* Prediction statistics */
    uint64_t slot_type_hits;
    uint64_t slot_type_misses;
    uint64_t branch_taken;
    uint64_t branch_not_taken;
    uint64_t ic_hits;
    uint64_t ic_misses;

    struct timespec start_time;
    struct timespec end_time;
} MksProfiler;

void profiler_enable(ProfileLevel level);
int  profiler_is_enabled(void);
ProfileLevel profiler_get_level(void);
void profiler_on_eval(ASTNodeType type);
void profiler_on_vm_opcode(int opcode);
void profiler_on_vm_opcode_timed(int opcode, uint64_t duration_ns);
void profiler_on_vm_hotspot(VMProfileHotspot hotspot);
void profiler_on_function_call(void);
void profiler_on_method_call(void);
void profiler_on_native_call(void);
void profiler_on_allocation(int type, uint64_t size);
void profiler_on_builder_start(void);
void profiler_on_builder_append(uint64_t bytes);
void profiler_on_builder_finish(void);
void profiler_on_module_load(int from_cache);
void profiler_on_array_access(void);
void profiler_on_field_access(void);
void profiler_on_loop_iteration(void);
void profiler_report(void);

#define PROFILER_ON_EVAL(type) \
    do { \
        if (profiler_is_enabled()) { \
            profiler_on_eval(type); \
        } \
    } while (0)

#define PROFILER_ON_VM_OPCODE(opcode) \
    do { \
        if (profiler_is_enabled()) { \
            profiler_on_vm_opcode((int)(opcode)); \
        } \
    } while (0)

#define PROFILER_ON_VM_OPCODE_TIMED(opcode, duration_ns) \
    do { \
        if (profiler_is_enabled()) { \
            profiler_on_vm_opcode_timed((int)(opcode), (duration_ns)); \
        } \
    } while (0)

#define PROFILER_ON_VM_HOTSPOT(hotspot) \
    do { \
        if (profiler_is_enabled()) { \
            profiler_on_vm_hotspot((hotspot)); \
        } \
    } while (0)

#define PROFILER_ON_FUNCTION_CALL() \
    do { \
        if (profiler_is_enabled()) { \
            profiler_on_function_call(); \
        } \
    } while (0)

#define PROFILER_ON_METHOD_CALL() \
    do { \
        if (profiler_is_enabled()) { \
            profiler_on_method_call(); \
        } \
    } while (0)

#define PROFILER_ON_NATIVE_CALL() \
    do { \
        if (profiler_is_enabled()) { \
            profiler_on_native_call(); \
        } \
    } while (0)

#define PROFILER_ON_ALLOCATION(type, size) \
    do { \
        if (profiler_is_enabled()) { \
            profiler_on_allocation((type), (size)); \
        } \
    } while (0)

#define PROFILER_ON_BUILDER_START() \
    do { \
        if (profiler_is_enabled()) { \
            profiler_on_builder_start(); \
        } \
    } while (0)

#define PROFILER_ON_BUILDER_APPEND(bytes) \
    do { \
        if (profiler_is_enabled()) { \
            profiler_on_builder_append((bytes)); \
        } \
    } while (0)

#define PROFILER_ON_BUILDER_FINISH() \
    do { \
        if (profiler_is_enabled()) { \
            profiler_on_builder_finish(); \
        } \
    } while (0)

#define PROFILER_ON_MODULE_LOAD(from_cache) \
    do { \
        if (profiler_is_enabled()) { \
            profiler_on_module_load((from_cache)); \
        } \
    } while (0)

#define PROFILER_ON_ARRAY_ACCESS() \
    do { \
        if (profiler_is_enabled()) { \
            profiler_on_array_access(); \
        } \
    } while (0)

#define PROFILER_ON_FIELD_ACCESS() \
    do { \
        if (profiler_is_enabled()) { \
            profiler_on_field_access(); \
        } \
    } while (0)

#define PROFILER_ON_LOOP_ITERATION() \
    do { \
        if (profiler_is_enabled()) { \
            profiler_on_loop_iteration(); \
        } \
    } while (0)

#define PROFILER_ON_SLOT_TYPE_HIT() \
    do { \
        if (profiler_is_enabled()) { \
            profiler_on_slot_type_hit(); \
        } \
    } while (0)

#define PROFILER_ON_SLOT_TYPE_MISS() \
    do { \
        if (profiler_is_enabled()) { \
            profiler_on_slot_type_miss(); \
        } \
    } while (0)

#define PROFILER_ON_BRANCH_TAKEN() \
    do { \
        if (profiler_is_enabled()) { \
            profiler_on_branch_taken(); \
        } \
    } while (0)

#define PROFILER_ON_BRANCH_NOT_TAKEN() \
    do { \
        if (profiler_is_enabled()) { \
            profiler_on_branch_not_taken(); \
        } \
    } while (0)

void profiler_on_slot_type_hit(void);
void profiler_on_slot_type_miss(void);
void profiler_on_branch_taken(void);
void profiler_on_branch_not_taken(void);

#endif
