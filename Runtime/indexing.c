#include "indexing.h"
#include "errors.h"
#include <stdlib.h>

RuntimeValue runtime_get_index(RuntimeValue target, RuntimeValue index) {
    if (target.type == VAL_RETURN) {
        target.type = target.original_type;
    }
    if (index.type == VAL_RETURN) {
        index.type = index.original_type;
    }

    const int i = (int)runtime_value_as_int(index);
    if (target.type == VAL_STRING) {
        const ManagedString *str = target.data.managed_string;
        const int len = (int)str->len;

        if (i < 0 || i >= len) {
            runtime_error("String index %d out of bounds (length %d)", i, len);
        }

        char *tmp = (char *)malloc(2);
        if (tmp == NULL) {
            runtime_error("Out of memory in string indexing");
        }

        tmp[0] = str->data[i];
        tmp[1] = '\0';
        return make_string_owned(tmp, 1);
    }

    if (target.type == VAL_ARRAY) {
        const ManagedArray *arr = target.data.managed_array;

        if (i < 0 || i >= arr->count) {
            runtime_error("Array index %d out of bounds (size %d)", i, arr->count);
        }

        return arr->elements[i];
    }

    runtime_error("Type is not indexable");
    return make_null();
}

RuntimeValue runtime_set_index(RuntimeValue target, RuntimeValue index, RuntimeValue value) {
    if (target.type == VAL_RETURN) {
        target.type = target.original_type;
    }
    if (index.type == VAL_RETURN) {
        index.type = index.original_type;
    }
    if (value.type == VAL_RETURN) {
        value.type = value.original_type;
    }

    const int i = (int)runtime_value_as_int(index);
    if (target.type != VAL_ARRAY) {
        runtime_error("Only arrays support index assignment");
    }

    ManagedArray *arr = target.data.managed_array;
    if (i < 0 || i >= arr->count) {
        runtime_error("Array index %d out of bounds in assignment (size %d)", i, arr->count);
    }

    arr->elements[i] = value;
    return value;
}
