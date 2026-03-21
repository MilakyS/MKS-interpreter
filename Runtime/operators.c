#include "operators.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../Eval/eval.h"
#include  "../Lexer/lexer.h"

RuntimeValue eval_binop(const ASTNode *node, Environment *env) {
    const int op = node->data.binop.op;

    RuntimeValue left_val = eval(node->data.binop.left, env);
    if (left_val.type == VAL_RETURN) left_val.type = left_val.original_type;

    RuntimeValue right_val = eval(node->data.binop.right, env);
    if (right_val.type == VAL_RETURN) right_val.type = right_val.original_type;

    // String concatenation
    if (op == TOKEN_PLUS && (left_val.type == VAL_STRING || right_val.type == VAL_STRING)) {
        char buf_l[128], buf_r[128];
        char *s_l, *s_r;

        if (left_val.type == VAL_INT) {
            sprintf(buf_l, "%g", left_val.data.float_value);
            s_l = buf_l;
        } else if (left_val.type == VAL_STRING) {
            s_l = left_val.data.string_value;
        } else {
            s_l = "[Object]";
        }

        if (right_val.type == VAL_INT) {
            sprintf(buf_r, "%g", right_val.data.float_value);
            s_r = buf_r;
        } else if (right_val.type == VAL_STRING) {
            s_r = right_val.data.string_value;
        } else {
            s_r = "[Object]";
        }

        char *res = malloc(strlen(s_l) + strlen(s_r) + 1);
        strcpy(res, s_l);
        strcat(res, s_r);

        RuntimeValue out = make_string(res);
        free(res);
        return out;
    }

    // Numeric operations
    if (left_val.type == VAL_INT && right_val.type == VAL_INT) {
        double l = left_val.data.float_value;
        double r = right_val.data.float_value;

        switch (op) {
            case TOKEN_PLUS: return make_int(l + r);
            case TOKEN_MINUS: return make_int(l - r);
            case TOKEN_STAR: return make_int(l * r);
            case TOKEN_SLASH:
                if (r == 0) {
                    printf("Runtime Error: division by zero\n");
                    exit(1);
                }
                return make_int(l / r);
            case TOKEN_MOD: return make_int(fmod(l, r));

            case TOKEN_EQ: return make_int(l == r);
            case TOKEN_NOT_EQ: return make_int(l != r);
            case TOKEN_LESS: return make_int(l < r);
            case TOKEN_GREATER: return make_int(l > r);
            case TOKEN_LESS_EQUAL: return make_int(l <= r);
            case TOKEN_GREATER_EQUAL: return make_int(l >= r);

            case TOKEN_AND: return make_int(l && r);
            case TOKEN_OR: return make_int(l || r);
        }
    }

    return make_int(0);
}