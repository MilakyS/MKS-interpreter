#include "control_flow.h"
#include "../Eval/eval.h"
#include "../GC/gc.h"
#include <stdlib.h>

RuntimeValue eval_block(const ASTNode *node, Environment *env) {
    RuntimeValue last = make_int(0);
    for (size_t i = 0; i < node->data.Block.count; i++) {
        if (mks_gc.allocated_bytes > mks_gc.threshold) {
            gc_collect(env, env);
        }

        last = eval(node->data.Block.items[i], env);
        if (last.type == VAL_RETURN) return last;
    }
    return last;
}

RuntimeValue eval_if(const ASTNode *node, Environment *env) {
    RuntimeValue cond = eval(node->data.IfBlck.condition, env);

    if (cond.data.float_value != 0.0) {
        return eval(node->data.IfBlck.body, env);
    } else if (node->data.IfBlck.else_body) {
        return eval(node->data.IfBlck.else_body, env);
    }
    return make_int(0);
}

RuntimeValue eval_while(const ASTNode *node, Environment *env) {
    while (eval(node->data.While.condition, env).data.float_value != 0.0) {
        if (mks_gc.allocated_bytes > mks_gc.threshold) {
            gc_collect(env, env);
        }

        RuntimeValue res = eval(node->data.While.body, env);
        if (res.type == VAL_RETURN) return res;
    }
    return make_int(0);
}

RuntimeValue eval_for(const ASTNode *node, Environment *env) {
    Environment *local = env_create_child(env);

    gc_push_env(local);

    if (node->data.For.init != NULL) {
        eval(node->data.For.init, local);
    }

    while (1) {
        if (mks_gc.allocated_bytes > mks_gc.threshold) {
            gc_collect(env, local);
        }

        if (node->data.For.condition != NULL) {
            RuntimeValue cond = eval(node->data.For.condition, local);
            if (cond.data.float_value == 0.0) break;
        }

        RuntimeValue res = eval(node->data.For.body, local);
        if (res.type == VAL_RETURN) {
            gc_pop_env();
            return res;
        }

        if (node->data.For.step != NULL) {
            eval(node->data.For.step, local);
        }
    }

    gc_pop_env();
    return make_int(0);
}