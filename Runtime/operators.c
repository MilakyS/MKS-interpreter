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

int runtime_value_equals(RuntimeValue left, RuntimeValue right) {
    if (left.type == VAL_RETURN) {
        left.type = left.original_type;
    }
    if (right.type == VAL_RETURN) {
        right.type = right.original_type;
    }

    if (left.type == VAL_NULL || right.type == VAL_NULL) {
        return left.type == VAL_NULL && right.type == VAL_NULL;
    }

    if (left.type == VAL_BOOL && right.type == VAL_BOOL) {
        return left.data.bool_value == right.data.bool_value;
    }

    if (left.type == VAL_STRING && right.type == VAL_STRING) {
        const char *s_l = left.data.managed_string != NULL ? left.data.managed_string->data : "";
        const char *s_r = right.data.managed_string != NULL ? right.data.managed_string->data : "";
        return strcmp(s_l, s_r) == 0;
    }

    if (runtime_value_is_number(left) && runtime_value_is_number(right)) {
        if (left.type == VAL_INT && right.type == VAL_INT) {
            return runtime_value_as_int(left) == runtime_value_as_int(right);
        }
        return runtime_value_as_double(left) == runtime_value_as_double(right);
    }

    if (left.type != right.type) {
        return 0;
    }

    switch (left.type) {
        case VAL_ARRAY:
            return left.data.managed_array == right.data.managed_array;
        case VAL_POINTER:
            return left.data.managed_pointer == right.data.managed_pointer;
        case VAL_OBJECT:
        case VAL_MODULE:
            return left.data.obj_env == right.data.obj_env;
        case VAL_FUNC:
            return left.data.func.node == right.data.func.node &&
                   left.data.func.closure_env == right.data.func.closure_env;
        case VAL_NATIVE_FUNC:
            return left.data.native.fn == right.data.native.fn &&
                   left.data.native.ctx == right.data.native.ctx;
        case VAL_BLUEPRINT:
            return left.data.blueprint.entity_node == right.data.blueprint.entity_node &&
                   left.data.blueprint.closure_env == right.data.blueprint.closure_env;
        default:
            return 0;
    }
}

int runtime_value_is_truthy(RuntimeValue v) {
    switch (v.type) {
        case VAL_NULL:
            return 0;

        case VAL_INT:
            return v.data.int_value != 0;

        case VAL_BOOL:
            return v.data.bool_value ? 1 : 0;

        case VAL_FLOAT:
            return v.data.float_value != 0.0;

        case VAL_STRING:
            return v.data.managed_string != NULL &&
                   v.data.managed_string->len > 0;

        case VAL_ARRAY:
            return v.data.managed_array != NULL &&
                   v.data.managed_array->count > 0;

        case VAL_FUNC:
        case VAL_NATIVE_FUNC:
        case VAL_POINTER:
        case VAL_OBJECT:
        case VAL_MODULE:
            return 1;

        case VAL_RETURN:
            return runtime_value_is_truthy(unwrap(v));

        default:
            return 0;
    }
}

static const char *value_type_name(const RuntimeValue v) {
    switch (v.type) {
        case VAL_INT:         return "int";
        case VAL_FLOAT:       return "float";
        case VAL_BOOL:        return "bool";
        case VAL_STRING:      return "string";
        case VAL_ARRAY:       return "array";
        case VAL_POINTER:     return "pointer";
        case VAL_FUNC:        return "function";
        case VAL_NATIVE_FUNC: return "native_function";
        case VAL_RETURN:      return "return";
        case VAL_OBJECT:      return "object";
        case VAL_MODULE:      return "module";
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
            snprintf(buf, buf_size, "%lld", (long long)v.data.int_value);
            return buf;

        case VAL_FLOAT:
            snprintf(buf, buf_size, "%g", v.data.float_value);
            return buf;

        case VAL_BOOL:
            return v.data.bool_value ? "true" : "false";

        case VAL_STRING:
            if (v.data.managed_string && v.data.managed_string->data) {
                return v.data.managed_string->data;
            }
            return "";

        case VAL_NULL:
            return "null";

        case VAL_ARRAY:
            return "[Array]";

        case VAL_POINTER:
            return "[Pointer]";

        case VAL_OBJECT:
            return "[Object]";

        case VAL_MODULE:
            return "[Module]";

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

    /* Быстрый путь: одна из строк пустая */
    if (len_l == 0 && left_val.type == VAL_STRING) {
        return right_val.type == VAL_STRING ? right_val : make_string_len(s_r, len_r);
    }
    if (len_r == 0 && right_val.type == VAL_STRING) {
        return left_val.type == VAL_STRING ? left_val : make_string_len(s_l, len_l);
    }

    char *res_str = (char *)malloc(len_l + len_r + 1);
    if (res_str == NULL) {
        runtime_error("Out of memory in string concatenation");
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
    const int left_rooted = gc_push_root_if_needed(&left_val);

    if (op == TOKEN_AND) {
        if (!runtime_value_is_truthy(left_val)) {
            result = make_bool(false);
            if (left_rooted) {
                gc_pop_root();
            }
            return result;
        }
    } else if (op == TOKEN_OR) {
        if (runtime_value_is_truthy(left_val)) {
            result = make_bool(true);
            if (left_rooted) {
                gc_pop_root();
            }
            return result;
        }
    }

    RuntimeValue right_val = unwrap(eval(node->data.binop.right, env));
    const int right_rooted = gc_push_root_if_needed(&right_val);

    if (left_val.type == VAL_STRING && right_val.type == VAL_STRING) {
        const char *s_l = left_val.data.managed_string->data;
        const char *s_r = right_val.data.managed_string->data;

        if (op == TOKEN_EQ) {
            result = make_bool(strcmp(s_l, s_r) == 0);
            if (right_rooted) gc_pop_root();
            if (left_rooted) gc_pop_root();
            return result;
        }

        if (op == TOKEN_NOT_EQ) {
            result = make_bool(strcmp(s_l, s_r) != 0);
            if (right_rooted) gc_pop_root();
            if (left_rooted) gc_pop_root();
            return result;
        }
    }

    if (left_val.type == VAL_NULL || right_val.type == VAL_NULL) {
        if (op == TOKEN_EQ) {
            result = make_bool(runtime_value_equals(left_val, right_val));
            if (right_rooted) gc_pop_root();
            if (left_rooted) gc_pop_root();
            return result;
        }

        if (op == TOKEN_NOT_EQ) {
            result = make_bool(!runtime_value_equals(left_val, right_val));
            if (right_rooted) gc_pop_root();
            if (left_rooted) gc_pop_root();
            return result;
        }
    }

    if (op == TOKEN_PLUS && (left_val.type == VAL_STRING || right_val.type == VAL_STRING)) {
        result = concat_values_as_string(left_val, right_val);
        if (right_rooted) gc_pop_root();
        if (left_rooted) gc_pop_root();
        return result;
    }

    if (op == TOKEN_AND || op == TOKEN_OR) {
        result = make_bool(
            (op == TOKEN_AND)
                ? (runtime_value_is_truthy(left_val) && runtime_value_is_truthy(right_val))
                : (runtime_value_is_truthy(left_val) || runtime_value_is_truthy(right_val))
        );

        if (right_rooted) gc_pop_root();
        if (left_rooted) gc_pop_root();
        return result;
    }

    if (op == TOKEN_EQ || op == TOKEN_NOT_EQ) {
        const int equal = runtime_value_equals(left_val, right_val);
        result = make_bool(op == TOKEN_EQ ? equal : !equal);
        if (right_rooted) gc_pop_root();
        if (left_rooted) gc_pop_root();
        return result;
    }

    if (runtime_value_is_number(left_val) && runtime_value_is_number(right_val)) {
        const int both_int = left_val.type == VAL_INT && right_val.type == VAL_INT;
        const int64_t li = runtime_value_as_int(left_val);
        const int64_t ri = runtime_value_as_int(right_val);
        const double l = runtime_value_as_double(left_val);
        const double r = runtime_value_as_double(right_val);

        switch (op) {
            case TOKEN_PLUS:
                result = both_int ? make_int(li + ri) : make_float(l + r);
                break;

            case TOKEN_MINUS:
                result = both_int ? make_int(li - ri) : make_float(l - r);
                break;

            case TOKEN_STAR:
                result = both_int ? make_int(li * ri) : make_float(l * r);
                break;

            case TOKEN_SLASH:
                if (r == 0.0) {
                    if (right_rooted) gc_pop_root();
                    if (left_rooted) gc_pop_root();
                    runtime_error("division by zero");
                }
                result = make_float(l / r);
                break;

            case TOKEN_MOD:
                if (r == 0.0) {
                    if (right_rooted) gc_pop_root();
                    if (left_rooted) gc_pop_root();
                    runtime_error("modulo by zero");
                }
                result = both_int ? make_int(li % ri) : make_float(fmod(l, r));
                break;

            case TOKEN_EQ:
                result = make_bool(runtime_value_equals(left_val, right_val));
                break;

            case TOKEN_NOT_EQ:
                result = make_bool(!runtime_value_equals(left_val, right_val));
                break;

            case TOKEN_LESS:
                result = make_bool(both_int ? (li < ri) : (l < r));
                break;

            case TOKEN_GREATER:
                result = make_bool(both_int ? (li > ri) : (l > r));
                break;

            case TOKEN_LESS_EQUAL:
                result = make_bool(both_int ? (li <= ri) : (l <= r));
                break;

            case TOKEN_GREATER_EQUAL:
                result = make_bool(both_int ? (li >= ri) : (l >= r));
                break;

            default:
                if (right_rooted) gc_pop_root();
                if (left_rooted) gc_pop_root();
                runtime_type_error_binop(op, left_val, right_val);
                return make_null();
        }

        if (right_rooted) gc_pop_root();
        if (left_rooted) gc_pop_root();
        return result;
    }
    if (right_rooted) gc_pop_root();
    if (left_rooted) gc_pop_root();
    runtime_type_error_binop(op, left_val, right_val);
    return make_null();
}
