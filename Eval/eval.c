#include "eval.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../Lexer/lexer.h"

RuntimeValue make_int(const int val) {
    RuntimeValue v;
    v.type = VAL_INT; v.data.int_value = val; v.original_type = VAL_INT;
    return v;
}

RuntimeValue make_string(const char *str) {
    RuntimeValue v;
    v.type = VAL_STRING;
    v.data.string_value = (char*)str;
    v.original_type = VAL_STRING;
    return v;
}

static inline unsigned int get_hash(const char *str) {
    unsigned int hash = 5381; int c;
    while ((c = *str++)) hash = ((hash << 5) + hash) + c;
    return hash;
}

void env_init(Environment *env) {
    for (size_t i = 0; i < TABLE_SIZE; i++) env->buckets[i] = NULL;
    env->parent = NULL;
}

void env_free(const Environment *env) {
    for (size_t i = 0; i < TABLE_SIZE; i++) {
        EnvVar *current = env->buckets[i];
        while (current != NULL) {
            EnvVar *next = current->next;
            free(current->name);
            if (current->value.type == VAL_STRING) free(current->value.data.string_value);
            free(current);
            current = next;
        }
    }
}

void env_set(Environment *env, const char *name, RuntimeValue value) {
    const unsigned int index = get_hash(name) % TABLE_SIZE;
    if (value.type == VAL_RETURN) value.type = value.original_type;

    EnvVar *current = env->buckets[index];
    while (current != NULL) {
        if (strcmp(current->name, name) == 0) {
            if (current->value.type == VAL_STRING) free(current->value.data.string_value);
            current->value = value;
            if (value.type == VAL_STRING) current->value.data.string_value = value.data.string_value ? strdup(value.data.string_value) : strdup("");
            return;
        }
        current = current->next;
    }

    EnvVar *new_var = malloc(sizeof(EnvVar));
    new_var->name = strdup(name);
    new_var->value = value;
    if (value.type == VAL_STRING) new_var->value.data.string_value = value.data.string_value ? strdup(value.data.string_value) : strdup("");
    new_var->next = env->buckets[index];
    env->buckets[index] = new_var;
}

RuntimeValue env_get_fast(const Environment *env, const char *name, const unsigned int h) {
    const unsigned int index = h % TABLE_SIZE;
    const EnvVar *current = env->buckets[index];
    while (current != NULL) {
        if (strcmp(current->name, name) == 0) return current->value;
        current = current->next;
    }
    if (env->parent != NULL) return env_get_fast(env->parent, name, h);
    printf("Runtime Error: Undefined variable '%s'\n", name);
    exit(1);
}

RuntimeValue env_get(const Environment *env, const char *name) {
    return env_get_fast(env, name, get_hash(name));
}

void env_update_fast(Environment *env, const char *name, const unsigned int h, RuntimeValue value) {
    const unsigned int index = h % TABLE_SIZE;
    EnvVar *current = env->buckets[index];

    while (current != NULL) {
        if (strcmp(current->name, name) == 0) {
            if (current->value.type == VAL_STRING) free(current->value.data.string_value);
            current->value = value;
            if (value.type == VAL_STRING) current->value.data.string_value = value.data.string_value ? strdup(value.data.string_value) : strdup("");
            return;
        }
        current = current->next;
    }
    if (env->parent != NULL) { env_update_fast(env->parent, name, h, value); return; }
    printf("Runtime Error: Variable '%s' is not defined!\n", name); exit(1);
}

RuntimeValue eval(const ASTNode *node, Environment *env) {
    if (node == NULL) return make_int(0);

    switch (node->type) {
        case AST_NUMBER: return make_int(node->data.number_value);
        case AST_STRING: return make_string(node->data.string_value);
        case AST_IDENTIFIER: return env_get_fast(env, node->data.identifier.name, node->data.identifier.id_hash);

        case AST_BLOCK: {
            RuntimeValue last_val = make_int(0);
            for (size_t i = 0; i < node->data.Block.count; i++) {
                last_val = eval(node->data.Block.items[i], env);
                if (last_val.type == VAL_RETURN) return last_val;
            }
            return last_val;
        }

        case AST_IF_BLOCK: {
            RuntimeValue cond = eval(node->data.IfBlck.condition, env);
            if (cond.type == VAL_RETURN) cond.type = cond.original_type;
            if (cond.type == VAL_INT && cond.data.int_value != 0) return eval(node->data.IfBlck.body, env);
            else if (node->data.IfBlck.else_body != NULL) return eval(node->data.IfBlck.else_body, env);
            return make_int(0);
        }

        case AST_RETURN: {
            RuntimeValue val = eval(node->data.Return.value, env);
            if (val.type == VAL_RETURN) val.type = val.original_type;
            RuntimeValue ret_signal; ret_signal.type = VAL_RETURN;
            ret_signal.data = val.data; ret_signal.original_type = val.type;
            return ret_signal;
        }

        case AST_FUNC_DECL: {
            RuntimeValue func_val; func_val.type = VAL_FUNC; func_val.data.func_node = (ASTNode*)node;
            env_set(env, node->data.FuncDecl.name, func_val);
            return make_int(0);
        }

        case AST_FUNC_CALL: {
            RuntimeValue func_val = env_get(env, node->data.FuncCall.name);
            ASTNode *decl = func_val.data.func_node;
            Environment local_env; env_init(&local_env); local_env.parent = env;

            for (int i = 0; i < decl->data.FuncDecl.param_count; i++) {
                RuntimeValue arg_val = eval(node->data.FuncCall.args[i], env);
                if (arg_val.type == VAL_RETURN) arg_val.type = arg_val.original_type;
                env_set(&local_env, decl->data.FuncDecl.params[i], arg_val);
            }
            RuntimeValue result = eval(decl->data.FuncDecl.body, &local_env);
            env_free(&local_env);
            if (result.type == VAL_RETURN) result.type = result.original_type;
            return result;
        }

        case AST_BINOP: {
            RuntimeValue left_val = eval(node->data.binop.left, env);
            RuntimeValue right_val = eval(node->data.binop.right, env);
            if (left_val.type == VAL_RETURN) left_val.type = left_val.original_type;
            if (right_val.type == VAL_RETURN) right_val.type = right_val.original_type;
            const int op = node->data.binop.op;

            if (left_val.type == VAL_INT && right_val.type == VAL_INT) {
                int l = left_val.data.int_value, r = right_val.data.int_value;
                if (op == TOKEN_PLUS)   return make_int(l + r);
                if (op == TOKEN_MINUS)  return make_int(l - r);
                if (op == TOKEN_STAR)   return make_int(l * r);
                if (op == TOKEN_SLASH) {
                    if (r == 0) { printf("Runtime Error: Division by zero\n"); exit(1); }
                    return make_int(l / r);
                }
                if (op == TOKEN_MOD) {
                    if (r == 0) { printf("Runtime Error: Modulo by zero\n"); exit(1); }
                    return make_int(l % r);
                }
                if (op == TOKEN_EQ)     return make_int(l == r);
                if (op == TOKEN_NOT_EQ) return make_int(l != r);
            }
            if (left_val.type == VAL_STRING && right_val.type == VAL_STRING) {
                if (op == TOKEN_EQ)     return make_int(strcmp(left_val.data.string_value, right_val.data.string_value) == 0);
                if (op == TOKEN_NOT_EQ) return make_int(strcmp(left_val.data.string_value, right_val.data.string_value) != 0);
            }
            if (op == TOKEN_EQ) return make_int(0);
            if (op == TOKEN_NOT_EQ) return make_int(1);
            printf("Runtime Error: Type mismatch\n"); exit(1);
        }

        case AST_WHILE: {
            while (1) {
                RuntimeValue cond = eval(node->data.While.condition, env);
                if (cond.type == VAL_RETURN) cond.type = cond.original_type;
                if (cond.data.int_value == 0) break;
                RuntimeValue res = eval(node->data.While.body, env);
                if (res.type == VAL_RETURN) return res;
            }
            return make_int(0);
        }

        case AST_VAR_DECL: {
            RuntimeValue val = eval(node->data.var_decl.value, env);
            if (val.type == VAL_RETURN) val.type = val.original_type;
            env_set(env, node->data.var_decl.name, val);
            return val;
        }

        case AST_ASSIGN: {
            RuntimeValue val = eval(node->data.Assign.value, env);
            if (val.type == VAL_RETURN) val.type = val.original_type;
            env_update_fast(env, node->data.Assign.name, node->data.Assign.id_hash, val);
            return val;
        }

        case AST_OUTPUT: {
            for (int i = 0; i < node->data.Output.arg_count; i++) {
                RuntimeValue val = eval(node->data.Output.args[i], env);
                if (val.type == VAL_RETURN) val.type = val.original_type;

                if (val.type == VAL_INT) printf("%d", val.data.int_value);
                else if (val.type == VAL_STRING) printf("%s", val.data.string_value ? val.data.string_value : "null");
                if (i < node->data.Output.arg_count - 1) printf(" ");
            }
            if (node->data.Output.is_newline) printf("\n");
            return make_int(0);
        }

        default: printf("Runtime Error: Unknown node\n"); exit(1);
    }
    return make_int(0);
}