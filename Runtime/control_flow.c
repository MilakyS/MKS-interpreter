#include "control_flow.h"
#include "../Eval/eval.h"
#include "../GC/gc.h"

static inline RuntimeValue unwrap(RuntimeValue v) {
    if (v.type == VAL_RETURN) {
        v.type = v.original_type;
    }
    return v;
}

RuntimeValue eval_block(const ASTNode *node, Environment *env) {
    RuntimeValue last = make_int(0);

    for (int i = 0; i < node->data.block.count; i++) {
        gc_check(env);

        last = eval(node->data.block.items[i], env);
        if (last.type == VAL_RETURN) {
            return last;
        }
    }

    return last;
}

RuntimeValue eval_if(const ASTNode *node, Environment *env) {
    RuntimeValue cond = unwrap(eval(node->data.if_block.condition, env));

    if (cond.data.float_value != 0.0) {
        return eval(node->data.if_block.body, env);
    }

    if (node->data.if_block.else_body != NULL) {
        return eval(node->data.if_block.else_body, env);
    }

    return make_int(0);
}

RuntimeValue eval_while(const ASTNode *node, Environment *env) {
    while (unwrap(eval(node->data.while_block.condition, env)).data.float_value != 0.0) {
        gc_check(env);

        RuntimeValue res = eval(node->data.while_block.body, env);
        if (res.type == VAL_RETURN) {
            return res;
        }
    }

    return make_int(0);
}

RuntimeValue eval_for(const ASTNode *node, Environment *env) {
    Environment *local = env_create_child(env);
    gc_push_env(local);

    if (node->data.for_block.init != NULL) {
        eval(node->data.for_block.init, local);
    }

    while (1) {
        gc_check(local);

        if (node->data.for_block.condition != NULL) {
            RuntimeValue cond = unwrap(eval(node->data.for_block.condition, local));
            if (cond.data.float_value == 0.0) {
                break;
            }
        }

        RuntimeValue res = eval(node->data.for_block.body, local);
        if (res.type == VAL_RETURN) {
            gc_pop_env();
            return res;
        }

        if (node->data.for_block.step != NULL) {
            eval(node->data.for_block.step, local);
        }
    }

    gc_pop_env();
    return make_int(0);
}