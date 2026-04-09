#include "operators.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "../Eval/eval.h"
#include "../Lexer/lexer.h"
#include "../GC/gc.h"
#include "errors.h"

#define MKS_NUM_BUF_SIZE 128

static inline RuntimeValue unwrap(RuntimeValue v) {
    if (v.type == VAL_RETURN) {
        v.type = v.original_type;
    }
    return v;
}

static RuntimeValue make_bool(const int value) {
    return make_int(value ? 1 : 0);
}

static int is_truthy(const RuntimeValue v) {
    switch (v.type) {
        case VAL_NULL:
            return 0;

        case VAL_INT:
            return v.data.float_value != 0.0;

        case VAL_STRING:
            return v.data.managed_string != NULL &&
                   v.data.managed_string->len > 0;

        case VAL_ARRAY:
            return v.data.managed_array != NULL &&
                   v.data.managed_array->count > 0;

        case VAL_FUNC:
        case VAL_NATIVE_FUNC:
        case VAL_OBJECT:
            return 1;

        case VAL_RETURN:
            return is_truthy(unwrap(v));

        default:
            return 0;
    }
}

static const char *value_type_name(const RuntimeValue v) {
    switch (v.type) {
        case VAL_INT:         return "number";
        case VAL_STRING:      return "string";
        case VAL_ARRAY:       return "array";
        case VAL_FUNC:        return "function";
        case VAL_NATIVE_FUNC: return "native_function";
        case VAL_RETURN:      return "return";
        case VAL_OBJECT:      return "object";
        case VAL_NULL:        return "null";
        default:              return "unknown";
    }
}

static const char *token_name(const int op) {
    switch (op) {
        case TOKEN_PLUS:          return "+";
        case TOKEN_MINUS:         return "-";
        case TOKEN_STAR:          return "*";
        case TOKEN_SLASH:         return "/";
        case TOKEN_MOD:           return "%";
        case TOKEN_EQ:            return "==";
        case TOKEN_NOT_EQ:        return "!=";
        case TOKEN_LESS:          return "<";
        case TOKEN_GREATER:       return ">";
        case TOKEN_LESS_EQUAL:    return "<=";
        case TOKEN_GREATER_EQUAL: return ">=";
        case TOKEN_AND:           return "&&";
        case TOKEN_OR:            return "||";
        default:                  return "<unknown_op>";
    }
}

static void runtime_type_error_binop(const int op,
                                     const RuntimeValue left,
                                     const RuntimeValue right) {
    runtime_error("unsupported operand types for '%s': %s and %s",
                  token_name(op),
                  value_type_name(left),
                  value_type_name(right));
}

static const char *value_to_cstr(const RuntimeValue v, char *buf, const size_t buf_size) {
    switch (v.type) {
        case VAL_INT:
            snprintf(buf, buf_size, "%g", v.data.float_value);
            return buf;

        case VAL_STRING:
            if (v.data.managed_string && v.data.managed_string->data) {
                return v.data.managed_string->data;
            }
            return "";

        case VAL_NULL:
            return "null";

        case VAL_ARRAY:
            return "[Array]";

        case VAL_OBJECT:
            return "[Object]";

        case VAL_FUNC:
            return "[Function]";

        case VAL_NATIVE_FUNC:
            return "[NativeFunction]";

        case VAL_RETURN:
            return value_to_cstr(unwrap(v), buf, buf_size);

        default:
            return "[Unknown]";
    }
}

static size_t value_string_length(const RuntimeValue v, const char *fallback_cstr) {
    if (v.type == VAL_STRING &&
        v.data.managed_string != NULL) {
        return v.data.managed_string->len;
    }

    return strlen(fallback_cstr);
}

static RuntimeValue concat_values_as_string(const RuntimeValue left_val,
                                            const RuntimeValue right_val) {
    char buf_l[MKS_NUM_BUF_SIZE];
    char buf_r[MKS_NUM_BUF_SIZE];

    const char *s_l = value_to_cstr(left_val, buf_l, sizeof(buf_l));
    const char *s_r = value_to_cstr(right_val, buf_r, sizeof(buf_r));

    const size_t len_l = value_string_length(left_val, s_l);
    const size_t len_r = value_string_length(right_val, s_r);

    char *res_str = (char *)malloc(len_l + len_r + 1);
    if (res_str == NULL) {
        fprintf(stderr, "Runtime Error: Out of memory in string concatenation\n");
        exit(1);
    }

    memcpy(res_str, s_l, len_l);
    memcpy(res_str + len_l, s_r, len_r);
    res_str[len_l + len_r] = '\0';

    return make_string_owned(res_str, len_l + len_r);
}

RuntimeValue eval_binop(const ASTNode *node, Environment *env) {
    const int op = node->data.binop.op;
    RuntimeValue result = make_null();

    RuntimeValue left_val = unwrap(eval(node->data.binop.left, env));
    gc_push_root(&left_val);

    if (op == TOKEN_AND) {
        if (!is_truthy(left_val)) {
            result = make_bool(0);
            gc_pop_root();
            return result;
        }
    } else if (op == TOKEN_OR) {
        if (is_truthy(left_val)) {
            result = make_bool(1);
            gc_pop_root();
            return result;
        }
    }

    RuntimeValue right_val = unwrap(eval(node->data.binop.right, env));
    gc_push_root(&right_val);

    if (left_val.type == VAL_STRING && right_val.type == VAL_STRING) {
        const char *s_l = left_val.data.managed_string->data;
        const char *s_r = right_val.data.managed_string->data;

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
        result = concat_values_as_string(left_val, right_val);
        gc_pop_root();
        gc_pop_root();
        return result;
    }

    if (op == TOKEN_AND || op == TOKEN_OR) {
        result = make_bool(
            (op == TOKEN_AND)
                ? (is_truthy(left_val) && is_truthy(right_val))
                : (is_truthy(left_val) || is_truthy(right_val))
        );

        gc_pop_root();
        gc_pop_root();
        return result;
    }

    if (left_val.type == VAL_INT && right_val.type == VAL_INT) {
        const double l = left_val.data.float_value;
        const double r = right_val.data.float_value;

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

            default:
                gc_pop_root();
                gc_pop_root();
                runtime_type_error_binop(op, left_val, right_val);
                return make_null();
        }

        gc_pop_root();
        gc_pop_root();
        return result;
    }
    gc_pop_root();
    gc_pop_root();
    runtime_type_error_binop(op, left_val, right_val);
    return make_null();
}
