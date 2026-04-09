#include "eval.h"
#include <stdio.h>
#include <stdlib.h>

#include "../Runtime/value.h"
#include "../env/env.h"
#include "../GC/gc.h"

#include "../Runtime/output.h"
#include "../Runtime/operators.h"
#include "../Runtime/indexing.h"
#include "../Runtime/methods.h"
#include "../Runtime/functions.h"
#include "../Runtime/control_flow.h"
#include "../Runtime/module.h"
#include "../Runtime/errors.h"
#include "../Runtime/extension.h"
#include "../Runtime/watch.h"
#include "../Runtime/profiler.h"

static inline RuntimeValue unwrap_return(RuntimeValue value) {
    if (value.type == VAL_RETURN) {
        value.type = value.original_type;
    }
    return value;
}

static RuntimeValue eval_entity(const ASTNode *node, Environment *env);
static RuntimeValue eval_extend(const ASTNode *node, Environment *env);
static RuntimeValue eval_swap(const ASTNode *node, Environment *env);
static RuntimeValue eval_test(const ASTNode *node, Environment *env);
static RuntimeValue eval_defer(const ASTNode *node, Environment *env);
static RuntimeValue eval_watch(const ASTNode *node, Environment *env);
static RuntimeValue eval_on_change(const ASTNode *node, Environment *env);
static RuntimeValue eval_break(const ASTNode *node, Environment *env);
static RuntimeValue eval_continue(const ASTNode *node, Environment *env);
static RuntimeValue eval_impl(const ASTNode *node, Environment *env);
static RuntimeValue eval_entity(const ASTNode *node, Environment *env) {
    RuntimeValue bp = make_blueprint(node, env);
    env_set_fast(env, node->data.entity.name, node->data.entity.name_hash, bp);
    return bp;
}

static RuntimeValue eval_extend(const ASTNode *node, Environment *env) {
    register_extension(node, env);
    return make_null();
}

static RuntimeValue eval_defer(const ASTNode *node, Environment *env) {
    (void)node;
    (void)env;
    return make_null();
}

static RuntimeValue eval_watch(const ASTNode *node, Environment *env) {
    (void)env;
    watch_register(node->data.watch_stmt.name, node->data.watch_stmt.hash);
    return make_null();
}

static RuntimeValue eval_on_change(const ASTNode *node, Environment *env) {
    watch_register_handler(node->data.on_change_stmt.name,
                           node->data.on_change_stmt.hash,
                           node->data.on_change_stmt.body,
                           env);
    return make_null();
}

static RuntimeValue eval_break(const ASTNode *node, Environment *env) {
    (void)node; (void)env;
    return make_break();
}

static RuntimeValue eval_continue(const ASTNode *node, Environment *env) {
    (void)node; (void)env;
    return make_continue();
}

typedef enum { LV_VAR, LV_OBJ_FIELD, LV_ARRAY_ELEM } LValueKind;
typedef struct {
    LValueKind kind;
    EnvVar *var_entry;
    const char *name;
    unsigned int hash;
    ManagedArray *arr;
    int index;
    Environment *obj_env;
} LValue;

static void resolve_lvalue(const ASTNode *node, Environment *env, LValue *out) {
    if (node->type == AST_IDENTIFIER) {
        out->kind = LV_VAR;
        out->var_entry = env_get_entry(env, node->data.identifier.name, node->data.identifier.id_hash);
        out->name = node->data.identifier.name;
        out->hash = node->data.identifier.id_hash;
        return;
    }
    if (node->type == AST_OBJ_GET) {
        RuntimeValue obj = eval(node->data.obj_get.object, env);
        if (obj.type != VAL_OBJECT) runtime_error("Swap target is not object");
        out->kind = LV_OBJ_FIELD;
        out->obj_env = obj.data.obj_env;
        out->name = node->data.obj_get.field;
        out->hash = node->data.obj_get.field_hash;
        return;
    }
    if (node->type == AST_INDEX) {
        RuntimeValue target = eval(node->data.index.target, env);
        RuntimeValue idxv = eval(node->data.index.index, env);
        int idx = (int)idxv.data.float_value;
        if (target.type != VAL_ARRAY) runtime_error("Swap target is not array");
        ManagedArray *arr = target.data.managed_array;
        if (idx < 0 || idx >= arr->count) runtime_error("Swap index out of bounds");
        out->kind = LV_ARRAY_ELEM;
        out->arr = arr;
        out->index = idx;
        return;
    }
    runtime_error("Invalid swap target");
}

static RuntimeValue lvalue_get(const LValue *lv) {
    switch (lv->kind) {
        case LV_VAR:
            if (lv->var_entry != NULL) {
                return lv->var_entry->value;
            }
            return env_get_fast(NULL, lv->name, lv->hash);
        case LV_OBJ_FIELD: {
            RuntimeValue v;
            if (!env_try_get(lv->obj_env, lv->name, lv->hash, &v)) return make_null();
            return v;
        }
        case LV_ARRAY_ELEM:
            return lv->arr->elements[lv->index];
    }
    return make_null();
}

static void lvalue_set(const LValue *lv, RuntimeValue v) {
    if (lv->kind == LV_VAR) {
        if (lv->var_entry != NULL) {
            lv->var_entry->value = v;
            watch_trigger(lv->name, lv->hash, NULL);
        } else {
            env_update_fast(NULL, lv->name, lv->hash, v);
        }
        return;
    }
    if (lv->kind == LV_OBJ_FIELD) {
        env_set_fast(lv->obj_env, lv->name, lv->hash, v);
        return;
    }
    if (lv->kind == LV_ARRAY_ELEM) {
        lv->arr->elements[lv->index] = v;
        return;
    }
}

static RuntimeValue eval_swap(const ASTNode *node, Environment *env) {
    LValue a = {0}, b = {0};
    resolve_lvalue(node->data.swap_stmt.left, env, &a);
    resolve_lvalue(node->data.swap_stmt.right, env, &b);

    RuntimeValue va = lvalue_get(&a);
    RuntimeValue vb = lvalue_get(&b);

    lvalue_set(&a, vb);
    lvalue_set(&b, va);

    return make_null();
}

static RuntimeValue eval_test(const ASTNode *node, Environment *env) {
    RuntimeValue res = eval(node->data.test_block.body, env);
    (void)res;
    printf("[PASS] %s\n", node->data.test_block.name);
    return make_null();
}

#define MKS_MAX_EVAL_DEPTH 10000
static int eval_depth = 0;

RuntimeValue eval(const ASTNode *node, Environment *env) {
    eval_depth++;
    if (eval_depth > MKS_MAX_EVAL_DEPTH) {
        runtime_error("Recursion depth limit (%d) exceeded", MKS_MAX_EVAL_DEPTH);
    }
    RuntimeValue out = eval_impl(node, env);
    eval_depth--;
    return out;
}

static RuntimeValue eval_impl(const ASTNode *node, Environment *env) {
    if (node == NULL) {
        return make_int(0);
    }

    runtime_set_line(node->line);
    profiler_on_eval(node->type);

    switch (node->type) {
        case AST_NUMBER:
            return make_int(node->data.number_value);

        case AST_STRING:
            return make_string(node->data.string_value);

        case AST_IDENTIFIER:
            return env_get_fast(env, node->data.identifier.name, node->data.identifier.id_hash);

        case AST_ARRAY: {
            const int count = node->data.array.item_count;
            RuntimeValue arr_val = make_array(count);

            gc_push_root(&arr_val);

            for (int i = 0; i < count; i++) {
                RuntimeValue elem = unwrap_return(eval(node->data.array.items[i], env));
                arr_val.data.managed_array->elements[i] = elem;
            }

            arr_val.data.managed_array->count = count;

            gc_pop_root();
            return arr_val;
        }

        case AST_VAR_DECL: {
            RuntimeValue val = eval(node->data.var_decl.value, env);
            gc_push_root(&val);

            env_set_fast(env, node->data.var_decl.name, node->data.var_decl.id_hash, val);

            gc_pop_root();
            return val;
        }

        case AST_ASSIGN: {
            RuntimeValue val = eval(node->data.assign.value, env);
            gc_push_root(&val);

            env_update_fast(env, node->data.assign.name, node->data.assign.id_hash, val);

            gc_pop_root();
            return val;
        }

        case AST_FUNC_DECL:
            return eval_func_decl(node, env);

        case AST_FUNC_CALL:
            return eval_func_call(node, env);

        case AST_METHOD_CALL:
            return eval_method_call(node, env);

        case AST_RETURN: {
            RuntimeValue val = eval(node->data.return_stmt.value, env);
            val.original_type = val.type;
            val.type = VAL_RETURN;
            return val;
        }

        case AST_BINOP:
            return eval_binop(node, env);

        case AST_OUTPUT:
            return eval_output(node, env);

        case AST_INDEX:
            return eval_index(node, env);

        case AST_INDEX_ASSIGN:
            return eval_index_assign(node, env);

        case AST_OBJ_GET: {
            RuntimeValue obj = eval(node->data.obj_get.object, env);
            gc_push_root(&obj);
            if (obj.type != VAL_OBJECT) {
                gc_pop_root();
                runtime_error("Property access on non-object");
            }
            RuntimeValue val;
            if (!env_try_get(obj.data.obj_env, node->data.obj_get.field, node->data.obj_get.field_hash, &val)) {
                gc_pop_root();
                return make_null();
            }
            gc_pop_root();
            return val;
        }

        case AST_OBJ_SET: {
            RuntimeValue obj = eval(node->data.obj_set.object, env);
            gc_push_root(&obj);
            if (obj.type != VAL_OBJECT) {
                gc_pop_root();
                runtime_error("Property set on non-object");
            }
            RuntimeValue val = eval(node->data.obj_set.value, env);
            env_set_fast(obj.data.obj_env, node->data.obj_set.field, node->data.obj_set.field_hash, val);
            gc_pop_root();
            return val;
        }

        case AST_ENTITY:
            return eval_entity(node, env);

        case AST_EXTEND:
            return eval_extend(node, env);

        case AST_DEFER:
            return eval_defer(node, env);

        case AST_WATCH:
            return eval_watch(node, env);

        case AST_ON_CHANGE:
            return eval_on_change(node, env);

        case AST_BREAK:
            return eval_break(node, env);

        case AST_CONTINUE:
            return eval_continue(node, env);

        case AST_SWAP:
            return eval_swap(node, env);

        case AST_TEST:
            return eval_test(node, env);

        case AST_BLOCK:
            return eval_block(node, env);

        case AST_IF_BLOCK:
            return eval_if(node, env);

        case AST_WHILE:
            return eval_while(node, env);

        case AST_FOR:
            return eval_for(node, env);

        case AST_REPEAT:
            return eval_repeat(node, env);

        case AST_USING: {
            module_eval_file(node->data.using_stmt.path, env);
            return make_null();
        }

        default:
            fprintf(stderr, "[MKS Eval Error] Unknown AST node type: %d\n", node->type);
            exit(1);
    }
}
