#include "functions.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../Eval/eval.h"
#include "../Utils/hash.h"

RuntimeValue eval_func_decl(const ASTNode *node, Environment *env) {
    RuntimeValue func;
    func.type = VAL_FUNC;
    func.data.func.node = (ASTNode*)node;

    func.data.func.closure_env = env;

    env_set(env, node->data.FuncDecl.name, func);

    return func;
}

RuntimeValue eval_func_call(const ASTNode *node, Environment *env) {

    RuntimeValue callable = env_get_fast(env, node->data.FuncCall.name, get_hash(node->data.FuncCall.name));

    if (callable.type == VAL_NATIVE_FUNC) {
        int arg_count = node->data.FuncCall.arg_count;
        RuntimeValue *args = malloc(arg_count * sizeof(RuntimeValue));

        for (int i = 0; i < arg_count; i++) {
            args[i] = eval(node->data.FuncCall.args[i], env);
            if (args[i].type == VAL_RETURN) args[i].type = args[i].original_type;
        }

        RuntimeValue result = callable.data.native_func(args, arg_count);

        free(args);
        return result;
    }

    if (callable.type == VAL_FUNC) {
        ASTNode *decl = callable.data.func.node;


        if (node->data.FuncCall.arg_count != decl->data.FuncDecl.param_count) {
            printf("\n[MKS Runtime Error] Function '%s' expects %d arguments, but got %d\n",
                   node->data.FuncCall.name, decl->data.FuncDecl.param_count, node->data.FuncCall.arg_count);
            exit(1);
        }


        Environment *local_env = env_create_child(callable.data.func.closure_env);


        for (int i = 0; i < decl->data.FuncDecl.param_count; i++) {
            RuntimeValue arg_val = eval(node->data.FuncCall.args[i], env);
            if (arg_val.type == VAL_RETURN) arg_val.type = arg_val.original_type;

            env_set(local_env, decl->data.FuncDecl.params[i], arg_val);
        }


        RuntimeValue result = eval(decl->data.FuncDecl.body, local_env);

        if (result.type == VAL_RETURN) {
            result.type = result.original_type;
        }

        // env_free(local_env);

        return result;
    }

    printf("\n[MKS Runtime Error] '%s' is not a function and cannot be called.\n", node->data.FuncCall.name);
    exit(1);
}