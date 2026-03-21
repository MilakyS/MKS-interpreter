#ifndef MKS_OUTPUT_H
#define MKS_OUTPUT_H

#include "value.h"
#include "../Parser/AST.h"
#include "../env/env.h"

void print_value(RuntimeValue val);

RuntimeValue eval_output(const ASTNode *node, struct Environment *env);

#endif