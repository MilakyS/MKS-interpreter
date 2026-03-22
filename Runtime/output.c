#include "output.h"
#include <stdio.h>
#include "../Eval/eval.h"
#include "../GC/gc.h"

void print_value(RuntimeValue val) {
    switch (val.type) {
        case VAL_INT:
            printf("%g", val.data.float_value);
            break;

        case VAL_STRING:
            if (val.data.managed_string && val.data.managed_string->data) {
                printf("%s", val.data.managed_string->data);
            } else {
                printf("null");
            }
            break;

        case VAL_ARRAY:
            printf("[");
            if (val.data.managed_array) {
                for (int i = 0; i < val.data.managed_array->count; i++) {
                    print_value(val.data.managed_array->elements[i]);
                    if (i < val.data.managed_array->count - 1) printf(", ");
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

        default:
            printf("<Unknown Type>");
            break;
    }
}

RuntimeValue eval_output(const ASTNode *node, Environment *env) {
    for (int i = 0; i < node->data.Output.arg_count; i++) {
        if (mks_gc.allocated_bytes > mks_gc.threshold) {
            gc_collect(env, env);
        }

        RuntimeValue val = eval(node->data.Output.args[i], env);
        gc_push_root(&val);

        print_value(val);

        gc_pop_root();

        if (i < node->data.Output.arg_count - 1) {
            printf(" ");
        }
    }

    if (node->data.Output.is_newline) {
        printf("\n");
    }
    return make_int(0);
}