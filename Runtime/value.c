#include "value.h"
#include <stdlib.h>
#include <string.h>

RuntimeValue make_int(double val) {
    RuntimeValue v;
    v.type = VAL_INT;
    v.data.float_value = val;
    v.original_type = VAL_INT;
    return v;
}

RuntimeValue make_string(const char *str) {
    RuntimeValue v;
    v.type = VAL_STRING;
    v.data.string_value = str ? strdup(str) : strdup("");
    v.original_type = VAL_STRING;
    return v;
}

RuntimeValue make_array(RuntimeValue *elements, int count) {
    RuntimeValue v;
    v.type = VAL_ARRAY;
    v.data.array_data.elements = elements;
    v.data.array_data.count = count;
    v.original_type = VAL_ARRAY;
    return v;
}