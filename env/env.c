#include "env.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "../Runtime/value.h"
#include "../GC/gc.h"
#include "../Utils/hash.h"
#include "../Runtime/errors.h"
#include "../Runtime/context.h"
#include "../std/watch.h"

static char *env_strdup(const char *src) {
    if (src == NULL) {
        src = "";
    }

    const size_t len = strlen(src);
    char *copy = (char *)malloc(len + 1);
    if (copy == NULL) {
        runtime_error("Out of memory duplicating variable name");
    }

    memcpy(copy, src, len + 1);
    return copy;
}

static inline int env_is_power_of_two(const size_t x) {
    return x != 0 && (x & (x - 1)) == 0;
}

static inline size_t env_bucket_index(const unsigned int hash, const size_t bucket_count) {
    return (size_t)hash & (bucket_count - 1);
}

static EnvVar **env_alloc_buckets(const size_t bucket_count) {
    if (!env_is_power_of_two(bucket_count)) {
        runtime_error("env bucket_count must be a power of two");
    }

    EnvVar **buckets = (EnvVar **)calloc(bucket_count, sizeof(EnvVar *));
    if (buckets == NULL) {
        runtime_error("Out of memory allocating env buckets");
    }
    return buckets;
}

static void env_resize(Environment *env, const size_t new_bucket_count) {
    if (!env_is_power_of_two(new_bucket_count)) {
        runtime_error("env new_bucket_count must be a power of two");
    }

    EnvVar **new_buckets = env_alloc_buckets(new_bucket_count);

    for (size_t i = 0; i < env->bucket_count; i++) {
        EnvVar *entry = env->buckets[i];
        while (entry != NULL) {
            EnvVar *next = entry->next;

            const size_t new_index = env_bucket_index(entry->hash, new_bucket_count);
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
        size_t new_bucket_count = env->bucket_count << 1;
        if (new_bucket_count < ENV_INITIAL_BUCKET_COUNT) {
            new_bucket_count = ENV_INITIAL_BUCKET_COUNT;
        }
        env_resize(env, new_bucket_count);
    }
}

void env_init(Environment *env) {
    env->bucket_count = ENV_INITIAL_BUCKET_COUNT;
    env->entry_count = 0;
    env->version = 0;
    env->buckets = env_alloc_buckets(env->bucket_count);
    env->parent = NULL;
}

Environment *env_create_child(Environment *parent) {
    Environment *env = gc_alloc(sizeof(Environment), GC_OBJ_ENV);
    env_init(env);
    env->parent = parent;
    return env;
}

void env_free(const Environment *env) {
    (void)env;
}

void env_set_fast(Environment *env, const char *name, const unsigned int h, RuntimeValue value) {
    if (value.type == VAL_RETURN) {
        value.type = value.original_type;
    }

    if (env->bucket_count == 0 || env->buckets == NULL) {
        env->bucket_count = ENV_INITIAL_BUCKET_COUNT;
        env->entry_count = 0;
        env->buckets = env_alloc_buckets(env->bucket_count);
    }

    const size_t index = env_bucket_index(h, env->bucket_count);

    EnvVar *current = env->buckets[index];
    while (current != NULL) {
        if (current->hash == h && strcmp(current->name, name) == 0) {
            current->value = value;
            if (watch_has_any()) {
                watch_trigger(name, h, env, &value);
            }
            return;
        }
        current = current->next;
    }

    env_maybe_grow(env);

    const size_t final_index = env_bucket_index(h, env->bucket_count);

    EnvVar *new_var = malloc(sizeof(EnvVar));
    if (new_var == NULL) {
        runtime_error("Out of memory allocating EnvVar");
    }

    new_var->name = env_strdup(name);
    new_var->hash = h;
    new_var->value = value;
    new_var->next = env->buckets[final_index];
    env->buckets[final_index] = new_var;

    env->entry_count++;
    env->version = ++mks_context_current()->env_shape_epoch;
}

void env_set(Environment *env, const char *name, const RuntimeValue value) {
    env_set_fast(env, name, get_hash(name), value);
}

RuntimeValue env_get_fast(const Environment *env, const char *name, unsigned int h) {
    const Environment *cur_env = env;

    while (cur_env != NULL) {
        if (cur_env->bucket_count > 0 && cur_env->buckets != NULL) {
            const size_t index = env_bucket_index(h, cur_env->bucket_count);
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

    runtime_error("Undefined variable '%s'", name);
    return make_null();
}

bool env_try_get(const Environment *env, const char *name, unsigned int h, RuntimeValue *out) {
    const Environment *cur_env = env;

    while (cur_env != NULL) {
        if (cur_env->bucket_count > 0 && cur_env->buckets != NULL) {
            const size_t index = env_bucket_index(h, cur_env->bucket_count);
            const EnvVar *current = cur_env->buckets[index];

            while (current != NULL) {
                if (current->hash == h && strcmp(current->name, name) == 0) {
                    if (out != NULL) {
                        *out = current->value;
                    }
                    return true;
                }
                current = current->next;
            }
        }

        cur_env = cur_env->parent;
    }

    return false;
}

EnvVar *env_get_entry(const Environment *env, const char *name, const unsigned int h) {
    return env_get_entry_with_owner(env, name, h, NULL);
}

EnvVar *env_get_entry_with_owner(const Environment *env, const char *name, const unsigned int h, Environment **owner) {
    const Environment *cur_env = env;

    while (cur_env != NULL) {
        if (cur_env->bucket_count > 0 && cur_env->buckets != NULL) {
            const size_t index = env_bucket_index(h, cur_env->bucket_count);
            EnvVar *current = cur_env->buckets[index];

            while (current != NULL) {
                if (current->hash == h && strcmp(current->name, name) == 0) {
                    if (owner != NULL) {
                        *owner = (Environment *)cur_env;
                    }
                    return current;
                }
                current = current->next;
            }
        }
        cur_env = cur_env->parent;
    }
    return NULL;
}

void env_update_fast(Environment *env, const char *name, unsigned int h, RuntimeValue value) {
    if (value.type == VAL_RETURN) {
        value.type = value.original_type;
    }

    const Environment *cur_env = env;

    while (cur_env != NULL) {
        if (cur_env->bucket_count > 0 && cur_env->buckets != NULL) {
            const size_t index = env_bucket_index(h, cur_env->bucket_count);
            EnvVar *current = cur_env->buckets[index];

            while (current != NULL) {
                if (current->hash == h && strcmp(current->name, name) == 0) {
                    current->value = value;
                    if (watch_has_any()) {
                        watch_trigger(name, h, env, &value);
                    }
                    return;
                }
                current = current->next;
            }
        }

        cur_env = cur_env->parent;
    }

    runtime_error("Variable '%s' is not defined!", name);
    return;
}
