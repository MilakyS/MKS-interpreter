#ifndef CMINUSINTERPRETATOR_EVAL_H
#define CMINUSINTERPRETATOR_EVAL_H

#include "../Parser/AST.h"
#include "../Runtime/value.h"
#include "../env/env.h"

#define TABLE_SIZE 256


RuntimeValue eval(const ASTNode *node, Environment *env);

RuntimeValue eval_repeat(const ASTNode *node, Environment *env);

#endif
