#ifndef CMINUSINTERPRETATOR_EVAL_H
#define CMINUSINTERPRETATOR_EVAL_H

#include "../Parser/AST.h"
#include "../Runtime/value.h"
#include "../env/env.h"

RuntimeValue eval(const ASTNode *node, Environment *env);

#endif