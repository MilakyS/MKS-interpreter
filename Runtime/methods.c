#include "methods.h"
#include "methods_cache.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "../Eval/eval.h"
#include "../GC/gc.h"

#define UNUSED(x) (void)(x)

static RuntimeValue handle_object_method(RuntimeValue target, const char* method, const struct ASTNode* node, struct Environment* env);

static inline RuntimeValue unwrap(RuntimeValue v) {
    if (v.type == VAL_RETURN) v.type = v.original_type;
    return v;
}

static RuntimeValue dispatch(MethodEntry *table, size_t count, unsigned int hash, const char* name,
                           RuntimeValue target, RuntimeValue *args, int arg_count, struct Environment *env) {
    for (size_t i = 0; i < count; i++) {
        if (table[i].hash == hash && strcmp(table[i].name, name) == 0) {
            return table[i].handler(target, args, arg_count, env);
        }
    }
    return make_null();
}

static RuntimeValue m_array_size(RuntimeValue target, RuntimeValue *args, int arg_count, struct Environment *env) {
    UNUSED(args); UNUSED(arg_count); UNUSED(env);
    return make_int(target.data.managed_array->count);
}

static RuntimeValue m_array_inject(RuntimeValue target, RuntimeValue *args, int arg_count, struct Environment *env) {
    UNUSED(env);
    ManagedArray *arr = target.data.managed_array;
    if (arg_count > 0) {
        if (arr->count >= arr->capacity) {
            arr->capacity = (arr->capacity == 0) ? 4 : arr->capacity * 2;
            arr->elements = realloc(arr->elements, arr->capacity * sizeof(RuntimeValue));
        }
        arr->elements[arr->count++] = args[0];
    }
    return target;
}

static RuntimeValue m_array_eject(RuntimeValue target, RuntimeValue *args, int arg_count, struct Environment *env) {
    UNUSED(args); UNUSED(arg_count); UNUSED(env);
    ManagedArray *arr = target.data.managed_array;
    if (arr->count == 0) return make_null();
    return arr->elements[--arr->count];
}

static RuntimeValue m_array_pull(RuntimeValue target, RuntimeValue *args, int arg_count, struct Environment *env) {
    UNUSED(args); UNUSED(arg_count); UNUSED(env);
    ManagedArray *arr = target.data.managed_array;
    if (arr->count == 0) return make_null();
    RuntimeValue first = arr->elements[0];
    for (int i = 0; i < arr->count - 1; i++) arr->elements[i] = arr->elements[i + 1];
    arr->count--;
    return first;
}

static RuntimeValue m_array_exclude(RuntimeValue target, RuntimeValue *args, int arg_count, struct Environment *env) {
    UNUSED(env);
    ManagedArray *arr = target.data.managed_array;
    if (arg_count < 1) return make_null();
    int idx = (int)args[0].data.float_value;
    if (idx >= 0 && idx < arr->count) {
        RuntimeValue removed = arr->elements[idx];
        for (int i = idx; i < arr->count - 1; i++) arr->elements[i] = arr->elements[i + 1];
        arr->count--;
        return removed;
    }
    return make_null();
}

static RuntimeValue m_array_offset(RuntimeValue target, RuntimeValue *args, int arg_count, struct Environment *env) {
    UNUSED(env);
    ManagedArray *arr = target.data.managed_array;
    if (arg_count < 1) return make_null();
    int idx = (int)args[0].data.float_value;
    if (idx >= 0 && idx < arr->count) return arr->elements[idx];
    return make_null();
}

static RuntimeValue m_array_purge(RuntimeValue target, RuntimeValue *args, int arg_count, struct Environment *env) {
    UNUSED(args); UNUSED(arg_count); UNUSED(env);
    target.data.managed_array->count = 0;
    return make_int(1);
}

static MethodEntry array_methods[] = {
    {"size",    M_HASH_SIZE,    m_array_size},
    {"len",     M_HASH_LEN,     m_array_size},
    {"inject",  M_HASH_INJECT,  m_array_inject},
    {"eject",   M_HASH_EJECT,   m_array_eject},
    {"pull",    M_HASH_PULL,    m_array_pull},
    {"exclude", M_HASH_EXCLUDE, m_array_exclude},
    {"offset",  M_HASH_OFFSET,  m_array_offset},
    {"purge",   M_HASH_PURGE,   m_array_purge}
};
#define ARRAY_METHODS_COUNT (sizeof(array_methods) / sizeof(MethodEntry))

static RuntimeValue m_string_upper(RuntimeValue target, RuntimeValue *args, int arg_count, struct Environment *env) {
    UNUSED(args); UNUSED(arg_count); UNUSED(env);
    const char *src = target.data.managed_string->data;
    size_t len = strlen(src);
    char *res_str = (char*)malloc(len + 1);
    for (size_t i = 0; i < len; i++) res_str[i] = (char)toupper(src[i]);
    res_str[len] = '\0';
    RuntimeValue res = make_string(res_str);
    free(res_str);
    return res;
}

static RuntimeValue m_string_lower(RuntimeValue target, RuntimeValue *args, int arg_count, struct Environment *env) {
    UNUSED(args); UNUSED(arg_count); UNUSED(env);
    const char *src = target.data.managed_string->data;
    size_t len = strlen(src);
    char *res_str = (char*)malloc(len + 1);
    for (size_t i = 0; i < len; i++) res_str[i] = (char)tolower(src[i]);
    res_str[len] = '\0';
    RuntimeValue res = make_string(res_str);
    free(res_str);
    return res;
}

static RuntimeValue m_string_len(RuntimeValue target, RuntimeValue *args, int arg_count, struct Environment *env) {
    UNUSED(args); UNUSED(arg_count); UNUSED(env);
    return make_int((int)strlen(target.data.managed_string->data));
}

static RuntimeValue m_string_empty(RuntimeValue target, RuntimeValue *args, int arg_count, struct Environment *env) {
    UNUSED(args); UNUSED(arg_count); UNUSED(env);
    return make_int(strlen(target.data.managed_string->data) == 0);
}

static MethodEntry string_methods[] = {
    {"upper", M_HASH_UPPER, m_string_upper},
    {"lower", M_HASH_LOWER, m_string_lower},
    {"len",   M_HASH_LEN,   m_string_len},
    {"empty", M_HASH_EMPTY, m_string_empty}
};
#define STRING_METHODS_COUNT (sizeof(string_methods) / sizeof(MethodEntry))

RuntimeValue eval_method_call(const ASTNode *node, Environment *env) {
    int gc_snap = gc_save_stack();

    RuntimeValue target = unwrap(eval(node->data.MethodCall.target, env));
    gc_push_root(&target);

    int arg_count = node->data.MethodCall.arg_count;
    if (arg_count > 16) arg_count = 16;
    RuntimeValue args[16];
    for (int i = 0; i < arg_count; i++) {
        args[i] = unwrap(eval(node->data.MethodCall.args[i], env));
        gc_push_root(&args[i]);
    }

    unsigned int hash = node->data.MethodCall.method_hash;
    const char *name = node->data.MethodCall.method_name;
    RuntimeValue result = make_null();

    switch (target.type) {
        case VAL_ARRAY:
            result = dispatch(array_methods, ARRAY_METHODS_COUNT, hash, name, target, args, arg_count, env);
            break;
        case VAL_STRING:
            result = dispatch(string_methods, STRING_METHODS_COUNT, hash, name, target, args, arg_count, env);
            break;
        case VAL_OBJECT:
            result = handle_object_method(target, name, node, env);
            break;
        default:
            fprintf(stderr, "[MKS Runtime Error] Type %d has no methods\n", target.type);
            exit(1);
    }

    gc_restore_stack(gc_snap);
    return result;
}

static RuntimeValue handle_object_method(RuntimeValue target, const char* method, const ASTNode* node, Environment* env) {
    RuntimeValue member = env_get_fast(target.data.obj_env, method, node->data.MethodCall.method_hash);
    if (member.type == VAL_FUNC) {
        ASTNode *decl = member.data.func.node;
        Environment *local_env = env_create_child(target.data.obj_env);
        for (int i = 0; i < decl->data.FuncDecl.param_count; i++) {
            RuntimeValue arg_val = unwrap(eval(node->data.MethodCall.args[i], env));
            env_set(local_env, decl->data.FuncDecl.params[i], arg_val);
        }
        return unwrap(eval(decl->data.FuncDecl.body, local_env));
    }
    return member;
}