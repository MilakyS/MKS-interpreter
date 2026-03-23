#include "gc.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "../Runtime/value.h"
#include "../env/env.h"

GarbageCollector mks_gc;

void gc_mark_value(RuntimeValue val);
void gc_mark_env(Environment *env);
void gc_mark_object(GCObject *obj);

void gc_init(const size_t initial_threshold) {
    mks_gc.head = NULL;
    mks_gc.allocated_bytes = 0;
    mks_gc.threshold = initial_threshold;
    mks_gc.pause_count = 0;
    mks_gc.roots_count = 0;
    mks_gc.env_roots_count = 0;
}


int gc_save_stack() {
    return mks_gc.roots_count;
}

void gc_restore_stack(const int top) {
    if (top >= 0 && top <= mks_gc.roots_count) {
        mks_gc.roots_count = top;
    }
}

void gc_pause() { mks_gc.pause_count++; }
void gc_resume() { if (mks_gc.pause_count > 0) mks_gc.pause_count--; }

void gc_check(Environment *env) {
    if (mks_gc.allocated_bytes > mks_gc.threshold && mks_gc.pause_count == 0) {
        gc_collect(env, env);
    }
}

void gc_push_root(RuntimeValue *val) {
    if (mks_gc.roots_count < MAX_ROOTS) {
        mks_gc.roots[mks_gc.roots_count++] = val;
    } else {
        fprintf(stderr, "[MKS GC] Fatal: Root stack overflow!\n");
        exit(1);
    }
}

void gc_pop_root() {
    if (mks_gc.roots_count > 0) mks_gc.roots_count--;
}

void gc_push_env(Environment *env) {
    if (mks_gc.env_roots_count < 1024) {
        mks_gc.env_roots[mks_gc.env_roots_count++] = env;
    } else {
        fprintf(stderr, "[MKS GC] Fatal: Env root stack overflow!\n");
        exit(1);
    }
}

void gc_pop_env() {
    if (mks_gc.env_roots_count > 0) mks_gc.env_roots_count--;
}

void *gc_alloc(const size_t size, const GCObjectType type) {
    const auto obj = (GCObject*)malloc(size);
    if (!obj) {
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

void gc_mark_object(GCObject *obj) {
    if (!obj || obj->marked) return;
    obj->marked = true;

    if (obj->type == GC_OBJ_ARRAY) {
        const ManagedArray *arr = (ManagedArray*)obj;
        for (int i = 0; i < arr->count; i++) {
            gc_mark_value(arr->elements[i]);
        }
    } else if (obj->type == GC_OBJ_ENV || obj->type == GC_OBJ_OBJECT) {
        gc_mark_env((Environment*)obj);
    }
}

void gc_mark_value(const RuntimeValue val) {
    if (val.type == VAL_STRING) gc_mark_object((GCObject*)val.data.managed_string);
    else if (val.type == VAL_ARRAY) gc_mark_object((GCObject*)val.data.managed_array);
    else if (val.type == VAL_OBJECT) gc_mark_object((GCObject*)val.data.obj_env);
}

void gc_mark_env(Environment *env) {
    if (!env || ((GCObject*)env)->marked) return;
    ((GCObject*)env)->marked = true;

    for (int i = 0; i < TABLE_SIZE; i++) {
        const EnvVar *entry = env->buckets[i];
        while (entry) {
            gc_mark_value(entry->value);
            entry = entry->next;
        }
    }
    if (env->parent) gc_mark_env(env->parent);
}


void gc_sweep() {
    GCObject **ptr = &mks_gc.head;
    while (*ptr) {
        if (!(*ptr)->marked) {
            GCObject *unreached = *ptr;
            *ptr = unreached->next;
            mks_gc.allocated_bytes -= unreached->size;

            if (unreached->type == GC_OBJ_STRING) {
                free(((ManagedString*)unreached)->data);
            } else if (unreached->type == GC_OBJ_ARRAY) {
                free(((ManagedArray*)unreached)->elements);
            } else if (unreached->type == GC_OBJ_ENV || unreached->type == GC_OBJ_OBJECT) {
                const Environment *env = (Environment*)unreached;
                for (int i = 0; i < TABLE_SIZE; i++) {
                    EnvVar *entry = env->buckets[i];
                    while (entry) {
                        EnvVar *temp = entry;
                        entry = entry->next;
                        free(temp->name);
                        free(temp);
                    }
                }
            }
            free(unreached);
        } else {
            (*ptr)->marked = false;
            ptr = &(*ptr)->next;
        }
    }
}

void gc_collect(Environment *global_env, Environment *current_env) {
    if (mks_gc.pause_count > 0) return;

    if (global_env) gc_mark_env(global_env);
    if (current_env) gc_mark_env(current_env);

    for (int i = 0; i < mks_gc.roots_count; i++) {
        gc_mark_value(*(mks_gc.roots[i]));
    }


    for (int i = 0; i < mks_gc.env_roots_count; i++) {
        gc_mark_env(mks_gc.env_roots[i]);
    }


    gc_sweep();

    mks_gc.threshold = mks_gc.allocated_bytes * 2;
    if (mks_gc.threshold < 1024 * 1024) mks_gc.threshold = 1024 * 1024;
}