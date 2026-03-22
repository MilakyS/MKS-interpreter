#ifndef MONKEYKERNELSYNTAX_CONTROL_FLOW_H
#define MONKEYKERNELSYNTAX_CONTROL_FLOW_H
#include "value.h"
#include "../Parser/AST.h"
#include "../env/env.h"

RuntimeValue eval_block(const ASTNode *node, Environment *env);
RuntimeValue eval_if(const ASTNode *node, Environment *env);
RuntimeValue eval_while(const ASTNode *node, Environment *env);
RuntimeValue eval_for(const ASTNode *node, Environment *env);

#endif //MONKEYKERNELSYNTAX_CONTROL_FLOW_H