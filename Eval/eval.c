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
#include "../std/watch.h"
#include "../Runtime/profiler.h"
#include "../Runtime/context.h"
#include "../Utils/hash.h"

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
static RuntimeValue eval_address_of(const ASTNode *node, Environment *env);
static RuntimeValue eval_deref(const ASTNode *node, Environment *env);
static RuntimeValue eval_deref_assign(const ASTNode *node, Environment *env);

static int ast_env_cache_valid(const Environment *env,
                               const Environment *cached_env,
                               size_t cached_env_version) {
    return env != NULL &&
           env == cached_env &&
           cached_env_version == mks_context_current()->env_shape_epoch;
}

static RuntimeValue eval_identifier_cached(const ASTNode *node, Environment *env) {
    if (ast_env_cache_valid(env,
                            node->data.identifier.cached_env,
                            node->data.identifier.cached_env_version) &&
        node->data.identifier.cached_entry != NULL) {
        return node->data.identifier.cached_entry->value;
    }

    EnvVar *entry = env_get_entry(env,
                                  node->data.identifier.name,
                                  node->data.identifier.id_hash);
    if (entry == NULL) {
        runtime_error("Undefined variable '%s'", node->data.identifier.name);
    }

    ASTNode *mutable_node = (ASTNode *)node;
    mutable_node->data.identifier.cached_entry = entry;
    mutable_node->data.identifier.cached_env = env;
    mutable_node->data.identifier.cached_env_version = mks_context_current()->env_shape_epoch;

    return entry->value;
}

static void assign_cached(const ASTNode *node, Environment *env, RuntimeValue value) {
    if (value.type == VAL_RETURN) {
        value.type = value.original_type;
    }

    EnvVar *entry = NULL;
    if (ast_env_cache_valid(env,
                            node->data.assign.cached_env,
                            node->data.assign.cached_env_version)) {
        entry = node->data.assign.cached_entry;
    }

    if (entry == NULL) {
        entry = env_get_entry(env, node->data.assign.name, node->data.assign.id_hash);
        if (entry == NULL) {
            runtime_error("Variable '%s' is not defined!", node->data.assign.name);
        }

        ASTNode *mutable_node = (ASTNode *)node;
        mutable_node->data.assign.cached_entry = entry;
        mutable_node->data.assign.cached_env = env;
        mutable_node->data.assign.cached_env_version = mks_context_current()->env_shape_epoch;
    }

    entry->value = value;
    if (watch_has_any()) {
        watch_trigger(node->data.assign.name, node->data.assign.id_hash, env, &value);
    }
}

static RuntimeValue eval_export(const ASTNode *node, Environment *env) {
    RuntimeValue res = eval(node->data.export_stmt.decl, env);
    const ASTNode *decl = node->data.export_stmt.decl;

    const char *name = NULL; unsigned int hash = 0;
    switch (decl->type) {
        case AST_FUNC_DECL:
            name = decl->data.func_decl.name;
            hash = decl->data.func_decl.name_hash;
            break;
        case AST_VAR_DECL:
            name = decl->data.var_decl.name;
            hash = decl->data.var_decl.id_hash;
            break;
        case AST_ENTITY:
            name = decl->data.entity.name;
            hash = decl->data.entity.name_hash;
            break;
        default:
            runtime_error("Unsupported export node type");
    }

    RuntimeValue exports_val;
    if (!env_try_get(env, "exports", get_hash("exports"), &exports_val)) {
        runtime_error("Internal: exports not found in module env");
    }
    if (exports_val.type != VAL_MODULE) {
        runtime_error("Internal: exports is not module");
    }
    env_set_fast(exports_val.data.obj_env, name, hash, res);
    return res;
}

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
        int idx = (int)runtime_value_as_int(idxv);
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
            if (watch_has_any()) {
                watch_trigger(lv->name, lv->hash, NULL, &v);
            }
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

static RuntimeValue pointer_read(ManagedPointer *ptr) {
    if (ptr == NULL) {
        runtime_error("Cannot dereference null pointer");
    }

    switch (ptr->kind) {
        case PTR_ENV_VAR:
            if (ptr->as.var.entry == NULL) {
                runtime_error("Pointer target variable no longer exists");
            }
            return ptr->as.var.entry->value;

        case PTR_ARRAY_ELEM: {
            ManagedArray *arr = ptr->as.array_elem.array;
            const int index = ptr->as.array_elem.index;
            if (arr == NULL || index < 0 || index >= arr->count) {
                runtime_error("Array pointer index out of bounds");
            }
            return arr->elements[index];
        }

        case PTR_OBJECT_FIELD: {
            RuntimeValue value;
            if (!env_try_get(ptr->as.object_field.env,
                             ptr->as.object_field.field,
                             ptr->as.object_field.hash,
                             &value)) {
                runtime_error("Pointer target field '%s' no longer exists", ptr->as.object_field.field);
            }
            return value;
        }
    }

    runtime_error("Invalid pointer target");
    return make_null();
}

static RuntimeValue pointer_write(ManagedPointer *ptr, RuntimeValue value) {
    if (ptr == NULL) {
        runtime_error("Cannot assign through null pointer");
    }

    if (value.type == VAL_RETURN) {
        value.type = value.original_type;
    }

    switch (ptr->kind) {
        case PTR_ENV_VAR:
            if (ptr->as.var.entry == NULL) {
                runtime_error("Pointer target variable no longer exists");
            }
            ptr->as.var.entry->value = value;
            return value;

        case PTR_ARRAY_ELEM: {
            ManagedArray *arr = ptr->as.array_elem.array;
            const int index = ptr->as.array_elem.index;
            if (arr == NULL || index < 0 || index >= arr->count) {
                runtime_error("Array pointer index out of bounds");
            }
            arr->elements[index] = value;
            return value;
        }

        case PTR_OBJECT_FIELD:
            env_set_fast(ptr->as.object_field.env,
                         ptr->as.object_field.field,
                         ptr->as.object_field.hash,
                         value);
            return value;
    }

    runtime_error("Invalid pointer target");
    return make_null();
}

static RuntimeValue eval_address_of_lvalue(const ASTNode *target, Environment *env) {
    if (target == NULL) {
        runtime_error("Cannot take address of empty expression");
    }

    switch (target->type) {
        case AST_IDENTIFIER: {
            Environment *owner = NULL;
            EnvVar *entry = env_get_entry_with_owner(env,
                                                     target->data.identifier.name,
                                                     target->data.identifier.id_hash,
                                                     &owner);
            if (entry == NULL || owner == NULL) {
                runtime_error("Cannot take address of undefined variable '%s'", target->data.identifier.name);
            }
            return make_pointer_to_var(owner, entry);
        }

        case AST_INDEX: {
            RuntimeValue container = unwrap_return(eval(target->data.index.target, env));
            const int container_rooted = gc_push_root_if_needed(&container);
            if (container.type != VAL_ARRAY) {
                if (container_rooted) gc_pop_root();
                runtime_error("Can only take address of array elements with []");
            }

            RuntimeValue index_value = unwrap_return(eval(target->data.index.index, env));
            const int index = (int)runtime_value_as_int(index_value);
            ManagedArray *arr = container.data.managed_array;
            if (arr == NULL || index < 0 || index >= arr->count) {
                if (container_rooted) gc_pop_root();
                runtime_error("Array pointer index out of bounds");
            }

            RuntimeValue pointer = make_pointer_to_array_elem(arr, index);
            if (container_rooted) gc_pop_root();
            return pointer;
        }

        case AST_OBJ_GET: {
            RuntimeValue object = unwrap_return(eval(target->data.obj_get.object, env));
            const int object_rooted = gc_push_root_if_needed(&object);
            if (object.type != VAL_OBJECT && object.type != VAL_MODULE) {
                if (object_rooted) gc_pop_root();
                runtime_error("Can only take address of object fields");
            }
            if (object.type == VAL_MODULE) {
                if (object_rooted) gc_pop_root();
                runtime_error("Cannot take address of module export '%s'", target->data.obj_get.field);
            }

            Environment *owner = NULL;
            EnvVar *entry = env_get_entry_with_owner(object.data.obj_env,
                                                     target->data.obj_get.field,
                                                     target->data.obj_get.field_hash,
                                                     &owner);
            if (entry == NULL || owner == NULL) {
                if (object_rooted) gc_pop_root();
                runtime_error("Cannot take address of missing field '%s'", target->data.obj_get.field);
            }

            RuntimeValue pointer = make_pointer_to_object_field(owner,
                                                                target->data.obj_get.field,
                                                                target->data.obj_get.field_hash);
            if (object_rooted) gc_pop_root();
            return pointer;
        }

        default:
            runtime_error("Cannot take address of temporary expression");
    }

    return make_null();
}

static RuntimeValue eval_address_of(const ASTNode *node, Environment *env) {
    return eval_address_of_lvalue(node->data.address_of.target, env);
}

static RuntimeValue eval_deref(const ASTNode *node, Environment *env) {
    RuntimeValue pointer = unwrap_return(eval(node->data.deref.target, env));
    if (pointer.type != VAL_POINTER) {
        runtime_error("Cannot dereference non-pointer");
    }
    return pointer_read(pointer.data.managed_pointer);
}

static RuntimeValue eval_deref_assign(const ASTNode *node, Environment *env) {
    RuntimeValue pointer = unwrap_return(eval(node->data.deref_assign.target, env));
    const int pointer_rooted = gc_push_root_if_needed(&pointer);
    if (pointer.type != VAL_POINTER) {
        if (pointer_rooted) gc_pop_root();
        runtime_error("Cannot assign through non-pointer");
    }

    RuntimeValue value = unwrap_return(eval(node->data.deref_assign.value, env));
    const int value_rooted = gc_push_root_if_needed(&value);
    RuntimeValue result = pointer_write(pointer.data.managed_pointer, value);

    if (value_rooted) gc_pop_root();
    if (pointer_rooted) gc_pop_root();
    return result;
}

#define MKS_MAX_EVAL_DEPTH 10000

RuntimeValue eval(const ASTNode *node, Environment *env) {
    MKSContext *ctx = mks_context_current();
    ctx->eval_depth++;
    if (ctx->eval_depth > MKS_MAX_EVAL_DEPTH) {
        runtime_error("Recursion depth limit (%d) exceeded", MKS_MAX_EVAL_DEPTH);
    }
    RuntimeValue out = eval_impl(node, env);
    ctx->eval_depth--;
    return out;
}

static RuntimeValue eval_impl(const ASTNode *node, Environment *env) {
    if (node == NULL) {
        return make_int(0);
    }

    runtime_set_line(node->line);
    PROFILER_ON_EVAL(node->type);

    switch (node->type) {
        case AST_NUMBER:
            return node->data.number.kind == NUMBER_INT
                ? make_int(node->data.number.int_value)
                : make_float(node->data.number.float_value);

        case AST_STRING:
            return make_string(node->data.string_value);

        case AST_NULL:
            return make_null();

        case AST_BOOL:
            return make_bool(node->data.bool_value);

        case AST_IDENTIFIER:
            return eval_identifier_cached(node, env);

        case AST_ARRAY: {
            const int count = node->data.array.item_count;
            RuntimeValue arr_val = make_array(count);

            gc_push_root(&arr_val);

            for (int i = 0; i < count; i++) {
                RuntimeValue elem = unwrap_return(eval(node->data.array.items[i], env));
                arr_val.data.managed_array->elements[i] = elem;
                arr_val.data.managed_array->count = i + 1;
            }

            gc_pop_root();
            return arr_val;
        }

        case AST_VAR_DECL: {
            RuntimeValue val = eval(node->data.var_decl.value, env);
            const int val_rooted = gc_push_root_if_needed(&val);

            env_set_fast(env, node->data.var_decl.name, node->data.var_decl.id_hash, val);

            if (val_rooted) {
                gc_pop_root();
            }
            return val;
        }

        case AST_ASSIGN: {
            RuntimeValue val = eval(node->data.assign.value, env);
            const int val_rooted = gc_push_root_if_needed(&val);

            assign_cached(node, env, val);

            if (val_rooted) {
                gc_pop_root();
            }
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

        case AST_ADDRESS_OF:
            return eval_address_of(node, env);

        case AST_DEREF:
            return eval_deref(node, env);

        case AST_DEREF_ASSIGN:
            return eval_deref_assign(node, env);

        case AST_OBJ_GET: {
            RuntimeValue obj = eval(node->data.obj_get.object, env);
            const int obj_rooted = gc_push_root_if_needed(&obj);
            if (obj.type != VAL_OBJECT && obj.type != VAL_MODULE) {
                if (obj_rooted) {
                    gc_pop_root();
                }
                runtime_error("Property access on non-object");
            }
            RuntimeValue val;
            if (!env_try_get(obj.data.obj_env, node->data.obj_get.field, node->data.obj_get.field_hash, &val)) {
                if (obj_rooted) {
                    gc_pop_root();
                }
                if (obj.type == VAL_MODULE) {
                    runtime_error("Module has no exported symbol '%s'", node->data.obj_get.field);
                }
                return make_null();
            }
            if (obj_rooted) {
                gc_pop_root();
            }
            return val;
        }

        case AST_OBJ_SET: {
            RuntimeValue obj = eval(node->data.obj_set.object, env);
            const int obj_rooted = gc_push_root_if_needed(&obj);
            if (obj.type != VAL_OBJECT && obj.type != VAL_MODULE) {
                if (obj_rooted) {
                    gc_pop_root();
                }
                runtime_error("Property set on non-object");
            }
            if (obj.type == VAL_MODULE) {
                if (obj_rooted) {
                    gc_pop_root();
                }
                runtime_error("Cannot assign to module export '%s'", node->data.obj_set.field);
            }
            RuntimeValue val = eval(node->data.obj_set.value, env);
            env_set_fast(obj.data.obj_env, node->data.obj_set.field, node->data.obj_set.field_hash, val);
            if (obj_rooted) {
                gc_pop_root();
            }
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

        case AST_SWITCH:
            return eval_switch(node, env);

        case AST_EXPORT:
            return eval_export(node, env);

        case AST_USING: {
            module_import(node->data.using_stmt.path,
                          node->data.using_stmt.alias,
                          node->data.using_stmt.is_legacy_path,
                          node->data.using_stmt.star_import,
                          env);
            return make_null();
        }

        default:
            fprintf(stderr, "[MKS Eval Error] Unknown AST node type: %d\n", node->type);
            exit(1);
    }
}
