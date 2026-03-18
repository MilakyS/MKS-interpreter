#ifndef CMINUSINTERPRETATOR_EVAL_H
#define CMINUSINTERPRETATOR_EVAL_H

#include "../Parser/AST.h"


enum ValueType {
    VAL_INT,
    VAL_STRING
};

typedef struct {
    enum ValueType type;
    union {
        int int_value;
        char *string_value;
    } data;
} RuntimeValue;


typedef struct EnvVar {
    char *name;
    RuntimeValue value;
    struct EnvVar *next;
} EnvVar;

typedef struct Environment {
    EnvVar *head;
} Environment;

void env_init(Environment *env);
void env_set(Environment *env, const char *name, RuntimeValue value);
RuntimeValue env_get(Environment *env, const char *name);

// Теперь eval возвращает не int, а нашу универсальную структуру!
RuntimeValue eval(ASTNode *node, Environment *env);

#endif //CMINUSINTERPRETATOR_EVAL_H