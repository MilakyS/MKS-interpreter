#include "output.h"
#include <stdio.h>

#include "../Eval/eval.h"
#include "../GC/gc.h"

static void print_value(const RuntimeValue *val) {
    if (val == NULL) {
        printf("null");
        return;
    }

    switch (val->type) {
        case VAL_INT:
            printf("%g", val->data.float_value);
            break;

        case VAL_STRING:
            if (val->data.managed_string != NULL &&
                val->data.managed_string->data != NULL) {
                printf("%s", val->data.managed_string->data);
            } else {
                printf("null");
            }
            break;

        case VAL_ARRAY:
            printf("[");
            if (val->data.managed_array != NULL) {
                const int count = val->data.managed_array->count;
                for (int i = 0; i < count; i++) {
                    print_value(&val->data.managed_array->elements[i]);
                    if (i < count - 1) {
                        printf(", ");
                    }
                }
            }
            printf("]");
            break;

        case VAL_OBJECT:
            printf("<Module/Object>");
            break;

        case VAL_FUNC:
            printf("<Function>");
            break;

        case VAL_NATIVE_FUNC:
            printf("<Native Function>");
            break;

        case VAL_NULL:
            printf("null");
            break;

        case VAL_RETURN: {
            RuntimeValue temp = *val;
            temp.type = temp.original_type;
            print_value(&temp);
            break;
        }

        default:
            printf("<Unknown Type>");
            break;
    }
}

RuntimeValue eval_output(const ASTNode *node, Environment *env) {
    if (node == NULL) {
        return make_int(0);
    }

    ASTNode **args = node->data.output.args;
    const int count = node->data.output.arg_count;
    const bool is_newline = node->data.output.is_newline;

    for (int i = 0; i < count; i++) {
        if (mks_gc.allocated_bytes > mks_gc.threshold) {
            gc_collect(env, env);
        }

        RuntimeValue val = eval(args[i], env);
        gc_push_root(&val);

        print_value(&val);

        gc_pop_root();

        if (i < count - 1) {
            printf(" ");
        }
    }

    if (is_newline) {
        printf("\n");
    }

    return make_int(0);
}