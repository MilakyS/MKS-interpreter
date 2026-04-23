#ifndef MKS_GC_H
#define MKS_GC_H

#include <stddef.h>
#include <stdbool.h>

struct RuntimeValue;
struct Environment;

typedef enum {
    GC_OBJ_STRING,
    GC_OBJ_ARRAY,
    GC_OBJ_POINTER,
    GC_OBJ_ENV,
    GC_OBJ_OBJECT
} GCObjectType;

typedef struct GCObject {
    GCObjectType type;
    bool marked;
    size_t size;
    struct GCObject *next;
} GCObject;

#define MAX_ROOTS 1024
#define MAX_ENV_ROOTS 4096

typedef struct {
    GCObject *head;
    size_t allocated_bytes;
    size_t threshold;

    size_t collections;
    size_t freed_objects;
    size_t freed_bytes;

    int pause_count;

    struct RuntimeValue *roots[MAX_ROOTS];
    struct Environment *env_roots[MAX_ENV_ROOTS];
    int roots_count;
    int env_roots_count;

    int debug_enabled;
} GarbageCollector;

GarbageCollector *gc_state(void);
#define mks_gc (*gc_state())

void gc_init(size_t initial_threshold);
void *gc_alloc(size_t size, GCObjectType type);
void gc_collect(struct Environment *global_env, struct Environment *current_env);
void gc_check(struct Environment *env);
void gc_free_all(void);

void gc_push_env(struct Environment *env);
void gc_pop_env(void);

void gc_pause(void);
void gc_resume(void);

void gc_push_root(struct RuntimeValue *val);
void gc_pop_root(void);
int gc_value_needs_root(const struct RuntimeValue *val);
int gc_push_root_if_needed(struct RuntimeValue *val);

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

#endif
