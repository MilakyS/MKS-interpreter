#include "value.h"
#include <stdlib.h>
#include <string.h>

RuntimeValue make_int(double val) {
    RuntimeValue v;
    v.type = VAL_INT;
    v.original_type = VAL_INT;
    v.data.float_value = val;
    return v;
}

RuntimeValue make_string(const char *str) {
    RuntimeValue v;
    v.type = VAL_STRING;
    v.original_type = VAL_STRING;

    v.data.managed_string = (ManagedString*)gc_alloc(sizeof(ManagedString), GC_OBJ_STRING);

    if (!str) {
        v.data.managed_string->data = strdup("");
        return v;
    }

    size_t len = strlen(str);
    char *processed = (char*)malloc(len + 1);
    size_t p_idx = 0;

    for (size_t i = 0; i < len; i++) {
        if (str[i] == '\\' && i + 1 < len) {
            switch (str[++i]) {
                case 'n':  processed[p_idx++] = '\n'; break;
                case 't':  processed[p_idx++] = '\t'; break;
                case 'r':  processed[p_idx++] = '\r'; break;
                case '\\': processed[p_idx++] = '\\'; break;
                case '\"': processed[p_idx++] = '\"'; break;
                case '\'': processed[p_idx++] = '\''; break;
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

    return v;
}

RuntimeValue make_array(int initial_capacity) {
    RuntimeValue v;
    v.type = VAL_ARRAY;
    v.original_type = VAL_ARRAY;

    if (initial_capacity <= 0) initial_capacity = 8;

    v.data.managed_array = (ManagedArray*)gc_alloc(sizeof(ManagedArray), GC_OBJ_ARRAY);

    v.data.managed_array->count = 0;
    v.data.managed_array->capacity = initial_capacity;

    v.data.managed_array->elements = (RuntimeValue*)malloc(sizeof(RuntimeValue) * initial_capacity);

    return v;
}
RuntimeValue make_null() {
    RuntimeValue v;
    v.type = VAL_NULL;
    v.data.float_value = 0;
    return v;
}