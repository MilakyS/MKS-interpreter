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
    size_t version;
    struct Environment *parent;
} Environment;

void env_init(Environment *env);
Environment *env_create_child(Environment *parent);
void env_free(const Environment *env);

void env_set(Environment *env, const char *name, RuntimeValue value);
void env_set_fast(Environment *env, const char *name, unsigned int h, RuntimeValue value);

RuntimeValue env_get_fast(const Environment *env, const char *name, unsigned int h);
void env_update_fast(Environment *env, const char *name, unsigned int h, RuntimeValue value);
bool env_try_get(const Environment *env, const char *name, unsigned int h, RuntimeValue *out);
/* Returns pointer to EnvVar if found, else NULL (no allocation). */
struct EnvVar *env_get_entry(const Environment *env, const char *name, unsigned int h);
struct EnvVar *env_get_entry_with_owner(const Environment *env, const char *name, unsigned int h, struct Environment **owner);

#endif
