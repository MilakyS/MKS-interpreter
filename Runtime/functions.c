#include "functions.h"
#include <stdio.h>
#include <stdlib.h>
#include "../Eval/eval.h"
#include "../GC/gc.h"

RuntimeValue eval_func_decl(const ASTNode *node, Environment *env) {
    RuntimeValue func;
    func.type = VAL_FUNC;
    func.original_type = VAL_FUNC;
    func.data.func.node = (ASTNode*)node;
    func.data.func.closure_env = env;

    env_set(env, node->data.FuncDecl.name, func);
    return func;
}

RuntimeValue eval_func_call(const ASTNode *node, Environment *env) {
    if (mks_gc.allocated_bytes > mks_gc.threshold) {
        gc_collect(env, env);
    }

    RuntimeValue callable = env_get_fast(env, node->data.FuncCall.name, node->data.FuncCall.id_hash);
    gc_push_root(&callable);

    if (callable.type == VAL_NATIVE_FUNC) {
        const int arg_count = node->data.FuncCall.arg_count;
        RuntimeValue args_stack[32];
        RuntimeValue *args = (arg_count > 32) ? malloc(arg_count * sizeof(RuntimeValue)) : args_stack;

        for (int i = 0; i < arg_count; i++) {
            args[i] = eval(node->data.FuncCall.args[i], env);
            if (args[i].type == VAL_RETURN) args[i].type = args[i].original_type;
            gc_push_root(&args[i]);
        }

        const RuntimeValue result = callable.data.native_func(args, arg_count);

        for (int i = 0; i < arg_count; i++) gc_pop_root();
        if (arg_count > 32) free(args);

        gc_pop_root();
        return result;
    }

    if (callable.type == VAL_FUNC) {
        const ASTNode *decl = callable.data.func.node;
        if (node->data.FuncCall.arg_count != decl->data.FuncDecl.param_count) {
            printf("\n[MKS Runtime Error] Function '%s' expects %d arguments\n", node->data.FuncCall.name, decl->data.FuncDecl.param_count);
            exit(1);
        }

        Environment *local_env = env_create_child(callable.data.func.closure_env);

        gc_push_env(local_env);

        for (int i = 0; i < decl->data.FuncDecl.param_count; i++) {
            RuntimeValue arg_val = eval(node->data.FuncCall.args[i], env);
            if (arg_val.type == VAL_RETURN) arg_val.type = arg_val.original_type;
            env_set(local_env, decl->data.FuncDecl.params[i], arg_val);
        }

        RuntimeValue result = eval(decl->data.FuncDecl.body, local_env);
        if (result.type == VAL_RETURN) result.type = result.original_type;

        gc_pop_env();

        gc_pop_root();
        return result;
    }

    printf("\n[MKS Runtime Error] '%s' is not a function.\n", node->data.FuncCall.name);
    exit(1);
}