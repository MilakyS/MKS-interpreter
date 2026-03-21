//
// Created by MilakyS on 21.03.2026.
//

#ifndef MONKEYKERNELSYNTAX_OPERATORS_H
#define MONKEYKERNELSYNTAX_OPERATORS_H


#include "../Runtime/value.h"
#include "../Parser/AST.h"
#include "../env/env.h"

RuntimeValue eval_binop(const ASTNode *node, Environment *env);

#endif //MONKEYKERNELSYNTAX_OPERATORS_H
