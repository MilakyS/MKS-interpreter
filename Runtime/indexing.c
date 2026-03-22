#include "indexing.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../Eval/eval.h"
#include "../GC/gc.h"

RuntimeValue eval_index(const ASTNode *node, Environment *env) {
    RuntimeValue target = eval(node->data.Index.target, env);
    if (target.type == VAL_RETURN) target.type = target.original_type;
    gc_push_root(&target);

    RuntimeValue idx_val = eval(node->data.Index.index, env);
    int i = (int)idx_val.data.float_value;

    if (target.type == VAL_STRING) {
        const char* s_data = target.data.managed_string->data;
        int len = (int)strlen(s_data);

        if (i < 0 || i >= len) {
            printf("Runtime Error: String index %d out of bounds (length %d)\n", i, len);
            exit(1);
        }

        char tmp[2] = { s_data[i], '\0' };
        gc_pop_root();
        return make_string(tmp);
    }

    if (target.type == VAL_ARRAY) {
        ManagedArray *arr = target.data.managed_array;
        if (i < 0 || i >= arr->count) {
            printf("Runtime Error: Array index %d out of bounds (size %d)\n", i, arr->count);
            exit(1);
        }

        RuntimeValue res = arr->elements[i];
        gc_pop_root();
        return res;
    }

    printf("Runtime Error: Type is not indexable\n");
    exit(1);
}

RuntimeValue eval_index_assign(const ASTNode *node, Environment *env) {
    ASTNode *lhs = node->data.IndexAssign.left;
    ASTNode *rhs = node->data.IndexAssign.right;

    RuntimeValue val = eval(rhs, env);
    if (val.type == VAL_RETURN) val.type = val.original_type;
    gc_push_root(&val);

    if (lhs->type == AST_IDENTIFIER) {
        env_update_fast(env, lhs->data.identifier.name, lhs->data.identifier.id_hash, val);
        gc_pop_root();
        return val;
    }

    if (lhs->type == AST_INDEX) {
        RuntimeValue target = eval(lhs->data.Index.target, env);
        gc_push_root(&target);

        RuntimeValue idx_expr = eval(lhs->data.Index.index, env);
        int i = (int)idx_expr.data.float_value;

        if (target.type == VAL_ARRAY) {
            ManagedArray *arr = target.data.managed_array;
            if (i < 0 || i >= arr->count) {
                printf("Runtime Error: Array index %d out of bounds in assignment\n", i);
                exit(1);
            }

            arr->elements[i] = val;

            gc_pop_root();
            gc_pop_root();
            return val;
        }
    }

    printf("Runtime Error: Invalid index assignment target\n");
    exit(1);
}