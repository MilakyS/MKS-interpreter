#include "methods.h"
#include "methods_cache.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <ctype.h>

#include "../Eval/eval.h"
#include "../Utils/hash.h"
#include "../GC/gc.h"
#include "errors.h"
#include "extension.h"

#define UNUSED(x) (void)(x)

static RuntimeValue handle_object_method(
    RuntimeValue target,
    const ASTNode *node,
    RuntimeValue *args,
    int arg_count,
    Environment *env
);

static inline RuntimeValue unwrap(RuntimeValue v) {
    if (v.type == VAL_RETURN) {
        v.type = v.original_type;
    }
    return v;
}

static RuntimeValue dispatch(
    MethodEntry *table,
    size_t count,
    unsigned int hash,
    const char *name,
    RuntimeValue target,
    RuntimeValue *args,
    int arg_count,
    Environment *env
) {
    for (size_t i = 0; i < count; i++) {
        if (table[i].hash == hash && strcmp(table[i].name, name) == 0) {
            return table[i].handler(target, args, arg_count, env);
        }
    }

    return make_null();
}

static RuntimeValue m_array_size(RuntimeValue target, RuntimeValue *args, int arg_count, Environment *env) {
    UNUSED(args);
    UNUSED(arg_count);
    UNUSED(env);
    return make_int(target.data.managed_array->count);
}

static RuntimeValue m_array_inject(RuntimeValue target, RuntimeValue *args, int arg_count, Environment *env) {
    UNUSED(env);

    ManagedArray *arr = target.data.managed_array;
    if (arg_count <= 0) {
        return target;
    }

    if (arr->count >= arr->capacity) {
        int new_capacity = (arr->capacity == 0) ? 4 : arr->capacity * 2;
        RuntimeValue *new_elements = (RuntimeValue *)realloc(
            arr->elements,
            (size_t)new_capacity * sizeof(RuntimeValue)
        );

        if (new_elements == NULL) {
            fprintf(stderr, "[MKS Runtime Error] Out of memory in array.inject()\n");
            exit(1);
        }

        arr->elements = new_elements;
        arr->capacity = new_capacity;
    }

    arr->elements[arr->count++] = args[0];
    return target;
}

static void arr_push(ManagedArray *arr, RuntimeValue v) {
    if (arr->count >= arr->capacity) {
        int new_capacity = (arr->capacity == 0) ? 4 : arr->capacity * 2;
        RuntimeValue *new_elements = (RuntimeValue *)realloc(
            arr->elements,
            (size_t)new_capacity * sizeof(RuntimeValue)
        );
        if (new_elements == NULL) {
            fprintf(stderr, "[MKS Runtime Error] Out of memory in array push\n");
            exit(1);
        }
        arr->elements = new_elements;
        arr->capacity = new_capacity;
    }
    arr->elements[arr->count++] = v;
}

static RuntimeValue m_array_eject(RuntimeValue target, RuntimeValue *args, int arg_count, Environment *env) {
    UNUSED(args);
    UNUSED(arg_count);
    UNUSED(env);

    ManagedArray *arr = target.data.managed_array;
    if (arr->count == 0) {
        return make_null();
    }

    return arr->elements[--arr->count];
}

static RuntimeValue m_array_pull(RuntimeValue target, RuntimeValue *args, int arg_count, Environment *env) {
    UNUSED(args);
    UNUSED(arg_count);
    UNUSED(env);

    ManagedArray *arr = target.data.managed_array;
    if (arr->count == 0) {
        return make_null();
    }

    RuntimeValue first = arr->elements[0];
    for (int i = 0; i < arr->count - 1; i++) {
        arr->elements[i] = arr->elements[i + 1];
    }

    arr->count--;
    return first;
}

static RuntimeValue m_array_exclude(RuntimeValue target, RuntimeValue *args, int arg_count, Environment *env) {
    UNUSED(env);

    ManagedArray *arr = target.data.managed_array;
    if (arg_count < 1) {
        return make_null();
    }

    int idx = (int)args[0].data.float_value;
    if (idx < 0 || idx >= arr->count) {
        return make_null();
    }

    RuntimeValue removed = arr->elements[idx];
    for (int i = idx; i < arr->count - 1; i++) {
        arr->elements[i] = arr->elements[i + 1];
    }

    arr->count--;
    return removed;
}

static RuntimeValue m_array_offset(RuntimeValue target, RuntimeValue *args, int arg_count, Environment *env) {
    UNUSED(env);

    ManagedArray *arr = target.data.managed_array;
    if (arg_count < 1) {
        return make_null();
    }

    int idx = (int)args[0].data.float_value;
    if (idx >= 0 && idx < arr->count) {
        return arr->elements[idx];
    }

    return make_null();
}

static RuntimeValue m_array_purge(RuntimeValue target, RuntimeValue *args, int arg_count, Environment *env) {
    UNUSED(args);
    UNUSED(arg_count);
    UNUSED(env);

    target.data.managed_array->count = 0;
    return make_int(1);
}

static RuntimeValue m_array_join(RuntimeValue target, RuntimeValue *args, int arg_count, Environment *env) {
    UNUSED(env);
    if (arg_count != 1 || args[0].type != VAL_STRING) {
        runtime_error("array.join expects 1 string delimiter");
    }
    ManagedArray *arr = target.data.managed_array;
    const char *delim = args[0].data.managed_string->data;
    size_t delim_len = args[0].data.managed_string->len;

    size_t total = 0;
    for (int i = 0; i < arr->count; i++) {
        if (arr->elements[i].type != VAL_STRING) runtime_error("array.join: all elements must be strings");
        total += arr->elements[i].data.managed_string->len;
        if (i + 1 < arr->count) total += delim_len;
    }
    char *buf = malloc(total + 1);
    if (!buf) runtime_error("Out of memory in array.join");
    char *out = buf;
    for (int i = 0; i < arr->count; i++) {
        ManagedString *ms = arr->elements[i].data.managed_string;
        memcpy(out, ms->data, ms->len); out += ms->len;
        if (i + 1 < arr->count && delim_len > 0) {
            memcpy(out, delim, delim_len); out += delim_len;
        }
    }
    *out = '\0';
    RuntimeValue s = make_string_owned(buf, total);
    return s;
}

static MethodEntry array_methods[] = {
    {"size",    M_HASH_SIZE,    m_array_size},
    {"len",     M_HASH_LEN,     m_array_size},
    {"inject",  M_HASH_INJECT,  m_array_inject},
    {"eject",   M_HASH_EJECT,   m_array_eject},
    {"pull",    M_HASH_PULL,    m_array_pull},
    {"exclude", M_HASH_EXCLUDE, m_array_exclude},
    {"offset",  M_HASH_OFFSET,  m_array_offset},
    {"purge",   M_HASH_PURGE,   m_array_purge},
    {"join",    M_HASH_JOIN,    m_array_join}
};

#define ARRAY_METHODS_COUNT (sizeof(array_methods) / sizeof(array_methods[0]))

static RuntimeValue m_string_upper(RuntimeValue target, RuntimeValue *args, int arg_count, Environment *env) {
    UNUSED(args);
    UNUSED(arg_count);
    UNUSED(env);

    const char *src = target.data.managed_string->data;
    const size_t len = target.data.managed_string->len;

    char *res_str = (char *)malloc(len + 1);
    if (res_str == NULL) {
        fprintf(stderr, "[MKS Runtime Error] Out of memory in string.upper()\n");
        exit(1);
    }

    for (size_t i = 0; i < len; i++) {
        res_str[i] = (char)toupper((unsigned char)src[i]);
    }
    res_str[len] = '\0';

    return make_string_owned(res_str, len);
}

static RuntimeValue m_string_lower(RuntimeValue target, RuntimeValue *args, int arg_count, Environment *env) {
    UNUSED(args);
    UNUSED(arg_count);
    UNUSED(env);

    const char *src = target.data.managed_string->data;
    const size_t len = target.data.managed_string->len;

    char *res_str = (char *)malloc(len + 1);
    if (res_str == NULL) {
        fprintf(stderr, "[MKS Runtime Error] Out of memory in string.lower()\n");
        exit(1);
    }

    for (size_t i = 0; i < len; i++) {
        res_str[i] = (char)tolower((unsigned char)src[i]);
    }
    res_str[len] = '\0';

    return make_string_owned(res_str, len);
}

static RuntimeValue m_string_len(RuntimeValue target, RuntimeValue *args, int arg_count, Environment *env) {
    UNUSED(args);
    UNUSED(arg_count);
    UNUSED(env);

    return make_int((double)target.data.managed_string->len);
}

static RuntimeValue m_string_contains(RuntimeValue target, RuntimeValue *args, int arg_count, Environment *env) {
    UNUSED(env);
    if (arg_count != 1 || args[0].type != VAL_STRING) {
        runtime_error("contains expects 1 string argument");
    }
    if (strstr(target.data.managed_string->data, args[0].data.managed_string->data) != NULL) {
        return make_int(1);
    }
    return make_int(0);
}

static RuntimeValue m_string_starts(RuntimeValue target, RuntimeValue *args, int arg_count, Environment *env) {
    UNUSED(env);
    if (arg_count != 1 || args[0].type != VAL_STRING) runtime_error("starts_with expects 1 string");
    const char *s = target.data.managed_string->data;
    const char *p = args[0].data.managed_string->data;
    size_t lp = args[0].data.managed_string->len;
    if (target.data.managed_string->len < lp) return make_int(0);
    return make_int(strncmp(s, p, lp) == 0 ? 1 : 0);
}

static RuntimeValue m_string_ends(RuntimeValue target, RuntimeValue *args, int arg_count, Environment *env) {
    UNUSED(env);
    if (arg_count != 1 || args[0].type != VAL_STRING) runtime_error("ends_with expects 1 string");
    const char *s = target.data.managed_string->data;
    const char *p = args[0].data.managed_string->data;
    size_t ls = target.data.managed_string->len;
    size_t lp = args[0].data.managed_string->len;
    if (ls < lp) return make_int(0);
    return make_int(strncmp(s + ls - lp, p, lp) == 0 ? 1 : 0);
}

static RuntimeValue m_string_trim(RuntimeValue target, RuntimeValue *args, int arg_count, Environment *env) {
    UNUSED(args); UNUSED(arg_count); UNUSED(env);
    const char *s = target.data.managed_string->data;
    size_t len = target.data.managed_string->len;
    size_t start = 0, end = len;
    while (start < len && isspace((unsigned char)s[start])) start++;
    while (end > start && isspace((unsigned char)s[end-1])) end--;
    size_t outlen = end - start;
    char *buf = malloc(outlen + 1);
    if (!buf) runtime_error("Out of memory in trim");
    memcpy(buf, s + start, outlen);
    buf[outlen] = '\0';
    return make_string_owned(buf, outlen);
}

static RuntimeValue m_string_replace(RuntimeValue target, RuntimeValue *args, int arg_count, Environment *env) {
    UNUSED(env);
    if (arg_count != 2 || args[0].type != VAL_STRING || args[1].type != VAL_STRING) {
        runtime_error("replace expects (old, new) strings");
    }
    const char *src = target.data.managed_string->data;
    const char *old = args[0].data.managed_string->data;
    const char *nw  = args[1].data.managed_string->data;
    size_t len_old = args[0].data.managed_string->len;
    size_t len_new = args[1].data.managed_string->len;
    if (len_old == 0) return target;


    size_t count = 0;
    const char *p = src;
    while ((p = strstr(p, old)) != NULL) { count++; p += len_old; }
    if (count == 0) return target;

    size_t src_len = target.data.managed_string->len;
    size_t out_len = src_len + count * (len_new - len_old);
    char *buf = malloc(out_len + 1);
    if (!buf) runtime_error("Out of memory in replace");

    const char *cur = src;
    char *out = buf;
    while ((p = strstr(cur, old)) != NULL) {
        size_t chunk = (size_t)(p - cur);
        memcpy(out, cur, chunk); out += chunk;
        memcpy(out, nw, len_new); out += len_new;
        cur = p + len_old;
    }
    size_t tail = strlen(cur);
    memcpy(out, cur, tail);
    out[tail] = '\0';
    return make_string_owned(buf, out_len);
}

static RuntimeValue m_string_split(RuntimeValue target, RuntimeValue *args, int arg_count, Environment *env) {
    UNUSED(env);
    if (arg_count != 1 || args[0].type != VAL_STRING) runtime_error("split expects 1 string delimiter");
    const char *src = target.data.managed_string->data;
    const char *delim = args[0].data.managed_string->data;
    size_t len_delim = args[0].data.managed_string->len;
    if (len_delim == 0) runtime_error("split delimiter cannot be empty");

    ManagedArray *arr = make_array(4).data.managed_array;

    const char *start = src;
    const char *pos;
    while ((pos = strstr(start, delim)) != NULL) {
        size_t len = (size_t)(pos - start);
        char *buf = malloc(len + 1);
        if (!buf) runtime_error("Out of memory in split");
        memcpy(buf, start, len); buf[len] = '\0';
        RuntimeValue s = make_string_owned(buf, len);
        arr_push(arr, s);
        start = pos + len_delim;
    }
    size_t len = strlen(start);
    char *buf = malloc(len + 1);
    if (!buf) runtime_error("Out of memory in split tail");
    memcpy(buf, start, len); buf[len] = '\0';
    RuntimeValue s = make_string_owned(buf, len);
    arr_push(arr, s);

    RuntimeValue out;
    out.type = VAL_ARRAY;
    out.original_type = VAL_ARRAY;
    out.data.managed_array = arr;
    return out;
}

static RuntimeValue m_string_empty(RuntimeValue target, RuntimeValue *args, int arg_count, Environment *env) {
    UNUSED(args);
    UNUSED(arg_count);
    UNUSED(env);

    return make_int(target.data.managed_string->len == 0);
}

static MethodEntry string_methods[] = {
    {"upper", M_HASH_UPPER, m_string_upper},
    {"lower", M_HASH_LOWER, m_string_lower},
    {"len",   M_HASH_LEN,   m_string_len},
    {"contains", M_HASH_CONTAINS, m_string_contains},
    {"starts_with", M_HASH_STARTS_WITH, m_string_starts},
    {"ends_with", M_HASH_ENDS_WITH, m_string_ends},
    {"trim",  M_HASH_TRIM,  m_string_trim},
    {"replace", M_HASH_REPLACE, m_string_replace},
    {"split", M_HASH_SPLIT, m_string_split},
    {"empty", M_HASH_EMPTY, m_string_empty}
};

#define STRING_METHODS_COUNT (sizeof(string_methods) / sizeof(string_methods[0]))

RuntimeValue eval_method_call(const ASTNode *node, Environment *env) {
    int gc_snap = gc_save_stack();

    RuntimeValue target = unwrap(eval(node->data.method_call.target, env));
    gc_push_root(&target);

    int arg_count = node->data.method_call.arg_count;
    if (arg_count > 16) {
        arg_count = 16;
    }

    RuntimeValue args[16];
    for (int i = 0; i < arg_count; i++) {
        args[i] = unwrap(eval(node->data.method_call.args[i], env));
        gc_push_root(&args[i]);
    }

    unsigned int hash = node->data.method_call.method_hash;
    const char *name = node->data.method_call.method_name;
    RuntimeValue result = make_null();

    switch (target.type) {
        case VAL_ARRAY:
            result = dispatch(array_methods, ARRAY_METHODS_COUNT, hash, name, target, args, arg_count, env);
            break;

        case VAL_STRING:
            result = dispatch(string_methods, STRING_METHODS_COUNT, hash, name, target, args, arg_count, env);
            break;

        case VAL_OBJECT:
            result = handle_object_method(target, node, args, arg_count, env);
            break;

        default:
            result = make_null();
            break;
    }

    if (result.type == VAL_NULL) {
        RuntimeValue ext = dispatch_extension(target.type, hash, name, target, args, arg_count, env);
        if (ext.type != VAL_NULL) {
            result = ext;
        }
    }

    if (result.type == VAL_NULL) {
        runtime_error("Method '%s' not found", name);
    }

    gc_restore_stack(gc_snap);
    return result;
}

static RuntimeValue handle_object_method(
    RuntimeValue target,
    const ASTNode *node,
    RuntimeValue *args,
    int arg_count,
    Environment *env
) {
    UNUSED(env);

    RuntimeValue member;
    if (!env_try_get(target.data.obj_env,
                     node->data.method_call.method_name,
                     node->data.method_call.method_hash,
                     &member)) {
        return make_null();
    }

    if (member.type == VAL_FUNC) {
        ASTNode *decl = member.data.func.node;
        Environment *local_env = env_create_child(target.data.obj_env);
        gc_push_env(local_env);

        static unsigned int self_hash = 0;
        if (self_hash == 0) self_hash = get_hash("self");
        env_set_fast(local_env, "self", self_hash, target);

        const int param_count = decl->data.func_decl.param_count;
        if (arg_count != param_count) {
            gc_pop_env();
            fprintf(stderr,
                    "[MKS Runtime Error] Method '%s' expects %d arguments, got %d\n",
                    node->data.method_call.method_name,
                    param_count,
                    arg_count);
            exit(1);
        }

        for (int i = 0; i < param_count; i++) {
            env_set_fast(local_env, decl->data.func_decl.params[i], decl->data.func_decl.param_hashes[i], args[i]);
        }

        RuntimeValue result = unwrap(eval(decl->data.func_decl.body, local_env));
        gc_pop_env();
        return result;
    }

    return member;
}
