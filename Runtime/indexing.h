

#ifndef MONKEYKERNELSYNTAX_INDEXING_H
#define MONKEYKERNELSYNTAX_INDEXING_H
#include "value.h"
#include "../Parser/AST.h"
#include "../env/env.h"


RuntimeValue eval_index(const ASTNode *node, Environment *env);

RuntimeValue eval_index_assign(const ASTNode *node, Environment *env);

#endif