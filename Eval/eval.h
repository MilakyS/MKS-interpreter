#ifndef CMINUSINTERPRETATOR_EVAL_H
#define CMINUSINTERPRETATOR_EVAL_H

#include "../Parser/AST.h"

#define TABLE_SIZE 256

// Предварительное объявление для использования в NativeFn
struct RuntimeValue;

typedef struct RuntimeValue (*NativeFn)(struct RuntimeValue *args, int arg_count);

enum ValueType {
    VAL_INT,
    VAL_STRING,
    VAL_ARRAY,       // НОВЫЙ ТИП: Для списков
    VAL_FUNC,
    VAL_NATIVE_FUNC,
    VAL_RETURN,
    VAL_OBJECT
};

typedef struct RuntimeValue {
    enum ValueType type;
    enum ValueType original_type;
    union {
        int int_value;
        char *string_value;
        struct {
            struct RuntimeValue *elements;
            int count;
        } array_data;

        // ВОТ ТУТ ПРАВКА ДЛЯ ЗАМЫКАНИЙ
        struct {
            struct ASTNode *node;            // Указатель на тело функции
            struct Environment *closure_env; // Указатель на "родное" окружение
        } func;

        NativeFn native_func;
        struct Environment *obj_env;
    } data;
} RuntimeValue;

typedef struct EnvVar {
    char *name;
    RuntimeValue value;
    struct EnvVar *next;
} EnvVar;

typedef struct Environment {
    EnvVar *buckets[TABLE_SIZE];
    struct Environment *parent;
} Environment;

// Функции окружения
void env_init(Environment *env);
void env_free(const Environment *env);
void env_set(Environment *env, const char *name, RuntimeValue value);
RuntimeValue env_get(const Environment *env, const char *name);
RuntimeValue env_get_fast(const Environment *env, const char *name, const unsigned int h);
void env_update_fast(Environment *env, const char *name, const unsigned int h, RuntimeValue value);

// Помощники создания значений
RuntimeValue make_int(int val);
RuntimeValue make_string(const char *str);
RuntimeValue make_array(RuntimeValue *elements, int count); // НОВЫЙ ПРОТОТИП

// Главная функция интерпретации
RuntimeValue eval(const ASTNode *node, Environment *env);

#endif