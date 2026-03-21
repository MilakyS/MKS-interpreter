#include "env.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "../Runtime/value.h"

static inline unsigned int get_hash(const char *s) {
    unsigned int hash = 5381;
    int c;
    while ((c = *s++)) {
        hash = ((hash << 5) + hash) + c;
    }
    return hash;
}

void env_init(Environment *env) {
    for (size_t i = 0; i < TABLE_SIZE; i++) {
        env->buckets[i] = NULL;
    }
    env->parent = NULL;
}

Environment* env_create_child(Environment *parent) {
    Environment *env = (Environment*)malloc(sizeof(Environment));
    env_init(env);
    env->parent = parent;
    return env;
}

void env_free(const Environment *env) {
    if (!env) return;

    for (size_t i = 0; i < TABLE_SIZE; i++) {
        EnvVar *current = env->buckets[i];
        while (current != NULL) {
            EnvVar *next = current->next;

            free(current->name);

            if (current->value.type == VAL_STRING) {
                free(current->value.data.string_value);
            }

            if (current->value.type == VAL_ARRAY) {
                free(current->value.data.array_data.elements);
            }

            free(current);
            current = next;
        }
    }
}

void env_set(Environment *env, const char *name, RuntimeValue value) {
    unsigned int index = get_hash(name) % TABLE_SIZE;

    if (value.type == VAL_RETURN) {
        value.type = value.original_type;
    }

    EnvVar *current = env->buckets[index];

    while (current != NULL) {
        if (strcmp(current->name, name) == 0) {

            if (current->value.type == VAL_STRING) {
                free(current->value.data.string_value);
            }

            current->value = value;

            if (value.type == VAL_STRING) {
                current->value.data.string_value =
                    value.data.string_value ? strdup(value.data.string_value) : strdup("");
            }

            return;
        }
        current = current->next;
    }

    EnvVar *new_var = (EnvVar *)malloc(sizeof(EnvVar));
    new_var->name = strdup(name);
    new_var->value = value;

    if (value.type == VAL_STRING) {
        new_var->value.data.string_value =
            value.data.string_value ? strdup(value.data.string_value) : strdup("");
    }

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

            if (current->value.type == VAL_STRING) {
                free(current->value.data.string_value);
            }

            current->value = value;

            if (value.type == VAL_STRING) {
                current->value.data.string_value =
                    value.data.string_value ? strdup(value.data.string_value) : strdup("");
            }

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