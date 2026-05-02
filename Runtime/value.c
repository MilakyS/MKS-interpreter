#include "value.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#include "../env/env.h"
#include "../Parser/AST.h"
#include "../Utils/hash.h"

static char *mks_strdup(const char *src) {
    if (src == NULL) {
        src = "";
    }

    const size_t len = strlen(src);
    char *copy = (char *)malloc(len + 1);
    if (copy == NULL) {
        fprintf(stderr, "[MKS Value Error] Out of memory while copying string\n");
        exit(1);
    }

    memcpy(copy, src, len + 1);
    return copy;
}

static ManagedString *alloc_managed_string(void) {
    ManagedString *str = (ManagedString *)gc_alloc(sizeof(ManagedString), GC_OBJ_STRING);
    str->data = NULL;
    str->len = 0;
    str->hash = 0;
    return str;
}

RuntimeValue make_int(const int64_t val) {
    RuntimeValue v;
    v.type = VAL_INT;
    v.original_type = VAL_INT;
    v.data.int_value = val;
    return v;
}

RuntimeValue make_float(const double val) {
    RuntimeValue v;
    v.type = VAL_FLOAT;
    v.original_type = VAL_FLOAT;
    v.data.float_value = val;
    return v;
}

RuntimeValue make_bool(const bool val) {
    RuntimeValue v;
    v.type = VAL_BOOL;
    v.original_type = VAL_BOOL;
    v.data.bool_value = val;
    return v;
}

int runtime_value_is_number(RuntimeValue val) {
    if (val.type == VAL_RETURN) {
        val.type = val.original_type;
    }

    return val.type == VAL_INT || val.type == VAL_FLOAT;
}

double runtime_value_as_double(RuntimeValue val) {
    if (val.type == VAL_RETURN) {
        val.type = val.original_type;
    }

    if (val.type == VAL_INT) {
        return (double)val.data.int_value;
    }
    if (val.type == VAL_BOOL) {
        return val.data.bool_value ? 1.0 : 0.0;
    }
    if (val.type == VAL_FLOAT) {
        return val.data.float_value;
    }
    return 0.0;
}

int64_t runtime_value_as_int(RuntimeValue val) {
    if (val.type == VAL_RETURN) {
        val.type = val.original_type;
    }

    if (val.type == VAL_INT) {
        return val.data.int_value;
    }
    if (val.type == VAL_BOOL) {
        return val.data.bool_value ? 1 : 0;
    }
    if (val.type == VAL_FLOAT) {
        return (int64_t)val.data.float_value;
    }
    return 0;
}

RuntimeValue make_number_from_double(const double val) {
    double integral = 0.0;
    if (isfinite(val) && modf(val, &integral) == 0.0 &&
        integral >= (double)INT64_MIN && integral <= (double)INT64_MAX) {
        return make_int((int64_t)integral);
    }
    return make_float(val);
}

RuntimeValue make_object(Environment *env) {
    RuntimeValue v;
    v.type = VAL_OBJECT;
    v.original_type = VAL_OBJECT;
    v.data.obj_env = env;
    return v;
}

RuntimeValue make_module(Environment *env) {
    RuntimeValue v;
    v.type = VAL_MODULE;
    v.original_type = VAL_MODULE;
    v.data.obj_env = env;
    return v;
}

RuntimeValue make_string_owned(char *str, size_t len) {
    RuntimeValue v;
    v.type = VAL_STRING;
    v.original_type = VAL_STRING;

    v.data.managed_string = alloc_managed_string();

    if (str == NULL) {
        v.data.managed_string->data = mks_strdup("");
        v.data.managed_string->len = 0;
        v.data.managed_string->hash = get_hash(v.data.managed_string->data);
        v.data.managed_string->gc.external_size = 1;
        return v;
    }

    v.data.managed_string->data = str;
    v.data.managed_string->len = len;
    v.data.managed_string->hash = get_hash(v.data.managed_string->data);
    v.data.managed_string->gc.external_size = len + 1;
    return v;
}

RuntimeValue make_string_len(const char *str, size_t len) {
    RuntimeValue v;
    v.type = VAL_STRING;
    v.original_type = VAL_STRING;

    v.data.managed_string = alloc_managed_string();

    if (str == NULL) {
        v.data.managed_string->data = mks_strdup("");
        v.data.managed_string->len = 0;
        v.data.managed_string->hash = get_hash(v.data.managed_string->data);
        v.data.managed_string->gc.external_size = 1;
        return v;
    }

    char *copy = (char *)malloc(len + 1);
    if (copy == NULL) {
        fprintf(stderr, "[MKS Value Error] Out of memory while creating string\n");
        exit(1);
    }

    memcpy(copy, str, len);
    copy[len] = '\0';

    v.data.managed_string->data = copy;
    v.data.managed_string->len = len;
    v.data.managed_string->hash = get_hash(v.data.managed_string->data);
    v.data.managed_string->gc.external_size = len + 1;
    return v;
}

RuntimeValue make_string_raw(const char *str) {
    if (str == NULL) {
        return make_string_len("", 0);
    }

    return make_string_len(str, strlen(str));
}

RuntimeValue make_string(const char *str) {
    RuntimeValue v;
    v.type = VAL_STRING;
    v.original_type = VAL_STRING;

    v.data.managed_string = alloc_managed_string();

    if (str == NULL) {
        v.data.managed_string->data = mks_strdup("");
        v.data.managed_string->len = 0;
        v.data.managed_string->gc.external_size = 1;
        return v;
    }

    const size_t len = strlen(str);
    char *processed = (char *)malloc(len + 1);
    if (processed == NULL) {
        fprintf(stderr, "[MKS Value Error] Out of memory while creating string\n");
        exit(1);
    }

    size_t p_idx = 0;

    for (size_t i = 0; i < len; i++) {
        if (str[i] == '\\' && i + 1 < len) {
            switch (str[++i]) {
                case 'n':
                    processed[p_idx++] = '\n';
                    break;
                case 't':
                    processed[p_idx++] = '\t';
                    break;
                case 'r':
                    processed[p_idx++] = '\r';
                    break;
                case '\\':
                    processed[p_idx++] = '\\';
                    break;
                case '"':
                    processed[p_idx++] = '"';
                    break;
                case '\'':
                    processed[p_idx++] = '\'';
                    break;
                default:
                    processed[p_idx++] = '\\';
                    processed[p_idx++] = str[i];
                    break;
            }
        } else {
            processed[p_idx++] = str[i];
        }
    }

    processed[p_idx] = '\0';
    v.data.managed_string->data = processed;
    v.data.managed_string->len = p_idx;
    v.data.managed_string->hash = get_hash(v.data.managed_string->data);
    v.data.managed_string->gc.external_size = p_idx + 1;

    return v;
}

RuntimeValue make_array(int initial_capacity) {
    RuntimeValue v;
    v.type = VAL_ARRAY;
    v.original_type = VAL_ARRAY;

    if (initial_capacity <= 0) {
        initial_capacity = 8;
    }

    v.data.managed_array = (ManagedArray *)gc_alloc(sizeof(ManagedArray), GC_OBJ_ARRAY);
    v.data.managed_array->count = 0;
    v.data.managed_array->capacity = initial_capacity;

    v.data.managed_array->elements =
        (RuntimeValue *)malloc(sizeof(RuntimeValue) * (size_t)initial_capacity);

    if (v.data.managed_array->elements == NULL) {
        fprintf(stderr, "[MKS Value Error] Out of memory while creating array\n");
        exit(1);
    }

    v.data.managed_array->gc.external_size = sizeof(RuntimeValue) * (size_t)initial_capacity;
    return v;
}

static ManagedPointer *alloc_managed_pointer(void) {
    ManagedPointer *ptr = (ManagedPointer *)gc_alloc(sizeof(ManagedPointer), GC_OBJ_POINTER);
    ptr->kind = PTR_ENV_VAR;
    ptr->as.var.env = NULL;
    ptr->as.var.entry = NULL;
    return ptr;
}

RuntimeValue make_pointer_to_var(Environment *env, EnvVar *entry) {
    RuntimeValue v;
    v.type = VAL_POINTER;
    v.original_type = VAL_POINTER;
    v.data.managed_pointer = alloc_managed_pointer();
    v.data.managed_pointer->kind = PTR_ENV_VAR;
    v.data.managed_pointer->as.var.env = env;
    v.data.managed_pointer->as.var.entry = entry;
    return v;
}

RuntimeValue make_pointer_to_array_elem(ManagedArray *array, int index) {
    RuntimeValue v;
    v.type = VAL_POINTER;
    v.original_type = VAL_POINTER;
    v.data.managed_pointer = alloc_managed_pointer();
    v.data.managed_pointer->kind = PTR_ARRAY_ELEM;
    v.data.managed_pointer->as.array_elem.array = array;
    v.data.managed_pointer->as.array_elem.index = index;
    return v;
}

RuntimeValue make_pointer_to_object_field(Environment *env, const char *field, unsigned int hash) {
    RuntimeValue v;
    v.type = VAL_POINTER;
    v.original_type = VAL_POINTER;
    v.data.managed_pointer = alloc_managed_pointer();
    v.data.managed_pointer->kind = PTR_OBJECT_FIELD;
    v.data.managed_pointer->as.object_field.env = env;
    v.data.managed_pointer->as.object_field.hash = hash;
    v.data.managed_pointer->as.object_field.field = mks_strdup(field);
    return v;
}

RuntimeValue make_null(void) {
    RuntimeValue v;
    v.type = VAL_NULL;
    v.original_type = VAL_NULL;
    v.data.int_value = 0;
    return v;
}

RuntimeValue make_blueprint(const ASTNode *entity_node, Environment *closure_env) {
    RuntimeValue v;
    v.type = VAL_BLUEPRINT;
    v.original_type = VAL_BLUEPRINT;
    v.data.blueprint.entity_node = entity_node;
    v.data.blueprint.closure_env = closure_env;
    return v;
}

RuntimeValue make_break(void) {
    RuntimeValue v;
    v.type = VAL_BREAK;
    v.original_type = VAL_BREAK;
    v.data.int_value = 0;
    return v;
}

RuntimeValue make_continue(void) {
    RuntimeValue v;
    v.type = VAL_CONTINUE;
    v.original_type = VAL_CONTINUE;
    v.data.int_value = 0;
    return v;
}

RuntimeValue sb_make(void) {
    RuntimeValue v;
    v.type = VAL_STRING_BUILDER;
    v.original_type = VAL_STRING_BUILDER;

    v.data.string_builder = (ManagedStringBuilder *)gc_alloc(sizeof(ManagedStringBuilder), GC_OBJ_STRING_BUILDER);
    v.data.string_builder->capacity = 256;
    v.data.string_builder->len = 0;
    v.data.string_builder->data = (char *)malloc(256);

    if (v.data.string_builder->data == NULL) {
        fprintf(stderr, "[MKS Builder Error] Out of memory while creating string builder\n");
        exit(1);
    }

    v.data.string_builder->gc.external_size = 256;
    return v;
}

void sb_append_string(RuntimeValue *builder, const char *str, size_t len) {
    if (builder->type != VAL_STRING_BUILDER) {
        return;
    }

    ManagedStringBuilder *sb = builder->data.string_builder;
    size_t needed = sb->len + len;

    if (needed > sb->capacity) {
        size_t new_cap = sb->capacity * 2;
        while (new_cap < needed) {
            new_cap *= 2;
        }
        char *new_data = (char *)realloc(sb->data, new_cap);
        if (new_data == NULL) {
            fprintf(stderr, "[MKS Builder Error] Out of memory while appending to builder\n");
            exit(1);
        }
        sb->data = new_data;
        sb->capacity = new_cap;
        sb->gc.external_size = new_cap;
    }

    memcpy(sb->data + sb->len, str, len);
    sb->len += len;
}

void sb_append_value(RuntimeValue *builder, const RuntimeValue *val) {
    if (val->type == VAL_STRING) {
        ManagedString *s = val->data.managed_string;
        sb_append_string(builder, s->data, s->len);
    }
}

RuntimeValue sb_to_string(const RuntimeValue *builder) {
    if (builder->type != VAL_STRING_BUILDER) {
        return make_string("");
    }

    const ManagedStringBuilder *sb = builder->data.string_builder;
    char *result = (char *)malloc(sb->len + 1);
    if (result == NULL) {
        fprintf(stderr, "[MKS Builder Error] Out of memory while finalizing string\n");
        exit(1);
    }
    memcpy(result, sb->data, sb->len);
    result[sb->len] = '\0';
    return make_string_owned(result, sb->len);
}
