#ifndef MONKEYKERNELSYNTAX_ENV_H
#define MONKEYKERNELSYNTAX_ENV_H

#include "../Runtime/value.h"

#define TABLE_SIZE 256

typedef struct EnvVar {
    char *name;
    RuntimeValue value;
    struct EnvVar *next;
} EnvVar;

typedef struct Environment {
    GCObject gc;
    struct EnvVar *buckets[TABLE_SIZE];
    struct Environment *parent;
} Environment;


void env_init(Environment *env);
void env_set(Environment *env, const char *name, RuntimeValue value);
RuntimeValue env_get_fast(const Environment *env, const char *name, unsigned int h);
void env_update_fast(Environment *env, const char *name, unsigned int h, RuntimeValue value);
Environment* env_create_child(Environment *parent);

#endif