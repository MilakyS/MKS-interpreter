#include "gc.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "../Runtime/value.h"
#include "../Runtime/errors.h"
#include "../Runtime/context.h"
#include "../env/env.h"

GarbageCollector *gc_state(void) {
    return &mks_context_current()->gc;
}

typedef struct {
    GCObject **items;
    size_t count;
    size_t capacity;
} GCMarkStack;

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
        case GC_OBJ_POINTER:return "POINTER";
        case GC_OBJ_ENV:    return "ENV";
        case GC_OBJ_OBJECT: return "OBJECT";
        case GC_OBJ_STRING_BUILDER: return "STRING_BUILDER";
        default:            return "UNKNOWN";
    }
}


static void gc_mark_stack_init(GCMarkStack *stack) {
    stack->items = NULL;
    stack->count = 0;
    stack->capacity = 0;
}

static void gc_mark_stack_free(GCMarkStack *stack) {
    free(stack->items);
    stack->items = NULL;
    stack->count = 0;
    stack->capacity = 0;
}

static void gc_mark_stack_push(GCMarkStack *stack, GCObject *obj) {
    if (obj == NULL) {
        return;
    }

    if (stack->count >= stack->capacity) {
        size_t new_capacity = (stack->capacity == 0) ? 512 : stack->capacity * 2;
        GCObject **new_items =
            (GCObject **)realloc(stack->items, sizeof(GCObject *) * new_capacity);

        if (new_items == NULL) {
            runtime_error("Out of memory growing GC mark stack");
        }

        stack->items = new_items;
        stack->capacity = new_capacity;
    }

    stack->items[stack->count++] = obj;
}

static GCObject *gc_mark_stack_pop(GCMarkStack *stack) {
    if (stack->count == 0) {
        return NULL;
    }

    return stack->items[--stack->count];
}

static void gc_mark_object_push(GCMarkStack *stack, GCObject *obj) {
    if (obj == NULL || obj->marked) {
        return;
    }

    obj->marked = true;

    GC_LOG("[GC] mark+push ptr=%p type=%s size=%zu\n",
           (void *)obj,
           gc_type_name(obj->type),
           obj->size);

    gc_mark_stack_push(stack, obj);
}

static void gc_mark_value_push(GCMarkStack *stack, const RuntimeValue *val) {
    if (val == NULL) {
        return;
    }

    GC_LOG("[GC] mark value type=%d\n", val->type);

    switch (val->type) {
        case VAL_STRING:
            gc_mark_object_push(stack, (GCObject *)val->data.managed_string);
            break;

        case VAL_ARRAY:
            gc_mark_object_push(stack, (GCObject *)val->data.managed_array);
            break;

        case VAL_POINTER:
            gc_mark_object_push(stack, (GCObject *)val->data.managed_pointer);
            break;

        case VAL_OBJECT:
        case VAL_MODULE:
            gc_mark_object_push(stack, (GCObject *)val->data.obj_env);
            break;

        case VAL_FUNC:
            gc_mark_object_push(stack, (GCObject *)val->data.func.closure_env);
            break;

        case VAL_BLUEPRINT:
            gc_mark_object_push(stack, (GCObject *)val->data.blueprint.closure_env);
            break;

        case VAL_STRING_BUILDER:
            gc_mark_object_push(stack, (GCObject *)val->data.string_builder);
            break;

        case VAL_RETURN: {
            RuntimeValue tmp = *val;
            tmp.type = tmp.original_type;
            gc_mark_value_push(stack, &tmp);
            break;
        }

        default:
            break;
    }
}

static void gc_mark_all_iterative(Environment *global_env, Environment *current_env) {
    GCMarkStack stack;
    gc_mark_stack_init(&stack);

    if (global_env != NULL) {
        gc_mark_object_push(&stack, (GCObject *)global_env);
    }

    if (current_env != NULL && current_env != global_env) {
        gc_mark_object_push(&stack, (GCObject *)current_env);
    }

    for (int i = 0; i < mks_gc.roots_count; i++) {
        GC_LOG("[GC] root temp[%d]=%p\n", i, (void *)mks_gc.roots[i]);
        gc_mark_value_push(&stack, mks_gc.roots[i]);
    }

    for (int i = 0; i < mks_gc.pinned_roots_count; i++) {
        GC_LOG("[GC] root pinned[%d]=%p\n", i, (void *)mks_gc.pinned_roots[i]);
        gc_mark_value_push(&stack, mks_gc.pinned_roots[i]);
    }

    for (int i = 0; i < mks_gc.root_span_count; i++) {
        RuntimeValue *values = mks_gc.root_spans[i];
        const int count = mks_gc.root_span_lengths[i];
        for (int j = 0; j < count; j++) {
            gc_mark_value_push(&stack, &values[j]);
        }
    }

    for (int i = 0; i < mks_gc.env_roots_count; i++) {
        GC_LOG("[GC] root env_stack[%d]=%p\n", i, (void *)mks_gc.env_roots[i]);
        gc_mark_object_push(&stack, (GCObject *)mks_gc.env_roots[i]);
    }

    for (int i = 0; i < mks_gc.pinned_env_roots_count; i++) {
        GC_LOG("[GC] root env_pinned[%d]=%p\n", i, (void *)mks_gc.pinned_env_roots[i]);
        gc_mark_object_push(&stack, (GCObject *)mks_gc.pinned_env_roots[i]);
    }

    while (stack.count > 0) {
        GCObject *obj = gc_mark_stack_pop(&stack);
        if (obj == NULL) {
            continue;
        }

        switch (obj->type) {
            case GC_OBJ_ARRAY: {
                ManagedArray *arr = (ManagedArray *)obj;
                for (int i = 0; i < arr->count; i++) {
                    gc_mark_value_push(&stack, &arr->elements[i]);
                }
                break;
            }

            case GC_OBJ_ENV:
            case GC_OBJ_OBJECT: {
                Environment *env = (Environment *)obj;

                GC_LOG("[GC] scan env ptr=%p buckets=%zu entries=%zu\n",
                       (void *)env,
                       env->bucket_count,
                       env->entry_count);

                if (env->buckets != NULL) {
                    for (size_t i = 0; i < env->bucket_count; i++) {
                        EnvVar *entry = env->buckets[i];
                        while (entry != NULL) {
                            GC_LOG("[GC]   env entry name=%s hash=%u\n",
                                   entry->name ? entry->name : "<null>",
                                   entry->hash);
                            gc_mark_value_push(&stack, &entry->value);
                            entry = entry->next;
                        }
                    }
                }

                if (env->parent != NULL) {
                    gc_mark_object_push(&stack, (GCObject *)env->parent);
                }

                break;
            }

            case GC_OBJ_POINTER: {
                ManagedPointer *ptr = (ManagedPointer *)obj;
                switch (ptr->kind) {
                    case PTR_ENV_VAR:
                        gc_mark_object_push(&stack, (GCObject *)ptr->as.var.env);
                        break;
                    case PTR_ARRAY_ELEM:
                        gc_mark_object_push(&stack, (GCObject *)ptr->as.array_elem.array);
                        break;
                    case PTR_OBJECT_FIELD:
                        gc_mark_object_push(&stack, (GCObject *)ptr->as.object_field.env);
                        break;
                }
                break;
            }

            case GC_OBJ_STRING:
            default:
                break;
        }
    }

    gc_mark_stack_free(&stack);
}


void gc_set_debug(const int enabled) {
    mks_gc.debug_enabled = enabled;
}

void gc_init(const size_t initial_threshold) {
    RuntimeValue **old_root_spans = mks_gc.root_spans;
    int *old_root_span_lengths = mks_gc.root_span_lengths;
    mks_gc.head = NULL;
    mks_gc.allocated_bytes = 0;
    mks_gc.threshold = initial_threshold;
    mks_gc.young_threshold = 256 * 1024;
    mks_gc.collections = 0;
    mks_gc.young_collections = 0;
    mks_gc.freed_objects = 0;
    mks_gc.freed_bytes = 0;
    mks_gc.young_freed_objects = 0;
    mks_gc.young_freed_bytes = 0;
    mks_gc.promoted_to_survivor = 0;
    mks_gc.promoted_to_stable = 0;
    mks_gc.barrier_calls = 0;
    mks_gc.old_to_young_edges = 0;
    mks_gc.full_fallbacks = 0;
    mks_gc.pause_count = 0;
    mks_gc.young_since_full = 0;
    mks_gc.roots_count = 0;
    mks_gc.env_roots_count = 0;
    mks_gc.pinned_roots_count = 0;
    mks_gc.pinned_env_roots_count = 0;
    mks_gc.root_span_count = 0;
    mks_gc.root_span_capacity = GC_INITIAL_ROOT_SPANS;
    mks_gc.root_spans = (RuntimeValue **)realloc(old_root_spans,
                                                 sizeof(RuntimeValue *) * (size_t)mks_gc.root_span_capacity);
    mks_gc.root_span_lengths = (int *)realloc(old_root_span_lengths,
                                             sizeof(int) * (size_t)mks_gc.root_span_capacity);
    if (mks_gc.root_spans == NULL || mks_gc.root_span_lengths == NULL) {
        runtime_error("Out of memory initializing GC root spans");
    }
    mks_gc.remembered_set_count = 0;
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

MksGcRootScope gc_root_scope_begin(void) {
    MksGcRootScope scope;
    scope.top = gc_save_stack();
    return scope;
}

void gc_root_scope_end(MksGcRootScope *scope) {
    if (scope != NULL) {
        gc_restore_stack(scope->top);
    }
}

void gc_pause(void) {
    mks_gc.pause_count++;
    GC_LOG("[GC] pause -> %d\n", mks_gc.pause_count);
}

void gc_resume(void) {
    if (mks_gc.pause_count <= 0) {
        runtime_error("gc_resume called without matching gc_pause");
    }

    mks_gc.pause_count--;
    GC_LOG("[GC] resume -> %d\n", mks_gc.pause_count);
}

void gc_check(Environment *env) {
    if (mks_gc.pause_count > 0) return;

    size_t pressure = mks_gc.allocated_bytes;

    if (pressure >= mks_gc.young_threshold && mks_gc.young_threshold > 0) {
        GC_LOG("[GC] young threshold hit: allocated=%zu young_threshold=%zu\n",
               pressure, mks_gc.young_threshold);
        gc_collect_young(env, env);
        return;
    }

    if (pressure >= mks_gc.threshold) {
        GC_LOG("[GC] threshold hit: allocated=%zu threshold=%zu (collect)\n",
               pressure, mks_gc.threshold);
        gc_collect(env, env);
    }
}

void gc_push_root(RuntimeValue *val) {
    if (mks_gc.roots_count >= MAX_ROOTS) {
        runtime_error("GC root stack overflow");
    }

    mks_gc.roots[mks_gc.roots_count++] = val;
    GC_LOG("[GC] push root ptr=%p roots_count=%d\n", (void *)val, mks_gc.roots_count);
}

void gc_push_root_span(RuntimeValue *values, int count) {
    if (values == NULL || count <= 0) {
        return;
    }
    if (mks_gc.root_span_count >= mks_gc.root_span_capacity) {
        int new_capacity = mks_gc.root_span_capacity == 0
            ? GC_INITIAL_ROOT_SPANS
            : mks_gc.root_span_capacity * 2;
        RuntimeValue **new_spans = (RuntimeValue **)realloc(
            mks_gc.root_spans,
            sizeof(RuntimeValue *) * (size_t)new_capacity);
        int *new_lengths = (int *)realloc(
            mks_gc.root_span_lengths,
            sizeof(int) * (size_t)new_capacity);
        if (new_spans == NULL || new_lengths == NULL) {
            runtime_error("Out of memory growing GC root spans");
        }
        mks_gc.root_spans = new_spans;
        mks_gc.root_span_lengths = new_lengths;
        mks_gc.root_span_capacity = new_capacity;
    }
    mks_gc.root_spans[mks_gc.root_span_count] = values;
    mks_gc.root_span_lengths[mks_gc.root_span_count] = count;
    mks_gc.root_span_count++;
}

void gc_update_root_span(RuntimeValue *old_values, RuntimeValue *new_values, int new_count) {
    if (old_values == NULL || new_values == NULL || new_count <= 0) {
        return;
    }
    for (int i = mks_gc.root_span_count - 1; i >= 0; i--) {
        if (mks_gc.root_spans[i] == old_values) {
            mks_gc.root_spans[i] = new_values;
            mks_gc.root_span_lengths[i] = new_count;
            return;
        }
    }
}

void gc_pin_root(RuntimeValue *val) {
    if (val == NULL) {
        return;
    }

    for (int i = 0; i < mks_gc.pinned_roots_count; i++) {
        if (mks_gc.pinned_roots[i] == val) {
            return;
        }
    }

    if (mks_gc.pinned_roots_count >= MAX_PINNED_ROOTS) {
        runtime_error("GC pinned root stack overflow");
    }

    mks_gc.pinned_roots[mks_gc.pinned_roots_count++] = val;
}

void gc_unpin_root(RuntimeValue *val) {
    if (val == NULL) {
        return;
    }

    for (int i = 0; i < mks_gc.pinned_roots_count; i++) {
        if (mks_gc.pinned_roots[i] == val) {
            mks_gc.pinned_roots[i] = mks_gc.pinned_roots[mks_gc.pinned_roots_count - 1];
            mks_gc.pinned_roots_count--;
            return;
        }
    }
}

int gc_value_needs_root(const RuntimeValue *val) {
    if (val == NULL) {
        return 0;
    }

    switch (val->type) {
        case VAL_STRING:
        case VAL_ARRAY:
        case VAL_POINTER:
        case VAL_OBJECT:
        case VAL_MODULE:
        case VAL_FUNC:
        case VAL_BLUEPRINT:
            return 1;
        case VAL_RETURN: {
            RuntimeValue unwrapped = *val;
            unwrapped.type = unwrapped.original_type;
            return gc_value_needs_root(&unwrapped);
        }
        default:
            return 0;
    }
}

int gc_push_root_if_needed(RuntimeValue *val) {
    if (!gc_value_needs_root(val)) {
        return 0;
    }

    gc_push_root(val);
    return 1;
}

int gc_pin_root_if_needed(RuntimeValue *val) {
    if (!gc_value_needs_root(val)) {
        return 0;
    }

    gc_pin_root(val);
    return 1;
}

void gc_unpin_root_if_needed(RuntimeValue *val) {
    if (!gc_value_needs_root(val)) {
        return;
    }

    gc_unpin_root(val);
}

void gc_pop_root(void) {
    if (mks_gc.roots_count > 0) {
        mks_gc.roots_count--;
    }
    GC_LOG("[GC] pop root roots_count=%d\n", mks_gc.roots_count);
}

void gc_pop_root_span(void) {
    if (mks_gc.root_span_count <= 0) {
        runtime_error("GC root span stack underflow");
    }
    mks_gc.root_span_count--;
}

void gc_push_env(Environment *env) {
    if (mks_gc.env_roots_count >= MAX_ENV_ROOTS) {
        runtime_error("GC env root stack overflow");
    }

    mks_gc.env_roots[mks_gc.env_roots_count++] = env;
    GC_LOG("[GC] push env root env=%p env_roots_count=%d\n",
           (void *)env, mks_gc.env_roots_count);
}

void gc_pin_env(Environment *env) {
    if (env == NULL) {
        return;
    }

    for (int i = 0; i < mks_gc.pinned_env_roots_count; i++) {
        if (mks_gc.pinned_env_roots[i] == env) {
            return;
        }
    }

    if (mks_gc.pinned_env_roots_count >= MAX_PINNED_ENV_ROOTS) {
        runtime_error("GC pinned env root stack overflow");
    }

    mks_gc.pinned_env_roots[mks_gc.pinned_env_roots_count++] = env;
    ((GCObject*)env)->flags |= ORBIT_FLAG_PINNED;
}

void gc_unpin_env(Environment *env) {
    if (env == NULL) {
        return;
    }

    for (int i = 0; i < mks_gc.pinned_env_roots_count; i++) {
        if (mks_gc.pinned_env_roots[i] == env) {
            mks_gc.pinned_env_roots[i] = mks_gc.pinned_env_roots[mks_gc.pinned_env_roots_count - 1];
            mks_gc.pinned_env_roots_count--;
            ((GCObject*)env)->flags &= ~ORBIT_FLAG_PINNED;
            return;
        }
    }
}

void gc_pop_env(void) {
    if (mks_gc.env_roots_count > 0) {
        mks_gc.env_roots_count--;
    }
    GC_LOG("[GC] pop env root env_roots_count=%d\n", mks_gc.env_roots_count);
}

void *gc_alloc(const size_t size, const GCObjectType type) {
    if (size < sizeof(GCObject)) {
        runtime_error("gc_alloc size too small");
    }

    GCObject *obj = (GCObject *)malloc(size);
    if (obj == NULL) {
        runtime_error("Out of memory in gc_alloc");
    }

    obj->type = type;
    obj->marked = false;
    obj->size = size;
    obj->next = mks_gc.head;
    obj->age = 0;
    obj->heat = 0;
    obj->flags = 0;
    obj->external_size = 0;
    mks_gc.head = obj;

    mks_gc.allocated_bytes += size;

    GC_LOG("[GC] alloc ptr=%p type=%s size=%zu allocated=%zu\n",
           (void *)obj, gc_type_name(type), size, mks_gc.allocated_bytes);

    return obj;
}

static void gc_free_object(GCObject *obj) {
    if (obj == NULL) {
        return;
    }

    switch (obj->type) {
        case GC_OBJ_STRING:
            free(((ManagedString *)obj)->data);
            break;

        case GC_OBJ_ARRAY:
            free(((ManagedArray *)obj)->elements);
            break;

        case GC_OBJ_POINTER:
            if (((ManagedPointer *)obj)->kind == PTR_OBJECT_FIELD) {
                free(((ManagedPointer *)obj)->as.object_field.field);
            }
            break;

        case GC_OBJ_STRING_BUILDER:
            free(((ManagedStringBuilder *)obj)->data);
            break;

        case GC_OBJ_ENV:
        case GC_OBJ_OBJECT: {
            Environment *env = (Environment *)obj;

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

                free(env->buckets);
            }
            break;
        }

        default:
            break;
    }

    free(obj);
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

            gc_free_object(unreached);
        } else {
            GC_LOG("[GC] keep ptr=%p type=%s size=%zu\n",
                   (void *)(*ptr),
                   gc_type_name((*ptr)->type),
                   (*ptr)->size);

            (*ptr)->marked = false;


            if ((*ptr)->age < UINT8_MAX) (*ptr)->age++;
            (*ptr)->heat = (uint16_t)((*ptr)->heat / 2);
            if ((*ptr)->heat >= ORBIT_HOT_THRESHOLD)
                (*ptr)->flags |= ORBIT_FLAG_HOT;
            else
                (*ptr)->flags &= (uint8_t)(~ORBIT_FLAG_HOT);

            ptr = &(*ptr)->next;
        }
    }
}

void gc_free_all(void) {
    GCObject *obj = mks_gc.head;
    while (obj != NULL) {
        GCObject *next = obj->next;
        mks_gc.freed_objects++;
        mks_gc.freed_bytes += obj->size;
        gc_free_object(obj);
        obj = next;
    }

    mks_gc.head = NULL;
    mks_gc.allocated_bytes = 0;
    mks_gc.roots_count = 0;
    mks_gc.env_roots_count = 0;
    mks_gc.pinned_roots_count = 0;
    mks_gc.pinned_env_roots_count = 0;
    mks_gc.root_span_count = 0;
    free(mks_gc.root_spans);
    free(mks_gc.root_span_lengths);
    mks_gc.root_spans = NULL;
    mks_gc.root_span_lengths = NULL;
    mks_gc.root_span_capacity = 0;
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

    gc_mark_all_iterative(global_env, current_env);
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

    mks_gc.remembered_set_count = 0;
    mks_gc.young_since_full = 0;
}

static void gc_sweep_young(void) {
    GCObject **ptr = &mks_gc.head;

    while (*ptr != NULL) {
        GCObject *obj = *ptr;
        int orbit = gc_obj_orbit(obj);

        if (orbit >= ORBIT_2_STABLE) {
            obj->marked = false;
            ptr = &obj->next;
            continue;
        }

        if (!obj->marked) {
            *ptr = obj->next;
            mks_gc.allocated_bytes -= obj->size;
            mks_gc.young_freed_objects++;
            mks_gc.young_freed_bytes += obj->size;
            mks_gc.freed_objects++;
            mks_gc.freed_bytes += obj->size;
            gc_free_object(obj);
        } else {
            obj->marked = false;
            if (obj->age == 0) {
                obj->age = 1;
                mks_gc.promoted_to_survivor++;
            } else if (obj->age == 1) {
                obj->age = 2;
                mks_gc.promoted_to_stable++;
            }
            ptr = &obj->next;
        }
    }
}

void gc_collect_young(Environment *global_env, Environment *current_env) {
    if (mks_gc.pause_count > 0) return;

    if (mks_gc.remembered_set_count >= REMEMBERED_SET_MAX) {
        mks_gc.full_fallbacks++;
        gc_collect(global_env, current_env);
        return;
    }

    mks_gc.young_collections++;
    mks_gc.young_since_full++;

    gc_mark_all_iterative(global_env, current_env);
    gc_sweep_young();

    size_t new_young_threshold = mks_gc.allocated_bytes / 4;
    if (new_young_threshold < 256 * 1024) new_young_threshold = 256 * 1024;
    mks_gc.young_threshold = new_young_threshold;

    if (mks_gc.young_since_full >= ORBIT_FULL_GC_INTERVAL) {
        mks_gc.full_fallbacks++;
        mks_gc.young_since_full = 0;
        gc_collect(global_env, current_env);
    }
}

void gc_dump_stats(void) {
    fprintf(stderr,
            "[MKS GC] allocated=%zu threshold=%zu young_threshold=%zu "
            "collections=%zu young=%zu freed=%zu young_freed=%zu "
            "promoted_survivor=%zu promoted_stable=%zu "
            "barrier_calls=%zu old_to_young=%zu full_fallbacks=%zu\n",
            mks_gc.allocated_bytes,
            mks_gc.threshold,
            mks_gc.young_threshold,
            mks_gc.collections,
            mks_gc.young_collections,
            mks_gc.freed_objects,
            mks_gc.young_freed_objects,
            mks_gc.promoted_to_survivor,
            mks_gc.promoted_to_stable,
            mks_gc.barrier_calls,
            mks_gc.old_to_young_edges,
            mks_gc.full_fallbacks);
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



void orbit_touch_object(GCObject *obj) {
    if (obj == NULL) return;
    if (obj->heat < UINT16_MAX) obj->heat++;
    if (obj->heat >= ORBIT_HOT_THRESHOLD)
        obj->flags |= ORBIT_FLAG_HOT;
}

void orbit_touch_value(const RuntimeValue *value) {
    if (value == NULL) return;
    switch (value->type) {
        case VAL_STRING:
            orbit_touch_object((GCObject *)value->data.managed_string);
            break;
        case VAL_ARRAY:
            orbit_touch_object((GCObject *)value->data.managed_array);
            break;
        case VAL_POINTER:
            orbit_touch_object((GCObject *)value->data.managed_pointer);
            break;
        case VAL_OBJECT:
        case VAL_MODULE:
            orbit_touch_object((GCObject *)value->data.obj_env);
            break;
        case VAL_FUNC:
            orbit_touch_object((GCObject *)value->data.func.closure_env);
            break;
        case VAL_BLUEPRINT:
            orbit_touch_object((GCObject *)value->data.blueprint.closure_env);
            break;
        case VAL_STRING_BUILDER:
            orbit_touch_object((GCObject *)value->data.string_builder);
            break;
        default:
            break;
    }
}

int orbit_object_is_hot(const GCObject *obj) {
    if (obj == NULL) return 0;
    return (obj->flags & ORBIT_FLAG_HOT) != 0;
}

void orbit_dump_hot_objects(void) {
#define GC_TYPE_COUNT 6
    int total = 0, hot_count = 0, warm_count = 0, cold_count = 0;
    GCObject *hottest = NULL;
    int type_total[GC_TYPE_COUNT] = {0};
    int type_hot[GC_TYPE_COUNT]   = {0};

    GCObject *obj = mks_gc.head;
    while (obj != NULL) {
        total++;
        if (obj->type < GC_TYPE_COUNT) {
            type_total[obj->type]++;
        }

        if (obj->heat >= ORBIT_HOT_THRESHOLD) {
            hot_count++;
            if (obj->type < GC_TYPE_COUNT) type_hot[obj->type]++;
            if (hottest == NULL || obj->heat > hottest->heat) {
                hottest = obj;
            }
        } else if (obj->age > 0 && obj->heat >= ORBIT_WARM_THRESHOLD) {
            warm_count++;
        } else {
            cold_count++;
        }

        obj = obj->next;
    }

    fprintf(stderr, "[Orbit GC Hot]\n");
    fprintf(stderr, "objects: %d\n", total);
    fprintf(stderr, "hot:  %d\n", hot_count);
    fprintf(stderr, "warm: %d\n", warm_count);
    fprintf(stderr, "cold: %d\n", cold_count);
    if (hottest != NULL) {
        fprintf(stderr, "hottest: ptr=%p type=%s heat=%u age=%u\n",
                (void *)hottest,
                gc_type_name(hottest->type),
                (unsigned)hottest->heat,
                (unsigned)hottest->age);
    }
    fprintf(stderr, "by type:\n");
    for (int t = 0; t < GC_TYPE_COUNT; t++) {
        if (type_total[t] > 0) {
            fprintf(stderr, "  %-7s objects=%-5d hot=%d\n",
                    gc_type_name((GCObjectType)t),
                    type_total[t],
                    type_hot[t]);
        }
    }
#undef GC_TYPE_COUNT
}

/* === ORBIT GC V1: GENERATIONAL COLLECTION === */

static GCObject *gc_obj_of_value(const RuntimeValue *val) {
    if (val == NULL) return NULL;
    switch (val->type) {
        case VAL_STRING:
            return (GCObject *)val->data.managed_string;
        case VAL_ARRAY:
            return (GCObject *)val->data.managed_array;
        case VAL_POINTER:
            return (GCObject *)val->data.managed_pointer;
        case VAL_OBJECT:
        case VAL_MODULE:
            return (GCObject *)val->data.obj_env;
        case VAL_FUNC:
            return (GCObject *)val->data.func.closure_env;
        case VAL_BLUEPRINT:
            return (GCObject *)val->data.blueprint.closure_env;
        case VAL_STRING_BUILDER:
            return (GCObject *)val->data.string_builder;
        default:
            return NULL;
    }
}

static void gc_remembered_set_add(GCObject *obj) {
    if (obj == NULL) return;

    for (int i = 0; i < mks_gc.remembered_set_count; i++) {
        if (mks_gc.remembered_set[i] == obj) return;
    }

    if (mks_gc.remembered_set_count < REMEMBERED_SET_MAX) {
        mks_gc.remembered_set[mks_gc.remembered_set_count++] = obj;
    }
}

void gc_write_barrier(GCObject *owner, const RuntimeValue *value) {
    mks_gc.barrier_calls++;

    if (owner == NULL) return;

    int owner_orbit = gc_obj_orbit(owner);
    if (owner_orbit < ORBIT_2_STABLE) return;

    GCObject *val_obj = gc_obj_of_value(value);
    if (val_obj == NULL) return;

    int val_orbit = gc_obj_orbit(val_obj);
    if (val_orbit >= ORBIT_2_STABLE) return;

    mks_gc.old_to_young_edges++;
    gc_remembered_set_add(owner);
}

void gc_external_alloc(GCObject *obj, size_t bytes) {
    if (obj == NULL) return;
    obj->external_size += bytes;
    mks_gc.allocated_bytes += bytes;
}

void gc_external_free(GCObject *obj, size_t bytes) {
    if (obj == NULL) return;
    obj->external_size = (obj->external_size > bytes) ? obj->external_size - bytes : 0;
    mks_gc.allocated_bytes = (mks_gc.allocated_bytes > bytes) ? mks_gc.allocated_bytes - bytes : 0;
}

size_t gc_object_total_size(const GCObject *obj) {
    if (obj == NULL) return 0;
    return obj->size + obj->external_size;
}
