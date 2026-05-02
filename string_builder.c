#include "mks_module.h"
#include "env/env.h"
#include "Utils/hash.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>

/* Internal builder structure. Stored in a hidden field in the builder object. */
typedef struct {
    char *data;
    size_t len;
    size_t cap;
} MksStringBuilder;

#define BUILDER_PTR_FIELD "__ptr"

/* Helper: grow capacity exponentially */
static void sb_grow(MksStringBuilder *sb, size_t needed) {
    size_t required = sb->len + needed;
    if (required <= sb->cap) {
        return;  /* already enough space */
    }

    size_t new_cap = (sb->cap > 0) ? sb->cap : 64;
    while (new_cap < required) {
        new_cap *= 2;
    }

    char *new_data = (char *)realloc(sb->data, new_cap);
    if (!new_data) {
        runtime_error("StringBuilder: out of memory");
    }
    sb->data = new_data;
    sb->cap = new_cap;
}

/* Helper: append raw C string */
static void sb_append_cstr(MksStringBuilder *sb, const char *str) {
    if (!str) return;
    size_t slen = strlen(str);
    sb_grow(sb, slen);
    memcpy(sb->data + sb->len, str, slen);
    sb->len += slen;
    sb->data[sb->len] = '\0';
}

/* Helper: convert RuntimeValue to string representation */
static char *value_to_string(RuntimeValue v) {
    if (v.type == VAL_NULL) {
        return strdup("null");
    } else if (v.type == VAL_BOOL) {
        return strdup(v.data.bool_value ? "true" : "false");
    } else if (v.type == VAL_INT) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%lld", v.data.int_value);
        return strdup(buf);
    } else if (v.type == VAL_FLOAT) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%g", v.data.float_value);
        return strdup(buf);
    } else if (v.type == VAL_STRING) {
        return strdup(v.data.managed_string->data);
    } else {
        return strdup("[object]");
    }
}

/* Helper: get builder pointer from object */
static MksStringBuilder *get_builder_ptr(RuntimeValue obj) {
    if (obj.type != VAL_OBJECT) {
        runtime_error("builder: expected an object");
    }
    RuntimeValue ptr_val = env_get_fast(obj.data.obj_env, BUILDER_PTR_FIELD, get_hash(BUILDER_PTR_FIELD));
    if (ptr_val.type != VAL_INT) {
        runtime_error("builder: corrupted builder object");
    }
    return (MksStringBuilder *)(uintptr_t)ptr_val.data.int_value;
}

/* native: builder() -> creates new empty builder */
static RuntimeValue native_builder(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    (void)args;
    (void)arg_count;

    MksStringBuilder *sb = (MksStringBuilder *)malloc(sizeof(MksStringBuilder));
    if (!sb) {
        runtime_error("StringBuilder: out of memory");
    }
    sb->data = (char *)malloc(64);
    if (!sb->data) {
        free(sb);
        runtime_error("StringBuilder: out of memory");
    }
    sb->data[0] = '\0';
    sb->len = 0;
    sb->cap = 64;

    Environment *builder_env = env_create_child(NULL);
    if (!builder_env) {
        free(sb->data);
        free(sb);
        runtime_error("StringBuilder: out of memory");
    }

    /* Store C pointer as int in hidden field */
    RuntimeValue ptr_val = make_int((int64_t)(uintptr_t)sb);
    env_set_fast(builder_env, BUILDER_PTR_FIELD, get_hash(BUILDER_PTR_FIELD), ptr_val);

    return make_object(builder_env);
}

/* native: append(builder, value) -> builder */
static RuntimeValue native_append(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    if (arg_count < 2) {
        runtime_error("append expects 2 arguments");
    }

    MksStringBuilder *sb = get_builder_ptr(args[0]);

    char *str = value_to_string(args[1]);
    if (!str) {
        runtime_error("StringBuilder: could not convert value to string");
    }

    sb_append_cstr(sb, str);
    free(str);

    return args[0];  /* return builder for chaining */
}

/* native: append_raw(builder, str) -> builder */
static RuntimeValue native_append_raw(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    if (arg_count < 2) {
        runtime_error("append_raw expects 2 arguments");
    }

    if (args[1].type != VAL_STRING) {
        runtime_error("append_raw: second argument must be a string");
    }

    MksStringBuilder *sb = get_builder_ptr(args[0]);
    const char *str = args[1].data.managed_string->data;

    sb_append_cstr(sb, str);

    return args[0];  /* return builder for chaining */
}

/* native: append_line(builder, value) -> builder */
static RuntimeValue native_append_line(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    if (arg_count < 2) {
        runtime_error("append_line expects 2 arguments");
    }

    MksStringBuilder *sb = get_builder_ptr(args[0]);

    char *str = value_to_string(args[1]);
    if (!str) {
        runtime_error("StringBuilder: could not convert value to string");
    }

    sb_append_cstr(sb, str);
    free(str);
    sb_append_cstr(sb, "\n");

    return args[0];  /* return builder for chaining */
}

/* native: clear(builder) -> builder */
static RuntimeValue native_clear(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    if (arg_count < 1) {
        runtime_error("clear expects 1 argument");
    }

    MksStringBuilder *sb = get_builder_ptr(args[0]);
    sb->len = 0;
    sb->data[0] = '\0';

    return args[0];  /* return builder for chaining */
}

/* native: len(builder) -> number */
static RuntimeValue native_len(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    if (arg_count < 1) {
        runtime_error("len expects 1 argument");
    }

    MksStringBuilder *sb = get_builder_ptr(args[0]);
    return make_int((int64_t)sb->len);
}

/* native: capacity(builder) -> number */
static RuntimeValue native_capacity(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    if (arg_count < 1) {
        runtime_error("capacity expects 1 argument");
    }

    MksStringBuilder *sb = get_builder_ptr(args[0]);
    return make_int((int64_t)sb->cap);
}

/* native: to_string(builder) -> string */
static RuntimeValue native_to_string(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    if (arg_count < 1) {
        runtime_error("to_string expects 1 argument");
    }

    MksStringBuilder *sb = get_builder_ptr(args[0]);
    return make_string(sb->data);
}

/* native: reserve(builder, n) -> builder */
static RuntimeValue native_reserve(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    if (arg_count < 2) {
        runtime_error("reserve expects 2 arguments");
    }

    if (args[1].type != VAL_INT && args[1].type != VAL_FLOAT) {
        runtime_error("reserve: second argument must be a number");
    }

    MksStringBuilder *sb = get_builder_ptr(args[0]);
    size_t n = args[1].type == VAL_INT ? (size_t)args[1].data.int_value
                                         : (size_t)args[1].data.float_value;

    if (sb->cap < n) {
        char *new_data = (char *)realloc(sb->data, n);
        if (!new_data) {
            runtime_error("StringBuilder: out of memory");
        }
        sb->data = new_data;
        sb->cap = n;
    }

    return args[0];  /* return builder for chaining */
}

/* Module initialization */
void mks_module_init_string_builder(RuntimeValue exports, Environment *module_env) {
    (void)module_env;  /* unused */
    MKS_EXPORT_NATIVE(exports, "builder", native_builder);
    MKS_EXPORT_NATIVE(exports, "append", native_append);
    MKS_EXPORT_NATIVE(exports, "append_raw", native_append_raw);
    MKS_EXPORT_NATIVE(exports, "append_line", native_append_line);
    MKS_EXPORT_NATIVE(exports, "clear", native_clear);
    MKS_EXPORT_NATIVE(exports, "len", native_len);
    MKS_EXPORT_NATIVE(exports, "capacity", native_capacity);
    MKS_EXPORT_NATIVE(exports, "to_string", native_to_string);
    MKS_EXPORT_NATIVE(exports, "reserve", native_reserve);
}

/* ABI version declaration */
MKS_MODULE_DECLARE_ABI
