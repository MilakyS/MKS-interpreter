#ifndef MONKEYKERNELSYNTAX_METHODS_H
#define MONKEYKERNELSYNTAX_METHODS_H

#include "value.h"


struct ASTNode;
struct Environment;


typedef RuntimeValue (*MethodHandler)(RuntimeValue target, RuntimeValue *args, int arg_count, struct Environment *env);


typedef struct {
    const char *name;
    unsigned int hash;
    MethodHandler handler;
} MethodEntry;


RuntimeValue eval_method_call(const struct ASTNode *node, struct Environment *env);

#endif //MONKEYKERNELSYNTAX_METHODS_H