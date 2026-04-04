#include "gc.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "../Runtime/value.h"
#include "../env/env.h"

GarbageCollector mks_gc;

static void gc_mark_value(const RuntimeValue *val);
static void gc_mark_env(Environment *env);
static void gc_mark_object(GCObject *obj);
static void gc_sweep(void);

#define GC_LOG(...)                         \
    do {                                   \
        if (mks_gc.debug_enabled) {        \
            fprintf(stderr, __VA_ARGS__);  \
        }                                  \
    } while (0)

static const char *gc_type_name(const GCObjectType type) {
    switch (type) {
        case GC_OBJ_STRING: return "STRING";
        case GC_OBJ_ARRAY:  return "ARRAY";
        case GC_OBJ_ENV:    return "ENV";
        case GC_OBJ_OBJECT: return "OBJECT";
        default:            return "UNKNOWN";
    }
}

void gc_set_debug(const int enabled) {
    mks_gc.debug_enabled = enabled;
}

void gc_init(const size_t initial_threshold) {
    mks_gc.head = NULL;
    mks_gc.allocated_bytes = 0;
    mks_gc.threshold = initial_threshold;
    mks_gc.collections = 0;
    mks_gc.freed_objects = 0;
    mks_gc.freed_bytes = 0;
    mks_gc.pause_count = 0;
    mks_gc.roots_count = 0;
    mks_gc.env_roots_count = 0;
    mks_gc.debug_enabled = 0;
}

int gc_save_stack(void) {
    return mks_gc.roots_count;
}

void gc_restore_stack(const int top) {
    if (top >= 0 && top <= mks_gc.roots_count) {
        mks_gc.roots_count = top;
    }
}

void gc_pause(void) {
    mks_gc.pause_count++;
    GC_LOG("[GC] pause -> %d\n", mks_gc.pause_count);
}

void gc_resume(void) {
    if (mks_gc.pause_count <= 0) {
        fprintf(stderr, "[MKS GC] Fatal: gc_resume called without matching gc_pause\n");
        exit(1);
    }

    mks_gc.pause_count--;
    GC_LOG("[GC] resume -> %d\n", mks_gc.pause_count);
}

void gc_check(Environment *env) {
    if (mks_gc.pause_count == 0 && mks_gc.allocated_bytes >= mks_gc.threshold) {
        GC_LOG("[GC] threshold hit: allocated=%zu threshold=%zu\n",
               mks_gc.allocated_bytes, mks_gc.threshold);
        gc_collect(env, env);
    }
}

void gc_push_root(RuntimeValue *val) {
    if (mks_gc.roots_count >= MAX_ROOTS) {
        fprintf(stderr, "[MKS GC] Fatal: Root stack overflow!\n");
        exit(1);
    }

    mks_gc.roots[mks_gc.roots_count++] = val;
    GC_LOG("[GC] push root ptr=%p roots_count=%d\n", (void *)val, mks_gc.roots_count);
}

void gc_pop_root(void) {
    if (mks_gc.roots_count > 0) {
        mks_gc.roots_count--;
    }
    GC_LOG("[GC] pop root roots_count=%d\n", mks_gc.roots_count);
}

void gc_push_env(Environment *env) {
    if (mks_gc.env_roots_count >= MAX_ENV_ROOTS) {
        fprintf(stderr, "[MKS GC] Fatal: Env root stack overflow!\n");
        exit(1);
    }

    mks_gc.env_roots[mks_gc.env_roots_count++] = env;
    GC_LOG("[GC] push env root env=%p env_roots_count=%d\n",
           (void *)env, mks_gc.env_roots_count);
}

void gc_pop_env(void) {
    if (mks_gc.env_roots_count > 0) {
        mks_gc.env_roots_count--;
    }
    GC_LOG("[GC] pop env root env_roots_count=%d\n", mks_gc.env_roots_count);
}

void *gc_alloc(const size_t size, const GCObjectType type) {
    if (size < sizeof(GCObject)) {
        fprintf(stderr, "[MKS GC] Fatal: gc_alloc size too small\n");
        exit(1);
    }

    GCObject *obj = (GCObject *)malloc(size);
    if (obj == NULL) {
        fprintf(stderr, "[MKS GC] Fatal: Out of memory\n");
        exit(1);
    }

    obj->type = type;
    obj->marked = false;
    obj->size = size;
    obj->next = mks_gc.head;
    mks_gc.head = obj;

    mks_gc.allocated_bytes += size;

    GC_LOG("[GC] alloc ptr=%p type=%s size=%zu allocated=%zu\n",
           (void *)obj, gc_type_name(type), size, mks_gc.allocated_bytes);

    return obj;
}

static void gc_mark_object(GCObject *obj) {
    if (obj == NULL) {
        return;
    }

    if (obj->type == GC_OBJ_ENV || obj->type == GC_OBJ_OBJECT) {
        gc_mark_env((Environment *)obj);
        return;
    }

    if (obj->marked) {
        return;
    }

    obj->marked = true;

    GC_LOG("[GC] mark object ptr=%p type=%s size=%zu\n",
           (void *)obj, gc_type_name(obj->type), obj->size);

    switch (obj->type) {
        case GC_OBJ_ARRAY: {
            const ManagedArray *arr = (ManagedArray *)obj;
            for (int i = 0; i < arr->count; i++) {
                gc_mark_value(&arr->elements[i]);
            }
            break;
        }

        case GC_OBJ_STRING:
        default:
            break;
    }
}

static void gc_mark_value(const RuntimeValue *val) {
    if (val == NULL) {
        return;
    }

    GC_LOG("[GC] mark value type=%d\n", val->type);

    switch (val->type) {
        case VAL_STRING:
            gc_mark_object((GCObject *)val->data.managed_string);
            break;

        case VAL_ARRAY:
            gc_mark_object((GCObject *)val->data.managed_array);
            break;

        case VAL_OBJECT:
            gc_mark_object((GCObject *)val->data.obj_env);
            break;

        case VAL_FUNC:
            gc_mark_env(val->data.func.closure_env);
            break;

        case VAL_RETURN: {
            RuntimeValue unwrapped = *val;
            unwrapped.type = unwrapped.original_type;
            gc_mark_value(&unwrapped);
            break;
        }

        default:
            break;
    }
}

static void gc_mark_env(Environment *env) {
    while (env != NULL) {
        GCObject *obj = (GCObject *)env;
        if (obj->marked) {
            return;
        }

        obj->marked = true;

        GC_LOG("[GC] mark env ptr=%p buckets=%zu entries=%zu\n",
               (void *)env,
               env->bucket_count,
               env->entry_count);

        if (env->buckets != NULL) {
            for (size_t i = 0; i < env->bucket_count; i++) {
                const EnvVar *entry = env->buckets[i];
                while (entry != NULL) {
                    GC_LOG("[GC]   env entry name=%s hash=%u\n",
                           entry->name ? entry->name : "<null>",
                           entry->hash);
                    gc_mark_value(&entry->value);
                    entry = entry->next;
                }
            }
        }

        env = env->parent;
    }
}

static void gc_sweep(void) {
    GCObject **ptr = &mks_gc.head;

    while (*ptr != NULL) {
        if (!(*ptr)->marked) {
            GCObject *unreached = *ptr;
            *ptr = unreached->next;

            GC_LOG("[GC] free ptr=%p type=%s size=%zu\n",
                   (void *)unreached,
                   gc_type_name(unreached->type),
                   unreached->size);

            mks_gc.allocated_bytes -= unreached->size;
            mks_gc.freed_objects++;
            mks_gc.freed_bytes += unreached->size;

            switch (unreached->type) {
                case GC_OBJ_STRING:
                    free(((ManagedString *)unreached)->data);
                    break;

                case GC_OBJ_ARRAY:
                    free(((ManagedArray *)unreached)->elements);
                    break;

                case GC_OBJ_ENV:
                case GC_OBJ_OBJECT: {
                    const Environment *env = (Environment *)unreached;

                    if (env->buckets != NULL) {
                        for (size_t i = 0; i < env->bucket_count; i++) {
                            EnvVar *entry = env->buckets[i];
                            while (entry != NULL) {
                                EnvVar *temp = entry;
                                entry = entry->next;
                                free(temp->name);
                                free(temp);
                            }
                        }

                        if (env->buckets != env->inline_buckets) {
                            free(env->buckets);
                        }
                    }
                    break;
                }

                default:
                    break;
            }

            free(unreached);
        } else {
            GC_LOG("[GC] keep ptr=%p type=%s size=%zu\n",
                   (void *)(*ptr),
                   gc_type_name((*ptr)->type),
                   (*ptr)->size);

            (*ptr)->marked = false;
            ptr = &(*ptr)->next;
        }
    }
}

void gc_collect(Environment *global_env, Environment *current_env) {
    if (mks_gc.pause_count > 0) {
        GC_LOG("[GC] collect skipped: paused\n");
        return;
    }

    const size_t freed_objects_before = mks_gc.freed_objects;
    const size_t freed_bytes_before = mks_gc.freed_bytes;

    mks_gc.collections++;

    GC_LOG("[GC] collect start #%zu allocated=%zu threshold=%zu roots=%d env_roots=%d\n",
           mks_gc.collections,
           mks_gc.allocated_bytes,
           mks_gc.threshold,
           mks_gc.roots_count,
           mks_gc.env_roots_count);

    if (global_env != NULL) {
        GC_LOG("[GC] root global_env=%p\n", (void *)global_env);
        gc_mark_env(global_env);
    }

    if (current_env != NULL && current_env != global_env) {
        GC_LOG("[GC] root current_env=%p\n", (void *)current_env);
        gc_mark_env(current_env);
    }

    for (int i = 0; i < mks_gc.roots_count; i++) {
        GC_LOG("[GC] root temp[%d]=%p\n", i, (void *)mks_gc.roots[i]);
        gc_mark_value(mks_gc.roots[i]);
    }

    for (int i = 0; i < mks_gc.env_roots_count; i++) {
        GC_LOG("[GC] root env_stack[%d]=%p\n", i, (void *)mks_gc.env_roots[i]);
        gc_mark_env(mks_gc.env_roots[i]);
    }

    gc_sweep();

    const size_t freed_now_objects = mks_gc.freed_objects - freed_objects_before;
    const size_t freed_now_bytes = mks_gc.freed_bytes - freed_bytes_before;

    size_t new_threshold = mks_gc.allocated_bytes + (mks_gc.allocated_bytes / 2);
    if (new_threshold < 1024 * 1024) {
        new_threshold = 1024 * 1024;
    }
    mks_gc.threshold = new_threshold;

    GC_LOG("[GC] collect end   #%zu allocated=%zu threshold=%zu freed_now_objects=%zu freed_now_bytes=%zu freed_total_objects=%zu freed_total_bytes=%zu\n",
           mks_gc.collections,
           mks_gc.allocated_bytes,
           mks_gc.threshold,
           freed_now_objects,
           freed_now_bytes,
           mks_gc.freed_objects,
           mks_gc.freed_bytes);
}

void gc_dump_stats(void) {
    fprintf(stderr,
            "[MKS GC] allocated=%zu threshold=%zu collections=%zu freed_objects=%zu freed_bytes=%zu\n",
            mks_gc.allocated_bytes,
            mks_gc.threshold,
            mks_gc.collections,
            mks_gc.freed_objects,
            mks_gc.freed_bytes);
}

void gc_dump_objects(void) {
    fprintf(stderr, "[GC] object list begin\n");

    GCObject *obj = mks_gc.head;
    while (obj != NULL) {
        fprintf(stderr,
                "[GC] obj ptr=%p type=%s marked=%d size=%zu next=%p\n",
                (void *)obj,
                gc_type_name(obj->type),
                obj->marked ? 1 : 0,
                obj->size,
                (void *)obj->next);
        obj = obj->next;
    }

    fprintf(stderr, "[GC] object list end\n");
}