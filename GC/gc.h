#ifndef MKS_GC_H
#define MKS_GC_H

#include <stddef.h>
#include <stdbool.h>

struct RuntimeValue;
struct Environment;

typedef enum {
    GC_OBJ_STRING,
    GC_OBJ_ARRAY,
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

extern GarbageCollector mks_gc;

void gc_init(size_t initial_threshold);
void *gc_alloc(size_t size, GCObjectType type);
void gc_collect(struct Environment *global_env, struct Environment *current_env);
void gc_check(struct Environment *env);

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

void gc_set_debug(int enabled);
void gc_dump_stats(void);
void gc_dump_objects(void);

#endif
