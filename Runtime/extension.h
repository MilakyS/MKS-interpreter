#ifndef MKS_EXTENSION_H
#define MKS_EXTENSION_H

#include "value.h"
#include "../Parser/AST.h"
#include "../env/env.h"


typedef enum {
    EXT_ARRAY = 0,
    EXT_STRING = 1,
    EXT_NUMBER = 2
} ExtTarget;

void register_extension(const ASTNode *node, Environment *env);
RuntimeValue dispatch_extension(enum ValueType vtype, unsigned int hash, const char *name, RuntimeValue target, RuntimeValue *args, int arg_count, Environment *env);
void extension_free_all(void);

#endif
