#ifndef CMINUSINTERPRETATOR_EVAL_H
#define CMINUSINTERPRETATOR_EVAL_H

#include "../Parser/AST.h"

#define TABLE_SIZE 256

enum ValueType {
    VAL_INT,
    VAL_STRING,
    VAL_FUNC,
    VAL_RETURN
};

typedef struct RuntimeValue {
    enum ValueType type;
    enum ValueType original_type;
    union {
        int int_value;
        char *string_value;
        struct ASTNode *func_node;
    } data;
} RuntimeValue;

typedef struct EnvVar {
    char *name;
    RuntimeValue value;
    struct EnvVar *next;
} EnvVar;

typedef struct Environment {
    EnvVar *buckets[TABLE_SIZE];
    struct Environment *parent;
} Environment;

void env_init(Environment *env);
void env_free(const Environment *env);
void env_set(Environment *env, const char *name, RuntimeValue value);
RuntimeValue env_get(const Environment *env, const char *name);
RuntimeValue env_get_fast(const Environment *env, const char *name, const unsigned int h);
void env_update_fast(Environment *env, const char *name, const unsigned int h, RuntimeValue value);

RuntimeValue make_int(int val);
RuntimeValue make_string(const char *str);
RuntimeValue eval(const ASTNode *node, Environment *env);

#endif