#include "env.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "../Runtime/value.h"
#include "../GC/gc.h"
#include "../Utils/hash.h"

void env_init(Environment *env) {
    for (size_t i = 0; i < TABLE_SIZE; i++) {
        env->buckets[i] = NULL;
    }
    env->parent = NULL;
}

Environment* env_create_child(Environment *parent) {
    Environment *env = (Environment*)gc_alloc(sizeof(Environment), GC_OBJ_ENV);
    env_init(env);
    env->parent = parent;
    return env;
}

void env_free(const Environment *env) {
    (void)env;
}

void env_set(Environment *env, const char *name, RuntimeValue value) {
    unsigned int h = get_hash(name);
    unsigned int index = h % TABLE_SIZE;

    if (value.type == VAL_RETURN) {
        value.type = value.original_type;
    }

    EnvVar *current = env->buckets[index];
    while (current != NULL) {
        if (strcmp(current->name, name) == 0) {
            current->value = value;
            return;
        }
        current = current->next;
    }

    EnvVar *new_var = (EnvVar *)malloc(sizeof(EnvVar));
    new_var->name = strdup(name);
    new_var->value = value;
    new_var->next = env->buckets[index];
    env->buckets[index] = new_var;
}

RuntimeValue env_get_fast(const Environment *env, const char *name, unsigned int h) {
    unsigned int index = h % TABLE_SIZE;

    const EnvVar *current = env->buckets[index];
    while (current != NULL) {
        if (strcmp(current->name, name) == 0) {
            return current->value;
        }
        current = current->next;
    }

    if (env->parent != NULL) {
        return env_get_fast(env->parent, name, h);
    }

    printf("Runtime Error: Undefined variable '%s'\n", name);
    exit(1);
}

void env_update_fast(Environment *env, const char *name, unsigned int h, RuntimeValue value) {
    unsigned int index = h % TABLE_SIZE;

    EnvVar *current = env->buckets[index];
    while (current != NULL) {
        if (strcmp(current->name, name) == 0) {
            current->value = value;
            return;
        }
        current = current->next;
    }

    if (env->parent != NULL) {
        env_update_fast(env->parent, name, h, value);
        return;
    }

    printf("Runtime Error: Variable '%s' is not defined!\n", name);
    exit(1);
}