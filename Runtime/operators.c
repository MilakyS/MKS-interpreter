#include "operators.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "../Eval/eval.h"
#include "../Lexer/lexer.h"
#include "../GC/gc.h"

static inline RuntimeValue unwrap(RuntimeValue v) {
    if (v.type == VAL_RETURN) {
        v.type = v.original_type;
    }
    return v;
}

static RuntimeValue make_bool(int value) {
    return make_int(value ? 1 : 0);
}

RuntimeValue eval_binop(const ASTNode *node, Environment *env) {
    const int op = node->data.binop.op;

    RuntimeValue left_val = unwrap(eval(node->data.binop.left, env));
    gc_push_root(&left_val);

    if (op == TOKEN_AND) {
        if (left_val.type != VAL_INT || left_val.data.float_value == 0.0) {
            gc_pop_root();
            return make_bool(0);
        }
    } else if (op == TOKEN_OR) {
        if (left_val.type == VAL_INT && left_val.data.float_value != 0.0) {
            gc_pop_root();
            return make_bool(1);
        }
    }

    RuntimeValue right_val = unwrap(eval(node->data.binop.right, env));
    gc_push_root(&right_val);

    if (left_val.type == VAL_STRING && right_val.type == VAL_STRING) {
        const char *s_l = left_val.data.managed_string->data;
        const char *s_r = right_val.data.managed_string->data;
        RuntimeValue result = make_int(0);

        if (op == TOKEN_EQ) {
            result = make_bool(strcmp(s_l, s_r) == 0);
            gc_pop_root();
            gc_pop_root();
            return result;
        }

        if (op == TOKEN_NOT_EQ) {
            result = make_bool(strcmp(s_l, s_r) != 0);
            gc_pop_root();
            gc_pop_root();
            return result;
        }
    }

    if (op == TOKEN_PLUS && (left_val.type == VAL_STRING || right_val.type == VAL_STRING)) {
        char buf_l[128];
        char buf_r[128];
        const char *s_l = NULL;
        const char *s_r = NULL;
        size_t len_l, len_r;

        if (left_val.type == VAL_INT) {
            snprintf(buf_l, sizeof(buf_l), "%g", left_val.data.float_value);
            s_l = buf_l;
            len_l = strlen(s_l);
        } else if (left_val.type == VAL_STRING) {
            s_l = left_val.data.managed_string->data;
            len_l = left_val.data.managed_string->len;
        } else {
            s_l = "[Object]";
            len_l = strlen(s_l);
        }

        if (right_val.type == VAL_INT) {
            snprintf(buf_r, sizeof(buf_r), "%g", right_val.data.float_value);
            s_r = buf_r;
            len_r = strlen(s_r);
        } else if (right_val.type == VAL_STRING) {
            s_r = right_val.data.managed_string->data;
            len_r = right_val.data.managed_string->len;
        } else {
            s_r = "[Object]";
            len_r = strlen(s_r);
        }

        char *res_str = (char *)malloc(len_l + len_r + 1);
        if (res_str == NULL) {
            gc_pop_root();
            gc_pop_root();
            fprintf(stderr, "Runtime Error: Out of memory in string concatenation\n");
            exit(1);
        }

        memcpy(res_str, s_l, len_l);
        memcpy(res_str + len_l, s_r, len_r);
        res_str[len_l + len_r] = '\0';

        RuntimeValue out = make_string_len(res_str, len_l + len_r);
        free(res_str);

        gc_pop_root();
        gc_pop_root();
        return out;
    }

    if (left_val.type == VAL_INT && right_val.type == VAL_INT) {
        const double l = left_val.data.float_value;
        const double r = right_val.data.float_value;
        RuntimeValue result = make_int(0);

        switch (op) {
            case TOKEN_PLUS:
                result = make_int(l + r);
                break;

            case TOKEN_MINUS:
                result = make_int(l - r);
                break;

            case TOKEN_STAR:
                result = make_int(l * r);
                break;

            case TOKEN_SLASH:
                if (r == 0.0) {
                    gc_pop_root();
                    gc_pop_root();
                    fprintf(stderr, "Runtime Error: division by zero\n");
                    exit(1);
                }
                result = make_int(l / r);
                break;

            case TOKEN_MOD:
                if (r == 0.0) {
                    gc_pop_root();
                    gc_pop_root();
                    fprintf(stderr, "Runtime Error: modulo by zero\n");
                    exit(1);
                }
                result = make_int(fmod(l, r));
                break;

            case TOKEN_EQ:
                result = make_bool(l == r);
                break;

            case TOKEN_NOT_EQ:
                result = make_bool(l != r);
                break;

            case TOKEN_LESS:
                result = make_bool(l < r);
                break;

            case TOKEN_GREATER:
                result = make_bool(l > r);
                break;

            case TOKEN_LESS_EQUAL:
                result = make_bool(l <= r);
                break;

            case TOKEN_GREATER_EQUAL:
                result = make_bool(l >= r);
                break;

            case TOKEN_AND:
                result = make_bool(l != 0.0 && r != 0.0);
                break;

            case TOKEN_OR:
                result = make_bool(l != 0.0 || r != 0.0);
                break;

            default:
                result = make_int(0);
                break;
        }

        gc_pop_root();
        gc_pop_root();
        return result;
    }

    gc_pop_root();
    gc_pop_root();
    return make_int(0);
}