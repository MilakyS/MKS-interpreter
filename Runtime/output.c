#include "output.h"
#include <stdio.h>
#include "../Eval/eval.h"

void print_value(RuntimeValue val) {
    if (val.type == VAL_INT) {
        printf("%g", val.data.float_value);
    } else if (val.type == VAL_STRING) {
        printf("%s", val.data.string_value ? val.data.string_value : "null");
    } else if (val.type == VAL_ARRAY) {
        printf("[");
        for (int i = 0; i < val.data.array_data.count; i++) {
            print_value(val.data.array_data.elements[i]);
            if (i < val.data.array_data.count - 1) printf(", ");
        }
        printf("]");
    } else if (val.type == VAL_OBJECT) {
        printf("<Module>");
    } else if (val.type == VAL_FUNC) {
        printf("<Function>");
    }
}

RuntimeValue eval_output(const ASTNode *node, Environment *env) {
    for (int i = 0; i < node->data.Output.arg_count; i++) {
        RuntimeValue val = eval(node->data.Output.args[i], env);

        print_value(val);

        if (i < node->data.Output.arg_count - 1) {
            printf(" ");
        }
    }

    if (node->data.Output.is_newline) {
        printf("\n");
    }

    return make_int(0);
}