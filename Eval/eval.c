#include "eval.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../Lexer/lexer.h"
#include "../Parser/parser.h"
#include "../Runtime/value.h"
#include "../env/env.h"
#include "../GC/gc.h"

#include "../Runtime/output.h"
#include "../Runtime/operators.h"
#include "../Runtime/indexing.h"
#include "../Runtime/methods.h"
#include "../Runtime/functions.h"
#include "../Runtime/control_flow.h"

RuntimeValue eval(const ASTNode *node, Environment *env) {
    if (node == NULL) return make_int(0);

    switch (node->type) {
        case AST_NUMBER: return make_int(node->data.number_value);
        case AST_STRING: return make_string(node->data.string_value);
        case AST_IDENTIFIER:
            return env_get_fast(env, node->data.identifier.name, node->data.identifier.id_hash);

        case AST_ARRAY: {
            int count = node->data.Array.item_count;
            RuntimeValue arr_val = make_array(count);

            gc_push_root(&arr_val);

            for (int i = 0; i < count; i++) {
                RuntimeValue elem = eval(node->data.Array.items[i], env);
                if (elem.type == VAL_RETURN) elem.type = elem.original_type;

                arr_val.data.managed_array->elements[i] = elem;
                arr_val.data.managed_array->count++;
            }

            gc_pop_root();
            return arr_val;
        }

        case AST_VAR_DECL: {
            RuntimeValue val = eval(node->data.var_decl.value, env);
            gc_push_root(&val);

            env_set(env, node->data.var_decl.name, val);

            gc_pop_root();
            return val;
        }

        case AST_ASSIGN: {
            RuntimeValue val = eval(node->data.Assign.value, env);
            gc_push_root(&val);

            env_update_fast(env, node->data.Assign.name, node->data.Assign.id_hash, val);

            gc_pop_root();
            return val;
        }

        case AST_FUNC_DECL:   return eval_func_decl(node, env);
        case AST_FUNC_CALL:   return eval_func_call(node, env);
        case AST_METHOD_CALL: return eval_method_call(node, env);

        case AST_RETURN: {
            RuntimeValue val = eval(node->data.Return.value, env);
            val.original_type = val.type;
            val.type = VAL_RETURN;
            return val;
        }

        case AST_BINOP:  return eval_binop(node, env);
        case AST_OUTPUT: return eval_output(node, env);
        case AST_INDEX:  return eval_index(node, env);
        case AST_INDEX_ASSIGN: return eval_index_assign(node, env);

        case AST_BLOCK:    return eval_block(node, env);
        case AST_IF_BLOCK: return eval_if(node, env);
        case AST_WHILE:    return eval_while(node, env);
        case AST_FOR:      return eval_for(node, env);

        default: exit(1);
    }
}