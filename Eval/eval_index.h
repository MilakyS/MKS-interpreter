#ifndef MKS_EVAL_INDEX_H
#define MKS_EVAL_INDEX_H

#include "eval.h"

RuntimeValue eval_index(const ASTNode *node, Environment *env);
RuntimeValue eval_index_assign(const ASTNode *node, Environment *env);

#endif
