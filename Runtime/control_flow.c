#include "control_flow.h"
#include "../Eval/eval.h"
#include "../GC/gc.h"
#include "errors.h"
#include <stdlib.h>
#include <string.h>

static inline RuntimeValue unwrap(RuntimeValue v) {
    if (v.type == VAL_RETURN) {
        v.type = v.original_type;
    }
    return v;
}

RuntimeValue eval_block(const ASTNode *node, Environment *env) {
    RuntimeValue last = make_int(0);

    ASTNode *defer_inline[8];
    ASTNode **defer_stack = defer_inline;
    int defer_count = 0, defer_cap = 8;

    for (int i = 0; i < node->data.block.count; i++) {
        gc_check(env);

        last = eval(node->data.block.items[i], env);

        if (node->data.block.items[i]->type == AST_DEFER) {
            if (defer_count >= defer_cap) {
                int new_cap = defer_cap == 0 ? 4 : defer_cap * 2;
                ASTNode **new_stack = (defer_stack == defer_inline)
                    ? (ASTNode **)malloc(sizeof(ASTNode *) * (size_t)new_cap)
                    : (ASTNode **)realloc(defer_stack, sizeof(ASTNode *) * (size_t)new_cap);
                if (new_stack == NULL) {
                    runtime_error("Out of memory growing defer stack");
                }
                if (defer_stack == defer_inline) {
                    memcpy(new_stack, defer_inline, sizeof(ASTNode *) * (size_t)defer_count);
                }
                defer_stack = new_stack;
                defer_cap = new_cap;
            }
            defer_stack[defer_count++] = node->data.block.items[i]->data.defer_stmt.body;
        }
        if (last.type == VAL_RETURN || last.type == VAL_BREAK || last.type == VAL_CONTINUE) {
            for (int d = defer_count - 1; d >= 0; d--) {
                eval(defer_stack[d], env);
            }
            if (defer_stack != defer_inline) free(defer_stack);
            return last;
        }
    }

    for (int d = defer_count - 1; d >= 0; d--) {
        eval(defer_stack[d], env);
    }
    if (defer_stack != defer_inline) free(defer_stack);
    return last;
}

RuntimeValue eval_if(const ASTNode *node, Environment *env) {
    RuntimeValue cond = unwrap(eval(node->data.if_block.condition, env));

    if (cond.type != VAL_INT) {
        runtime_error("Unsupported condition type in if statement");
    }

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
        if (res.type == VAL_RETURN) return res;
        if (res.type == VAL_BREAK) return make_int(0);
        if (res.type == VAL_CONTINUE) continue;
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
        if (res.type == VAL_RETURN) { gc_pop_env(); return res; }
        if (res.type == VAL_BREAK) { gc_pop_env(); return make_int(0); }
        if (res.type == VAL_CONTINUE) { if (node->data.for_block.step != NULL) eval(node->data.for_block.step, local); continue; }

        if (node->data.for_block.step != NULL) eval(node->data.for_block.step, local);
    }

    gc_pop_env();
    return make_int(0);
}

RuntimeValue eval_repeat(const ASTNode *node, Environment *env) {
    RuntimeValue countv = unwrap(eval(node->data.repeat_stmt.count_expr, env));
    if (countv.type != VAL_INT) runtime_error("repeat expects number");
    int count = (int)countv.data.float_value;
    if (count < 0) count = 0;

    Environment *loop_env = env;
    if (node->data.repeat_stmt.has_iterator) {
        env_set_fast(loop_env, node->data.repeat_stmt.iter_name, node->data.repeat_stmt.iter_hash, make_int(0));
    }

    for (int i = 0; i < count; i++) {
        if (node->data.repeat_stmt.has_iterator) {
            env_update_fast(loop_env, node->data.repeat_stmt.iter_name, node->data.repeat_stmt.iter_hash, make_int(i));
        }

        RuntimeValue res = eval(node->data.repeat_stmt.body, loop_env);
        if (res.type == VAL_RETURN) return res;
        if (res.type == VAL_BREAK) return make_int(0);
        if (res.type == VAL_CONTINUE) continue;
    }

    return make_int(0);
}
