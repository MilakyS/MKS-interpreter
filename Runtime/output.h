#ifndef MKS_OUTPUT_H
#define MKS_OUTPUT_H

#include "value.h"
#include "../Parser/AST.h"
#include "../env/env.h"


RuntimeValue eval_output(const ASTNode *node, struct Environment *env);
RuntimeValue runtime_write_values(const RuntimeValue *args, int count, bool is_newline);

void print_value(const RuntimeValue *val);

#endif
