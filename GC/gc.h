#ifndef MKS_GC_H
#define MKS_GC_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

struct RuntimeValue;
struct Environment;

typedef enum {
    GC_OBJ_STRING,
    GC_OBJ_ARRAY,
    GC_OBJ_POINTER,
    GC_OBJ_ENV,
    GC_OBJ_OBJECT,
    GC_OBJ_STRING_BUILDER
} GCObjectType;

/* Orbit GC generation IDs (derived from age field) */
#define ORBIT_0_YOUNG    0
#define ORBIT_1_SURVIVOR 1
#define ORBIT_2_STABLE   2
#define ORBIT_3_PINNED   3

/* Orbit GC metadata flags */
#define ORBIT_FLAG_HOT    ((uint8_t)0x01)
#define ORBIT_FLAG_PINNED ((uint8_t)0x02)

/* Orbit GC heat thresholds */
#define ORBIT_HOT_THRESHOLD  32
#define ORBIT_WARM_THRESHOLD 4

/* Collection policy constants */
#define ORBIT_FULL_GC_INTERVAL 8
#define REMEMBERED_SET_MAX 4096

typedef struct GCObject {
    GCObjectType type;
    bool marked;
    size_t size;
    struct GCObject *next;
    uint8_t  age;           /* incremented each collection the object survives */
    uint16_t heat;          /* touch counter; decays by /2 each collection */
    uint8_t  flags;         /* ORBIT_FLAG_HOT, ORBIT_FLAG_PINNED */
    size_t   external_size; /* heap memory owned by this object but outside GCObject */
} GCObject;

/* Determine orbit of an object from its age and flags */
#define gc_obj_orbit(obj) \
    (((obj)->flags & ORBIT_FLAG_PINNED) ? ORBIT_3_PINNED : \
     (obj)->age == 0 ? ORBIT_0_YOUNG : \
     (obj)->age == 1 ? ORBIT_1_SURVIVOR : ORBIT_2_STABLE)

#define MAX_ROOTS 1024
#define MAX_ENV_ROOTS 4096
#define MAX_PINNED_ROOTS 2048
#define MAX_PINNED_ENV_ROOTS 4096
#define MAX_ROOT_SPANS 256

typedef struct {
    GCObject *head;
    size_t allocated_bytes;
    size_t threshold;
    size_t young_threshold;

    size_t collections;        /* full GC count */
    size_t young_collections;
    size_t freed_objects;      /* total across full GCs */
    size_t freed_bytes;
    size_t young_freed_objects;
    size_t young_freed_bytes;

    size_t promoted_to_survivor;  /* age 0→1 promotions */
    size_t promoted_to_stable;    /* age 1→2 promotions */
    size_t barrier_calls;
    size_t old_to_young_edges;
    size_t full_fallbacks;

    int pause_count;
    int young_since_full;  /* young GC count since last full GC */

    struct RuntimeValue *roots[MAX_ROOTS];
    struct Environment *env_roots[MAX_ENV_ROOTS];
    struct RuntimeValue *pinned_roots[MAX_PINNED_ROOTS];
    struct Environment *pinned_env_roots[MAX_PINNED_ENV_ROOTS];
    struct RuntimeValue *root_spans[MAX_ROOT_SPANS];
    int root_span_lengths[MAX_ROOT_SPANS];
    int roots_count;
    int env_roots_count;
    int pinned_roots_count;
    int pinned_env_roots_count;
    int root_span_count;

    /* Remembered set for old→young references */
    GCObject *remembered_set[REMEMBERED_SET_MAX];
    int remembered_set_count;

    int debug_enabled;
} GarbageCollector;

GarbageCollector *gc_state(void);
#define mks_gc (*gc_state())

void gc_init(size_t initial_threshold);
void *gc_alloc(size_t size, GCObjectType type);
void gc_collect(struct Environment *global_env, struct Environment *current_env);
void gc_collect_young(struct Environment *global_env, struct Environment *current_env);
void gc_check(struct Environment *env);
void gc_free_all(void);

void gc_write_barrier(GCObject *owner, const struct RuntimeValue *value);
void gc_external_alloc(GCObject *obj, size_t bytes);
void gc_external_free(GCObject *obj, size_t bytes);
size_t gc_object_total_size(const GCObject *obj);

void gc_push_env(struct Environment *env);
void gc_pop_env(void);

void gc_pause(void);
void gc_resume(void);

void gc_push_root(struct RuntimeValue *val);
void gc_pop_root(void);
void gc_push_root_span(struct RuntimeValue *values, int count);
void gc_pop_root_span(void);
void gc_pin_root(struct RuntimeValue *val);
void gc_unpin_root(struct RuntimeValue *val);
int gc_value_needs_root(const struct RuntimeValue *val);
int gc_push_root_if_needed(struct RuntimeValue *val);
int gc_pin_root_if_needed(struct RuntimeValue *val);
void gc_unpin_root_if_needed(struct RuntimeValue *val);
void gc_pin_env(struct Environment *env);
void gc_unpin_env(struct Environment *env);

int gc_save_stack(void);
void gc_restore_stack(int top);

typedef struct MksGcRootScope {
    int top;
} MksGcRootScope;

MksGcRootScope gc_root_scope_begin(void);
void gc_root_scope_end(MksGcRootScope *scope);

#if defined(__GNUC__) || defined(__clang__)
#define MKS_GC_ROOTS(name) \
    MksGcRootScope name __attribute__((cleanup(gc_root_scope_end))) = gc_root_scope_begin()
#define MKS_GC_ROOTS_END(name) ((void)(name))
#else
#define MKS_GC_ROOTS(name) int name = gc_save_stack()
#define MKS_GC_ROOTS_END(name) gc_restore_stack(name)
#endif

#define MKS_GC_ROOT(value_ptr) gc_push_root((value_ptr))
#define MKS_GC_ROOT_IF_NEEDED(value_ptr) gc_push_root_if_needed((value_ptr))

#define MKS_GC_ROOT_SCOPE(var_name) \
    for (int var_name = gc_save_stack(), var_name##_once = 1; \
         var_name##_once; \
         gc_restore_stack(var_name), var_name##_once = 0)

#define MKS_WITH_GC_ROOT(value_ptr) \
    for (int mks_gc_root_top__ = gc_save_stack(), mks_gc_root_once__ = (gc_push_root((value_ptr)), 1); \
         mks_gc_root_once__; \
         gc_restore_stack(mks_gc_root_top__), mks_gc_root_once__ = 0)

#define MKS_WITH_GC_ROOT_IF_NEEDED(value_ptr) \
    for (int mks_gc_root_top__ = gc_save_stack(), mks_gc_root_once__ = (gc_push_root_if_needed((value_ptr)), 1); \
         mks_gc_root_once__; \
         gc_restore_stack(mks_gc_root_top__), mks_gc_root_once__ = 0)

void gc_set_debug(int enabled);
void gc_dump_stats(void);
void gc_dump_objects(void);

/* Orbit GC hot/cold profiling API */
void orbit_touch_object(GCObject *obj);
void orbit_touch_value(const struct RuntimeValue *value);
int  orbit_object_is_hot(const GCObject *obj);
void orbit_dump_hot_objects(void);

#endif
