#include "env.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>

#include "../Runtime/value.h"
#include "../GC/gc.h"
#include "../Utils/hash.h"

static char *env_strdup(const char *src) {
    if (src == NULL) {
        src = "";
    }

    const size_t len = strlen(src);
    char *copy = (char *)malloc(len + 1);
    if (copy == NULL) {
        fprintf(stderr, "[MKS ENV] Fatal: Out of memory duplicating variable name\n");
        exit(1);
    }

    memcpy(copy, src, len + 1);
    return copy;
}

static EnvVar **env_alloc_buckets(size_t bucket_count) {
    EnvVar **buckets = (EnvVar **)calloc(bucket_count, sizeof(EnvVar *));
    if (buckets == NULL) {
        fprintf(stderr, "[MKS ENV] Fatal: Out of memory allocating env buckets\n");
        exit(1);
    }
    return buckets;
}

static void env_resize(Environment *env, size_t new_bucket_count) {
    // Optimization: Bucket counts are always powers of 2 (8, 16, 32...).
    // This allows replacing the expensive modulo operator (%) with bitwise AND (&).
    // Ensure new_bucket_count is a power of 2: (n & (n - 1)) == 0.
    assert(new_bucket_count > 0 && (new_bucket_count & (new_bucket_count - 1)) == 0);

    EnvVar **new_buckets = env_alloc_buckets(new_bucket_count);

    for (size_t i = 0; i < env->bucket_count; i++) {
        EnvVar *entry = env->buckets[i];
        while (entry != NULL) {
            EnvVar *next = entry->next;

            const size_t new_index = entry->hash & (new_bucket_count - 1);
            entry->next = new_buckets[new_index];
            new_buckets[new_index] = entry;

            entry = next;
        }
    }

    free(env->buckets);
    env->buckets = new_buckets;
    env->bucket_count = new_bucket_count;
}

static void env_maybe_grow(Environment *env) {
    if (env->bucket_count == 0) {
        env->bucket_count = ENV_INITIAL_BUCKET_COUNT;
        env->buckets = env_alloc_buckets(env->bucket_count);
        return;
    }

    if ((env->entry_count + 1) * ENV_MAX_LOAD_DEN >= env->bucket_count * ENV_MAX_LOAD_NUM) {
        size_t new_bucket_count = env->bucket_count * 2;
        if (new_bucket_count < ENV_INITIAL_BUCKET_COUNT) {
            new_bucket_count = ENV_INITIAL_BUCKET_COUNT;
        }
        // Doubling ensures it remains a power of 2.
        env_resize(env, new_bucket_count);
    }
}

void env_init(Environment *env) {
    // ENV_INITIAL_BUCKET_COUNT (8) is a power of 2.
    assert(ENV_INITIAL_BUCKET_COUNT > 0 && (ENV_INITIAL_BUCKET_COUNT & (ENV_INITIAL_BUCKET_COUNT - 1)) == 0);

    env->bucket_count = ENV_INITIAL_BUCKET_COUNT;
    env->entry_count = 0;
    env->buckets = env_alloc_buckets(env->bucket_count);
    env->parent = NULL;
}

Environment *env_create_child(Environment *parent) {
    Environment *env = (Environment *)gc_alloc(sizeof(Environment), GC_OBJ_ENV);
    env_init(env);
    env->parent = parent;
    return env;
}

void env_free(const Environment *env) {
    (void)env;
}

void env_set_fast(Environment *env, const char *name, unsigned int h, RuntimeValue value) {
    if (value.type == VAL_RETURN) {
        value.type = value.original_type;
    }

    if (env->bucket_count == 0 || env->buckets == NULL) {
        env->bucket_count = ENV_INITIAL_BUCKET_COUNT;
        env->entry_count = 0;
        env->buckets = env_alloc_buckets(env->bucket_count);
    }

    // Optimization: bucket_count is a power of 2, so use bitwise AND instead of modulo (%).
    const size_t index = h & (env->bucket_count - 1);

    EnvVar *current = env->buckets[index];
    while (current != NULL) {
        if (current->hash == h && strcmp(current->name, name) == 0) {
            current->value = value;
            return;
        }
        current = current->next;
    }

    env_maybe_grow(env);

    const size_t final_index = h & (env->bucket_count - 1);

    EnvVar *new_var = (EnvVar *)malloc(sizeof(EnvVar));
    if (new_var == NULL) {
        fprintf(stderr, "[MKS ENV] Fatal: Out of memory allocating EnvVar\n");
        exit(1);
    }

    new_var->name = env_strdup(name);
    new_var->hash = h;
    new_var->value = value;
    new_var->next = env->buckets[final_index];
    env->buckets[final_index] = new_var;

    env->entry_count++;
}

void env_set(Environment *env, const char *name, RuntimeValue value) {
    env_set_fast(env, name, get_hash(name), value);
}

RuntimeValue env_get_fast(const Environment *env, const char *name, unsigned int h) {
    const Environment *cur_env = env;

    while (cur_env != NULL) {
        if (cur_env->bucket_count > 0 && cur_env->buckets != NULL) {
            // Optimization: bucket_count is a power of 2, so use bitwise AND instead of modulo (%).
            const size_t index = h & (cur_env->bucket_count - 1);
            const EnvVar *current = cur_env->buckets[index];

            while (current != NULL) {
                if (current->hash == h && strcmp(current->name, name) == 0) {
                    return current->value;
                }
                current = current->next;
            }
        }

        cur_env = cur_env->parent;
    }

    fprintf(stderr, "Runtime Error: Undefined variable '%s'\n", name);
    exit(1);
}

void env_update_fast(Environment *env, const char *name, unsigned int h, RuntimeValue value) {
    if (value.type == VAL_RETURN) {
        value.type = value.original_type;
    }

    Environment *cur_env = env;

    while (cur_env != NULL) {
        if (cur_env->bucket_count > 0 && cur_env->buckets != NULL) {
            // Optimization: bucket_count is a power of 2, so use bitwise AND instead of modulo (%).
            const size_t index = h & (cur_env->bucket_count - 1);
            EnvVar *current = cur_env->buckets[index];

            while (current != NULL) {
                if (current->hash == h && strcmp(current->name, name) == 0) {
                    current->value = value;
                    return;
                }
                current = current->next;
            }
        }

        cur_env = cur_env->parent;
    }

    fprintf(stderr, "Runtime Error: Variable '%s' is not defined!\n", name);
    exit(1);
}