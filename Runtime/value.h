#ifndef MONKEYKERNELSYNTAX_VALUE_H
#define MONKEYKERNELSYNTAX_VALUE_H

#include <stddef.h>
#include <stdbool.h>
#include "../GC/gc.h"

struct Environment;

struct ASTNode;
struct Environment;

typedef struct RuntimeValue RuntimeValue;

typedef struct {
    GCObject gc;
    char *data;
    size_t len;
} ManagedString;

typedef struct {
    GCObject gc;
    RuntimeValue *elements;
    int count;
    int capacity;
} ManagedArray;

enum ValueType {
    VAL_INT,
    VAL_STRING,
    VAL_ARRAY,
    VAL_FUNC,
    VAL_NATIVE_FUNC,
    VAL_RETURN,
    VAL_BREAK,
    VAL_CONTINUE,
    VAL_OBJECT,
    VAL_BLUEPRINT,
    VAL_NULL,
};

typedef RuntimeValue (*NativeFn)(const RuntimeValue *args, int arg_count);

typedef struct NativeWithCtx {
    NativeFn fn;
    void *ctx;
} NativeWithCtx;

typedef struct ModuleRecord {
    void *ptr; /* opaque link back to module env if needed */
} ModuleRecord;

struct RuntimeValue {
    enum ValueType type;
    enum ValueType original_type;

    union {
        double float_value;
        ManagedString *managed_string;
        ManagedArray *managed_array;

        struct {
            struct ASTNode *node;
            struct Environment *closure_env;
        } func;

        NativeWithCtx native;
        struct Environment *obj_env;

        struct {
            const struct ASTNode *entity_node;
            struct Environment *closure_env;
        } blueprint;
    } data;
};

RuntimeValue make_int(double val);
RuntimeValue make_string(const char *str);
RuntimeValue make_string_raw(const char *str);
RuntimeValue make_string_owned(char *str, size_t len);
RuntimeValue make_string_len(const char *str, size_t len);
RuntimeValue make_array(int initial_capacity);
RuntimeValue make_null(void);
RuntimeValue make_object(struct Environment *env);
RuntimeValue make_blueprint(const struct ASTNode *entity_node, struct Environment *closure_env);
RuntimeValue make_break(void);
RuntimeValue make_continue(void);

#endif
