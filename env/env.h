#ifndef MONKEYKERNELSYNTAX_ENV_H
#define MONKEYKERNELSYNTAX_ENV_H

#include <stddef.h>
#include "../Runtime/value.h"

#define ENV_INITIAL_BUCKET_COUNT 8
#define ENV_MAX_LOAD_NUM 3
#define ENV_MAX_LOAD_DEN 4

typedef struct EnvVar {
    char *name;
    unsigned int hash;
    RuntimeValue value;
    struct EnvVar *next;
} EnvVar;

typedef struct Environment {
    GCObject gc;
    EnvVar **buckets;
    size_t bucket_count;
    size_t entry_count;
    struct Environment *parent;
} Environment;

void env_init(Environment *env);
Environment *env_create_child(Environment *parent);
void env_free(const Environment *env);

void env_set(Environment *env, const char *name, RuntimeValue value);
void env_set_fast(Environment *env, const char *name, unsigned int h, RuntimeValue value);

RuntimeValue env_get_fast(const Environment *env, const char *name, unsigned int h);
void env_update_fast(Environment *env, const char *name, unsigned int h, RuntimeValue value);

#endif