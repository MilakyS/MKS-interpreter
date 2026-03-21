#ifndef MONKEYKERNELSYNTAX_FUNCTIONS_H
#define MONKEYKERNELSYNTAX_FUNCTIONS_H

#include "value.h"
#include "../Parser/AST.h"
#include "../env/env.h"

RuntimeValue eval_func_decl(const ASTNode *node, Environment *env);


RuntimeValue eval_func_call(const ASTNode *node, Environment *env);

#endif //MONKEYKERNELSYNTAX_FUNCTIONS_H