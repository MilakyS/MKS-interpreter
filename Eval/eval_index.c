#include "eval_index.h"

#include "../GC/gc.h"
#include "../Runtime/errors.h"
#include "../Runtime/indexing.h"

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
    const RuntimeValue result = runtime_get_index(target, idx_val);
    if (target_rooted) {
        gc_pop_root();
    }
    return result;
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
        runtime_set_index(target, idx_val, val);

        if (target_rooted) {
            gc_pop_root();
        }
        if (val_rooted) {
            gc_pop_root();
        }
        return val;
    }

    if (val_rooted) {
        gc_pop_root();
    }
    runtime_error("Invalid index assignment target");
    return make_null();
}
