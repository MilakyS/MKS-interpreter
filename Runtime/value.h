#ifndef MONKEYKERNELSYNTAX_VALUE_H
#define MONKEYKERNELSYNTAX_VALUE_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include "../GC/gc.h"

struct Environment;
struct EnvVar;

struct ASTNode;
struct Environment;
struct MKSContext;

typedef struct RuntimeValue RuntimeValue;

typedef struct {
    GCObject gc;
    char *data;
    size_t len;
    unsigned int hash;
} ManagedString;

typedef struct {
    GCObject gc;
    RuntimeValue *elements;
    int count;
    int capacity;
} ManagedArray;

typedef enum PointerTargetKind {
    PTR_ENV_VAR,
    PTR_ARRAY_ELEM,
    PTR_OBJECT_FIELD
} PointerTargetKind;

typedef struct ManagedPointer {
    GCObject gc;
    PointerTargetKind kind;
    union {
        struct {
            struct Environment *env;
            struct EnvVar *entry;
        } var;
        struct {
            ManagedArray *array;
            int index;
        } array_elem;
        struct {
            struct Environment *env;
            char *field;
            unsigned int hash;
        } object_field;
    } as;
} ManagedPointer;

typedef struct {
    GCObject gc;
    char *data;
    size_t len;
    size_t capacity;
} ManagedStringBuilder;

enum ValueType {
    VAL_INT,
    VAL_FLOAT,
    VAL_BOOL,
    VAL_STRING,
    VAL_ARRAY,
    VAL_POINTER,
    VAL_FUNC,
    VAL_NATIVE_FUNC,
    VAL_RETURN,
    VAL_BREAK,
    VAL_CONTINUE,
    VAL_OBJECT,
    VAL_MODULE,
    VAL_BLUEPRINT,
    VAL_NULL,
    VAL_STRING_BUILDER,
};

typedef RuntimeValue (*NativeFn)(struct MKSContext *ctx, const RuntimeValue *args, int arg_count);

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
        int64_t int_value;
        double float_value;
        bool bool_value;
        ManagedString *managed_string;
        ManagedArray *managed_array;
        ManagedPointer *managed_pointer;
        ManagedStringBuilder *string_builder;

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

RuntimeValue make_int(int64_t val);
RuntimeValue make_float(double val);
RuntimeValue make_bool(bool val);
int runtime_value_is_number(RuntimeValue val);
double runtime_value_as_double(RuntimeValue val);
int64_t runtime_value_as_int(RuntimeValue val);
RuntimeValue make_number_from_double(double val);
RuntimeValue make_string(const char *str);
RuntimeValue make_string_raw(const char *str);
RuntimeValue make_string_owned(char *str, size_t len);
RuntimeValue make_string_len(const char *str, size_t len);
RuntimeValue make_array(int initial_capacity);
RuntimeValue make_pointer_to_var(struct Environment *env, struct EnvVar *entry);
RuntimeValue make_pointer_to_array_elem(ManagedArray *array, int index);
RuntimeValue make_pointer_to_object_field(struct Environment *env, const char *field, unsigned int hash);
RuntimeValue make_null(void);
RuntimeValue make_object(struct Environment *env);
RuntimeValue make_module(struct Environment *env);
RuntimeValue make_blueprint(const struct ASTNode *entity_node, struct Environment *closure_env);
RuntimeValue make_break(void);
RuntimeValue make_continue(void);

RuntimeValue sb_make(void);
void sb_append_string(RuntimeValue *builder, const char *str, size_t len);
void sb_append_value(RuntimeValue *builder, const RuntimeValue *val);
RuntimeValue sb_to_string(const RuntimeValue *builder);

#endif
