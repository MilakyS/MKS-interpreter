#ifndef MKS_OUTPUT_H
#define MKS_OUTPUT_H

#include "value.h"
#include "../Parser/AST.h"
#include "../env/env.h"


RuntimeValue eval_output(const ASTNode *node, struct Environment *env);

#endif