#include "functions.h"
#include <stdio.h>
#include <stdlib.h>

#include "../Eval/eval.h"
#include "../Utils/hash.h"
#include "../GC/gc.h"
#include "errors.h"

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

    env_set_fast(env, node->data.func_decl.name, node->data.func_decl.name_hash, func);
    return func;
}

RuntimeValue eval_func_call(const ASTNode *node, Environment *env) {
    gc_check(env);

    const char *name = node->data.func_call.name;
    const unsigned int hash = node->data.func_call.id_hash;
    const int arg_count = node->data.func_call.arg_count;

    RuntimeValue callable = env_get_fast(env, name, hash);
    gc_push_root(&callable);

    RuntimeValue args_stack[32];
    RuntimeValue *args = (arg_count > 32)
        ? (RuntimeValue *)malloc((size_t)arg_count * sizeof(RuntimeValue))
        : args_stack;
    if (args == NULL) {
        gc_pop_root();
        runtime_error("Out of memory in function call");
    }

    for (int i = 0; i < arg_count; i++) {
        args[i] = unwrap(eval(node->data.func_call.args[i], env));
        gc_push_root(&args[i]);
    }

    RuntimeValue result = make_null();

    if (callable.type == VAL_NATIVE_FUNC) {
        result = callable.data.native.fn(args, arg_count);
    } else if (callable.type == VAL_BLUEPRINT) {
        result = eval_blueprint_construct(callable, args, arg_count, env);
    } else if (callable.type == VAL_FUNC) {
        const ASTNode *decl = callable.data.func.node;
        const int param_count = decl->data.func_decl.param_count;

        if (arg_count != param_count) {
            gc_pop_root();
            runtime_error("Function '%s' expects %d arguments, got %d", name, param_count, arg_count);
        }

        Environment *local_env = env_create_child(callable.data.func.closure_env);
        gc_push_env(local_env);

        /* BOLT: Use pre-computed hashes for parameters to avoid redundant get_hash calls in recursion */
        for (int i = 0; i < param_count; i++) {
            env_set_fast(local_env, decl->data.func_decl.params[i], decl->data.func_decl.param_hashes[i], args[i]);
        }

        result = unwrap(eval(decl->data.func_decl.body, local_env));

        gc_pop_env();
    } else {
        gc_pop_root();
        runtime_error("'%s' is not a function.", name);
    }

    for (int i = 0; i < arg_count; i++) {
        gc_pop_root();
    }

    if (arg_count > 32) {
        free(args);
    }

    gc_pop_root();
    return result;
}

RuntimeValue eval_blueprint_construct(RuntimeValue blueprint, RuntimeValue *args, int arg_count, Environment *call_env) {
    (void)call_env;
    const ASTNode *entity = blueprint.data.blueprint.entity_node;
    const int param_count = entity->data.entity.param_count;

    if (arg_count != param_count) {
        runtime_error("Entity '%s' expects %d arguments, got %d",
                      entity->data.entity.name,
                      param_count,
                      arg_count);
    }

    Environment *obj_env = env_create_child(blueprint.data.blueprint.closure_env);
    gc_push_env(obj_env);

    RuntimeValue self = make_object(obj_env);
    /* BOLT: Cached hash for 'self' to avoid re-hashing on every object instantiation. */
    static unsigned int self_hash = 0;
    if (self_hash == 0) self_hash = get_hash("self");
    env_set_fast(obj_env, "self", self_hash, self);

    /* BOLT: Use pre-computed hashes for entity parameters. */
    for (int i = 0; i < param_count; i++) {
        env_set_fast(obj_env,
                entity->data.entity.params[i],
                entity->data.entity.param_hashes[i],
                args[i]);
    }

    for (int i = 0; i < entity->data.entity.method_count; i++) {
        ASTNode *m = entity->data.entity.methods[i];
        RuntimeValue fn;
        fn.type = VAL_FUNC;
        fn.original_type = VAL_FUNC;
        fn.data.func.node = m;
        fn.data.func.closure_env = obj_env;
        env_set_fast(obj_env, m->data.func_decl.name, m->data.func_decl.name_hash, fn);
    }

    if (entity->data.entity.init_body != NULL) {
        unwrap(eval(entity->data.entity.init_body, obj_env));
    }

    gc_pop_env();
    return self;
}
