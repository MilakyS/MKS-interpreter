#include "value.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

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
    return str;
}

RuntimeValue make_int(const double val) {
    RuntimeValue v;
    v.type = VAL_INT;
    v.original_type = VAL_INT;
    v.data.float_value = val;
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
        return v;
    }

    v.data.managed_string->data = str;
    v.data.managed_string->len = len;
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

    return v;
}

RuntimeValue make_null(void) {
    RuntimeValue v;
    v.type = VAL_NULL;
    v.original_type = VAL_NULL;
    v.data.float_value = 0;
    return v;
}