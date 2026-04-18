#include "indexing.h"
#include "errors.h"
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
    const int target_rooted = gc_push_root_if_needed(&target);

    const RuntimeValue idx_val = unwrap(eval(node->data.index.index, env));
    int i = (int)idx_val.data.float_value;

    if (target.type == VAL_STRING) {
        const ManagedString *str = target.data.managed_string;
        const int len = (int)str->len;

        if (i < 0 || i >= len) {
            if (target_rooted) {
                gc_pop_root();
            }
            runtime_error("String index %d out of bounds (length %d)", i, len);
        }

        char *tmp = (char *)malloc(2);
        if (tmp == NULL) {
            if (target_rooted) {
                gc_pop_root();
            }
            runtime_error("Out of memory in string indexing");
        }

        tmp[0] = str->data[i];
        tmp[1] = '\0';

        if (target_rooted) {
            gc_pop_root();
        }
        return make_string_owned(tmp, 1);
    }

    if (target.type == VAL_ARRAY) {
        const ManagedArray *arr = target.data.managed_array;

        if (i < 0 || i >= arr->count) {
            if (target_rooted) {
                gc_pop_root();
            }
            runtime_error("Array index %d out of bounds (size %d)", i, arr->count);
        }

        const RuntimeValue result = arr->elements[i];
        if (target_rooted) {
            gc_pop_root();
        }
        return result;
    }

    if (target_rooted) {
        gc_pop_root();
    }
    runtime_error("Type is not indexable");
    return make_null();
}

RuntimeValue eval_index_assign(const ASTNode *node, Environment *env) {
    const ASTNode *lhs = node->data.index_assign.left;
    const ASTNode *rhs = node->data.index_assign.right;

    RuntimeValue val = unwrap(eval(rhs, env));
    const int val_rooted = gc_push_root_if_needed(&val);

    if (lhs->type == AST_IDENTIFIER) {
        env_update_fast(env, lhs->data.identifier.name, lhs->data.identifier.id_hash, val);
        if (val_rooted) {
            gc_pop_root();
        }
        return val;
    }

    if (lhs->type == AST_INDEX) {
        RuntimeValue target = unwrap(eval(lhs->data.index.target, env));
        const int target_rooted = gc_push_root_if_needed(&target);

        const RuntimeValue idx_val = unwrap(eval(lhs->data.index.index, env));
        const int i = (int)idx_val.data.float_value;

        if (target.type != VAL_ARRAY) {
            if (target_rooted) gc_pop_root();
            if (val_rooted) gc_pop_root();
            runtime_error("Only arrays support index assignment");
        }

        const ManagedArray *arr = target.data.managed_array;
        if (i < 0 || i >= arr->count) {
            if (target_rooted) gc_pop_root();
            if (val_rooted) gc_pop_root();
            runtime_error("Array index %d out of bounds in assignment (size %d)", i, arr->count);
        }

        arr->elements[i] = val;

        if (target_rooted) gc_pop_root();
        if (val_rooted) gc_pop_root();
        return val;
    }

    if (val_rooted) {
        gc_pop_root();
    }
    runtime_error("Invalid index assignment target");
    return make_null();
}
