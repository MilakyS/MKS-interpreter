#include "operators.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../Eval/eval.h"
#include "../Lexer/lexer.h"
#include "../GC/gc.h"

RuntimeValue eval_binop(const ASTNode *node, Environment *env) {
    const int op = node->data.binop.op;

    RuntimeValue left_val = eval(node->data.binop.left, env);
    if (left_val.type == VAL_RETURN) left_val.type = left_val.original_type;
    gc_push_root(&left_val);

    if (op == TOKEN_AND) {
        if (left_val.type != VAL_INT || left_val.data.float_value == 0.0) {
            gc_pop_root();
            return make_int(0);
        }
    }
    else if (op == TOKEN_OR) {
        if (left_val.type == VAL_INT && left_val.data.float_value != 0.0) {
            gc_pop_root();
            return make_int(1);
        }
    }

    RuntimeValue right_val = eval(node->data.binop.right, env);
    if (right_val.type == VAL_RETURN) right_val.type = right_val.original_type;
    gc_push_root(&right_val);

    if (left_val.type == VAL_STRING && right_val.type == VAL_STRING) {
        const char *s_l = left_val.data.managed_string->data;
        const char *s_r = right_val.data.managed_string->data;

        if (op == TOKEN_EQ) {
            gc_pop_root(); gc_pop_root();
            return make_int(strcmp(s_l, s_r) == 0);
        }
        if (op == TOKEN_NOT_EQ) {
            gc_pop_root(); gc_pop_root();
            return make_int(strcmp(s_l, s_r) != 0);
        }
    }

    if (op == TOKEN_PLUS && (left_val.type == VAL_STRING || right_val.type == VAL_STRING)) {
        char buf_l[128], buf_r[128];
        const char *s_l, *s_r;

        if (left_val.type == VAL_INT) {
            sprintf(buf_l, "%g", left_val.data.float_value);
            s_l = buf_l;
        } else if (left_val.type == VAL_STRING) {
            s_l = left_val.data.managed_string->data;
        } else {
            s_l = "[Object]";
        }

        if (right_val.type == VAL_INT) {
            sprintf(buf_r, "%g", right_val.data.float_value);
            s_r = buf_r;
        } else if (right_val.type == VAL_STRING) {
            s_r = right_val.data.managed_string->data;
        } else {
            s_r = "[Object]";
        }

        char *res_str = malloc(strlen(s_l) + strlen(s_r) + 1);
        strcpy(res_str, s_l);
        strcat(res_str, s_r);

        RuntimeValue out = make_string(res_str);

        free(res_str);
        gc_pop_root();
        gc_pop_root();
        return out;
    }

    if (left_val.type == VAL_INT && right_val.type == VAL_INT) {
        double l = left_val.data.float_value;
        double r = right_val.data.float_value;
        RuntimeValue result = make_int(0);

        switch (op) {
            case TOKEN_PLUS:  result = make_int(l + r); break;
            case TOKEN_MINUS: result = make_int(l - r); break;
            case TOKEN_STAR:  result = make_int(l * r); break;
            case TOKEN_SLASH:
                if (r == 0) {
                    printf("Runtime Error: division by zero\n");
                    exit(1);
                }
                result = make_int(l / r);
                break;
            case TOKEN_MOD:   result = make_int(fmod(l, r)); break;

            case TOKEN_EQ:            result = make_int(l == r); break;
            case TOKEN_NOT_EQ:        result = make_int(l != r); break;
            case TOKEN_LESS:          result = make_int(l < r); break;
            case TOKEN_GREATER:       result = make_int(l > r); break;
            case TOKEN_LESS_EQUAL:    result = make_int(l <= r); break;
            case TOKEN_GREATER_EQUAL: result = make_int(l >= r); break;

            case TOKEN_AND: result = make_int(l && r); break;
            case TOKEN_OR:  result = make_int(l || r); break;
        }
        gc_pop_root(); gc_pop_root();
        return result;
    }

    gc_pop_root();
    gc_pop_root();
    return make_int(0);
}