#include "eval.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../Lexer/lexer.h"

RuntimeValue make_int(int val) {
    RuntimeValue v;
    v.type = VAL_INT;
    v.data.int_value = val;
    return v;
}

RuntimeValue make_string(const char *str) {
    RuntimeValue v;
    v.type = VAL_STRING;
    v.data.string_value = strdup(str);
    return v;
}

void env_init(Environment *env) {
    env->head = NULL;
}

void env_set(Environment *env, const char *name, RuntimeValue value) {
    EnvVar *current = env->head;
    while (current != NULL) {
        if (strcmp(current->name, name) == 0) {
            if (current->value.type == VAL_STRING) {
                free(current->value.data.string_value);
            }
            current->value = value;
            return;
        }
        current = current->next;
    }

    EnvVar *new_var = malloc(sizeof(EnvVar));
    new_var->name = strdup(name);
    new_var->value = value;
    new_var->next = env->head;
    env->head = new_var;
}

RuntimeValue env_get(Environment *env, const char *name) {
    EnvVar *current = env->head;
    while (current != NULL) {
        if (strcmp(current->name, name) == 0) {
            return current->value;
        }
        current = current->next;
    }
    printf("Runtime Error: Undefined variable '%s'\n", name);
    exit(1);
}

RuntimeValue eval(ASTNode *node, Environment *env) {
    if (node == NULL) return make_int(0);

    switch (node->type) {
        case AST_NUMBER:
            return make_int(node->data.number_value);

        case AST_STRING:
            return make_string(node->data.string_value);

        case AST_IDENTIFIER:
            return env_get(env, node->data.identifier_name);
        case AST_BLOCK: {
            RuntimeValue last_val = make_int(0);

            for (int i = 0; i < node->data.Block.count; i++) {
                last_val = eval(node->data.Block.items[i], env);
            }
            return last_val;
        }
        case AST_IF_BLOCK: {
            RuntimeValue cond = eval(node->data.IfBlck.condition, env);

            if (cond.type == VAL_INT && cond.data.int_value != 0) {
                return eval(node->data.IfBlck.body, env);
            }

            else if (node->data.IfBlck.else_body != NULL) {
                return eval(node->data.IfBlck.else_body, env);
            }

            return make_int(0);
        }




        case AST_BINOP: {
            RuntimeValue left_val = eval(node->data.binop.left, env);
            RuntimeValue right_val = eval(node->data.binop.right, env);

            if (left_val.type != VAL_INT || right_val.type != VAL_INT) {
                printf("Runtime Error: Math operations only support numbers for now!\n");
                exit(1);
            }

            int op = node->data.binop.op;
            if (op == TOKEN_PLUS) return make_int(left_val.data.int_value + right_val.data.int_value);
            if (op == TOKEN_MINUS) return make_int(left_val.data.int_value - right_val.data.int_value);
            if (op == TOKEN_EQ) {
                return make_int(left_val.data.int_value == right_val.data.int_value);
            }
            if (op == TOKEN_NOT_EQ) {
                return make_int(left_val.data.int_value != right_val.data.int_value);
            }

            printf("Runtime Error: Unknown operator\n");
            exit(1);
        }

        case AST_WHILE: {
           while (eval(node->data.While.condition, env).data.int_value != 0) {
               eval(node->data.While.body, env);
           }
            return make_int(0);
        }

        case AST_VAR_DECL: {
            RuntimeValue val = eval(node->data.var_decl.value, env);
            env_set(env, node->data.var_decl.name, val);
            return val;
        }

        case AST_ASSIGN: {
            RuntimeValue val = eval(node->data.Assign.value, env);
            env_set(env, node->data.Assign.name, val);
            return val;
        }

        case AST_WRITELN: {
            RuntimeValue val = eval(node->data.Writeln.expression, env);

            if (val.type == VAL_INT) {
                printf("%d\n", val.data.int_value);
            } else if (val.type == VAL_STRING) {
                printf("%s\n", val.data.string_value);
            }

            return val;
        }

        default:
            printf("Runtime Error: Unknown AST node type %d\n", node->type);
            exit(1);
    }
    return make_int(0);
}