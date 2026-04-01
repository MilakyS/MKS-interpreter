#include "functions.h"
#include <stdio.h>
#include <stdlib.h>

#include "../Eval/eval.h"
#include "../Utils/hash.h"
#include "../GC/gc.h"

static inline RuntimeValue unwrap(RuntimeValue v) {
    if (v.type == VAL_RETURN) {
        v.type = v.original_type;
    }
    return v;
}

RuntimeValue eval_func_decl(const ASTNode *node, Environment *env) {
    RuntimeValue func;
    func.type = VAL_FUNC;
    func.original_type = VAL_FUNC;
    func.data.func.node = (ASTNode *)node;
    func.data.func.closure_env = env;

    env_set_fast(env, node->data.func_decl.name, get_hash(node->data.func_decl.name), func);
    return func;
}

RuntimeValue eval_func_call(const ASTNode *node, Environment *env) {
    gc_check(env);

    const char *name = node->data.func_call.name;
    const unsigned int hash = node->data.func_call.id_hash;
    const int arg_count = node->data.func_call.arg_count;

    RuntimeValue callable = env_get_fast(env, name, hash);
    gc_push_root(&callable);

    if (callable.type == VAL_NATIVE_FUNC) {
        RuntimeValue args_stack[32];
        RuntimeValue *args = (arg_count > 32)
            ? (RuntimeValue *)malloc((size_t)arg_count * sizeof(RuntimeValue))
            : args_stack;

        if (args == NULL) {
            gc_pop_root();
            fprintf(stderr, "[MKS Runtime Error] Out of memory in function call\n");
            exit(1);
        }

        for (int i = 0; i < arg_count; i++) {
            args[i] = unwrap(eval(node->data.func_call.args[i], env));
            gc_push_root(&args[i]);
        }

        const RuntimeValue result = callable.data.native_func(args, arg_count);

        for (int i = 0; i < arg_count; i++) {
            gc_pop_root();
        }

        if (arg_count > 32) {
            free(args);
        }

        gc_pop_root();
        return result;
    }

    if (callable.type == VAL_FUNC) {
        const ASTNode *decl = callable.data.func.node;
        const int param_count = decl->data.func_decl.param_count;

        if (arg_count != param_count) {
            gc_pop_root();
            fprintf(stderr,
                    "\n[MKS Runtime Error] Function '%s' expects %d arguments, got %d\n",
                    name, param_count, arg_count);
            exit(1);
        }

        RuntimeValue args_stack[16];
        RuntimeValue *args = (arg_count > 16)
            ? (RuntimeValue *)malloc((size_t)arg_count * sizeof(RuntimeValue))
            : args_stack;

        if (args == NULL) {
            gc_pop_root();
            fprintf(stderr, "[MKS Runtime Error] Out of memory in function call args\n");
            exit(1);
        }

        for (int i = 0; i < arg_count; i++) {
            args[i] = unwrap(eval(node->data.func_call.args[i], env));
            gc_push_root(&args[i]);
        }

        Environment *local_env = env_create_child(callable.data.func.closure_env);
        gc_push_env(local_env);

        for (int i = 0; i < param_count; i++) {
            env_set(local_env, decl->data.func_decl.params[i], args[i]);
        }

        RuntimeValue result = unwrap(eval(decl->data.func_decl.body, local_env));

        gc_pop_env();

        for (int i = 0; i < arg_count; i++) {
            gc_pop_root();
        }

        if (arg_count > 16) {
            free(args);
        }

        gc_pop_root();
        return result;
    }

    gc_pop_root();
    fprintf(stderr, "\n[MKS Runtime Error] '%s' is not a function.\n", name);
    exit(1);
}