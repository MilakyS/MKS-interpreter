//
// Created by MilakyS on 21.03.2026.
//

#ifndef MONKEYKERNELSYNTAX_VALUE_H
#define MONKEYKERNELSYNTAX_VALUE_H
#pragma once

#include <stddef.h>

struct ASTNode;
struct Environment;

typedef struct RuntimeValue RuntimeValue;

typedef struct RuntimeValue (*NativeFn)(const RuntimeValue *args, int arg_count);
RuntimeValue make_int(double val);
RuntimeValue make_string(const char *str);
RuntimeValue make_array(RuntimeValue *elements, int count);

enum ValueType {
    VAL_INT,
    VAL_STRING,
    VAL_ARRAY,
    VAL_FUNC,
    VAL_NATIVE_FUNC,
    VAL_RETURN,
    VAL_OBJECT
};

struct RuntimeValue {
    enum ValueType type;
    enum ValueType original_type;

    union {
        double float_value;
        char *string_value;

        struct {
            RuntimeValue *elements;
            int count;
        } array_data;

        struct {
            struct ASTNode *node;
            struct Environment *closure_env;
        } func;

        NativeFn native_func;

        struct Environment *obj_env;
    } data;
};

#endif //MONKEYKERNELSYNTAX_VALUE_H