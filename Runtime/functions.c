#include "functions.h"
#include <stdio.h>
#include <stdlib.h>

#include "../Eval/eval.h"
#include "../Utils/hash.h"
#include "../GC/gc.h"
#include "errors.h"
#include "context.h"
#include "../VM/vm.h"

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
    int pushed_roots = 0;
    pushed_roots += gc_push_root_if_needed(&callable);

    RuntimeValue args_stack[32];
    RuntimeValue *args = (arg_count > 32)
        ? (RuntimeValue *)malloc((size_t)arg_count * sizeof(RuntimeValue))
        : args_stack;
    if (args == NULL) {
        while (pushed_roots-- > 0) {
            gc_pop_root();
        }
        runtime_error("Out of memory in function call");
    }

    for (int i = 0; i < arg_count; i++) {
        args[i] = unwrap(eval(node->data.func_call.args[i], env));
        pushed_roots += gc_push_root_if_needed(&args[i]);
    }

    RuntimeValue result = make_null();

    if (callable.type == VAL_NATIVE_FUNC) {
        result = callable.data.native.fn(mks_context_current(), args, arg_count);
    } else if (callable.type == VAL_BLUEPRINT) {
        result = eval_blueprint_construct(callable, args, arg_count, env);
    } else if (callable.type == VAL_FUNC) {
        const ASTNode *decl = callable.data.func.node;
        const int param_count = decl->data.func_decl.param_count;

        if (arg_count != param_count) {
            while (pushed_roots-- > 0) {
                gc_pop_root();
            }
            if (arg_count > 32) {
                free(args);
            }
            runtime_error("Function '%s' expects %d arguments, got %d", name, param_count, arg_count);
        }

        if (vm_has_compiled_function(decl)) {
            result = vm_call_function_value(callable, args, arg_count, NULL);
        } else {
            Environment *local_env = env_create_child(callable.data.func.closure_env);
            gc_push_env(local_env);

            for (int i = 0; i < param_count; i++) {
                env_set(local_env, decl->data.func_decl.params[i], args[i]);
            }

            result = unwrap(eval(decl->data.func_decl.body, local_env));

            gc_pop_env();
        }
    } else {
        while (pushed_roots-- > 0) {
            gc_pop_root();
        }
        if (arg_count > 32) {
            free(args);
        }
        runtime_error("'%s' is not a function.", name);
    }

    while (pushed_roots-- > 0) {
        gc_pop_root();
    }

    if (arg_count > 32) {
        free(args);
    }

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
    env_set(obj_env, "self", self);

    for (int i = 0; i < param_count; i++) {
        env_set(obj_env,
                entity->data.entity.params[i],
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
        if (vm_has_compiled_function(entity->data.entity.init_body)) {
            (void)unwrap(vm_run_compiled_ast_body(obj_env, entity->data.entity.init_body));
        } else {
            (void)unwrap(vm_run_ast(obj_env, entity->data.entity.init_body));
        }
    }

    gc_pop_env();
    return self;
}
