#include "gc.h"
#include <stdlib.h>
#include <stdio.h>

#include "../Runtime/value.h"
#include "../env/env.h"

GarbageCollector mks_gc;

static void gc_mark_value(const RuntimeValue *val);
static void gc_mark_env(Environment *env);
static void gc_mark_object(GCObject *obj);
static void gc_sweep(void);

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
}

void gc_resume(void) {
    if (mks_gc.pause_count > 0) {
        mks_gc.pause_count--;
    }
}

void gc_check(Environment *env) {
    if (mks_gc.pause_count == 0 && mks_gc.allocated_bytes > mks_gc.threshold) {
        gc_collect(env, env);
    }
}

void gc_push_root(RuntimeValue *val) {
    if (mks_gc.roots_count >= MAX_ROOTS) {
        fprintf(stderr, "[MKS GC] Fatal: Root stack overflow!\n");
        exit(1);
    }

    mks_gc.roots[mks_gc.roots_count++] = val;
}

void gc_pop_root(void) {
    if (mks_gc.roots_count > 0) {
        mks_gc.roots_count--;
    }
}

void gc_push_env(Environment *env) {
    if (mks_gc.env_roots_count >= MAX_ENV_ROOTS) {
        fprintf(stderr, "[MKS GC] Fatal: Env root stack overflow!\n");
        exit(1);
    }

    mks_gc.env_roots[mks_gc.env_roots_count++] = env;
}

void gc_pop_env(void) {
    if (mks_gc.env_roots_count > 0) {
        mks_gc.env_roots_count--;
    }
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
    return obj;
}

static void gc_mark_object(GCObject *obj) {
    if (obj == NULL || obj->marked) {
        return;
    }

    obj->marked = true;

    switch (obj->type) {
        case GC_OBJ_ARRAY: {
            ManagedArray *arr = (ManagedArray *)obj;
            for (int i = 0; i < arr->count; i++) {
                gc_mark_value(&arr->elements[i]);
            }
            break;
        }

        case GC_OBJ_ENV:
        case GC_OBJ_OBJECT:
            gc_mark_env((Environment *)obj);
            break;

        case GC_OBJ_STRING:
        default:
            break;
    }
}

static void gc_mark_value(const RuntimeValue *val) {
    if (val == NULL) {
        return;
    }

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

        for (int i = 0; i < TABLE_SIZE; i++) {
            const EnvVar *entry = env->buckets[i];
            while (entry != NULL) {
                gc_mark_value(&entry->value);
                entry = entry->next;
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
                    Environment *env = (Environment *)unreached;
                    for (int i = 0; i < TABLE_SIZE; i++) {
                        EnvVar *entry = env->buckets[i];
                        while (entry != NULL) {
                            EnvVar *temp = entry;
                            entry = entry->next;
                            free(temp->name);
                            free(temp);
                        }
                    }
                    break;
                }

                default:
                    break;
            }

            free(unreached);
        } else {
            (*ptr)->marked = false;
            ptr = &(*ptr)->next;
        }
    }
}

void gc_collect(Environment *global_env, Environment *current_env) {
    if (mks_gc.pause_count > 0) {
        return;
    }

    mks_gc.collections++;

    if (global_env != NULL) {
        gc_mark_env(global_env);
    }

    if (current_env != NULL && current_env != global_env) {
        gc_mark_env(current_env);
    }

    for (int i = 0; i < mks_gc.roots_count; i++) {
        gc_mark_value(mks_gc.roots[i]);
    }

    for (int i = 0; i < mks_gc.env_roots_count; i++) {
        gc_mark_env(mks_gc.env_roots[i]);
    }

    gc_sweep();

    mks_gc.threshold = mks_gc.allocated_bytes * 2;
    if (mks_gc.threshold < 1024 * 1024) {
        mks_gc.threshold = 1024 * 1024;
    }
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