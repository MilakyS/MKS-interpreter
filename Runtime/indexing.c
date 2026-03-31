#include "indexing.h"
#include <stdio.h>
#include <stdlib.h>

#include "../Eval/eval.h"
#include "../GC/gc.h"

static inline RuntimeValue unwrap(RuntimeValue v) {
    if (v.type == VAL_RETURN) {
        v.type = v.original_type;
    }
    return v;
}

RuntimeValue eval_index(const ASTNode *node, Environment *env) {
    RuntimeValue target = unwrap(eval(node->data.index.target, env));
    gc_push_root(&target);

    const RuntimeValue idx_val = unwrap(eval(node->data.index.index, env));
    int i = (int)idx_val.data.float_value;

    if (target.type == VAL_STRING) {
        const ManagedString *str = target.data.managed_string;
        const int len = (int)str->len;

        if (i < 0 || i >= len) {
            gc_pop_root();
            fprintf(stderr, "Runtime Error: String index %d out of bounds (length %d)\n", i, len);
            exit(1);
        }

        char *tmp = (char *)malloc(2);
        if (tmp == NULL) {
            gc_pop_root();
            fprintf(stderr, "Runtime Error: Out of memory in string indexing\n");
            exit(1);
        }

        tmp[0] = str->data[i];
        tmp[1] = '\0';

        gc_pop_root();
        return make_string_owned(tmp, 1);
    }

    if (target.type == VAL_ARRAY) {
        const ManagedArray *arr = target.data.managed_array;

        if (i < 0 || i >= arr->count) {
            gc_pop_root();
            fprintf(stderr, "Runtime Error: Array index %d out of bounds (size %d)\n", i, arr->count);
            exit(1);
        }

        const RuntimeValue result = arr->elements[i];
        gc_pop_root();
        return result;
    }

    gc_pop_root();
    fprintf(stderr, "Runtime Error: Type is not indexable\n");
    exit(1);
}

RuntimeValue eval_index_assign(const ASTNode *node, Environment *env) {
    const ASTNode *lhs = node->data.index_assign.left;
    const ASTNode *rhs = node->data.index_assign.right;

    RuntimeValue val = unwrap(eval(rhs, env));
    gc_push_root(&val);

    if (lhs->type == AST_IDENTIFIER) {
        env_update_fast(env, lhs->data.identifier.name, lhs->data.identifier.id_hash, val);
        gc_pop_root();
        return val;
    }

    if (lhs->type == AST_INDEX) {
        RuntimeValue target = unwrap(eval(lhs->data.index.target, env));
        gc_push_root(&target);

        const RuntimeValue idx_val = unwrap(eval(lhs->data.index.index, env));
        const int i = (int)idx_val.data.float_value;

        if (target.type != VAL_ARRAY) {
            gc_pop_root();
            gc_pop_root();
            fprintf(stderr, "Runtime Error: Only arrays support index assignment\n");
            exit(1);
        }

        const ManagedArray *arr = target.data.managed_array;
        if (i < 0 || i >= arr->count) {
            gc_pop_root();
            gc_pop_root();
            fprintf(stderr, "Runtime Error: Array index %d out of bounds in assignment (size %d)\n", i, arr->count);
            exit(1);
        }

        arr->elements[i] = val;

        gc_pop_root();
        gc_pop_root();
        return val;
    }

    gc_pop_root();
    fprintf(stderr, "Runtime Error: Invalid index assignment target\n");
    exit(1);
}