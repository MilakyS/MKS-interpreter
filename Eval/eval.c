#include "eval.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../Lexer/lexer.h"

RuntimeValue make_int(const int val) {
    RuntimeValue v;
    v.type = VAL_INT;
    v.data.int_value = val;
    return v;
}

static inline unsigned int get_hash(const char *str) {
    unsigned int hash = 5381;
    int c;
    while ((c = *str++))
        hash = ((hash << 5) + hash) + c;
    return hash;
}

RuntimeValue make_string(const char *str) {
    RuntimeValue v;
    v.type = VAL_STRING;
    v.data.string_value = strdup(str);
    return v;
}

void env_init(Environment *env) {
    for (size_t i = 0; i < TABLE_SIZE; i++) {
        env->buckets[i] = NULL;
    }
}


RuntimeValue env_get_fast(const Environment *env, const char *name, const unsigned int h) {
    const unsigned int index = h % TABLE_SIZE;
    const EnvVar *current = env->buckets[index];
    while (current != NULL) {
        if (strcmp(current->name, name) == 0) return current->value;
        current = current->next;
    }
    exit(1);
}

void env_set(Environment *env, const char *name, const RuntimeValue value) {
    const unsigned int index = get_hash(name) % TABLE_SIZE;
    EnvVar *current = env->buckets[index];

    while (current != NULL) {
        if (strcmp(current->name, name) == 0) {
            current->value = value;
            return;
        }
        current = current->next;
    }

    EnvVar *new_var = malloc(sizeof(EnvVar));
    new_var->name = strdup(name);
    new_var->value = value;
    new_var->next = env->buckets[index];
    env->buckets[index] = new_var;
}
void env_update_fast(const Environment *env, const char *name, const unsigned int h, const RuntimeValue value) {
    const unsigned int index = h % TABLE_SIZE;
    EnvVar *current = env->buckets[index];

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

    printf("Runtime Error: Variable '%s' is not defined!\n", name);
    exit(1);
}

RuntimeValue env_get(const Environment *env, const char *name) {
    const unsigned int index = get_hash(name) % TABLE_SIZE;
    const EnvVar *current = env->buckets[index];

    while (current != NULL) {
        if (strcmp(current->name, name) == 0) {
            return current->value;
        }
        current = current->next;
    }
    printf("Runtime Error: Undefined variable '%s'\n", name);
    exit(1);
}

void env_update(const Environment *env, const char *name, const RuntimeValue value) {
    const unsigned int index = get_hash(name) % TABLE_SIZE;
    EnvVar *current = env->buckets[index];

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

    printf("Runtime Error: Variable '%s' is not defined! Use 'var' to declare it. Sigma rules!\n", name);
    exit(1);
}

RuntimeValue eval(const ASTNode *node, Environment *env) {
    if (node == NULL) return make_int(0);

    switch (node->type) {
        case AST_NUMBER:
            return make_int(node->data.number_value);

        case AST_STRING:
            return make_string(node->data.string_value);

        case AST_IDENTIFIER:
            return env_get_fast(env, node->data.identifier.name, node->data.var_decl.id_hash);
        case AST_BLOCK: {
            RuntimeValue last_val = make_int(0);

            for (size_t i = 0; i < node->data.Block.count; i++) {
                last_val = eval(node->data.Block.items[i], env);
            }
            return last_val;
        }
        case AST_IF_BLOCK: {
            const RuntimeValue cond = eval(node->data.IfBlck.condition, env);

            if (cond.type == VAL_INT && cond.data.int_value != 0) {
                return eval(node->data.IfBlck.body, env);
            }

            else if (node->data.IfBlck.else_body != NULL) {
                return eval(node->data.IfBlck.else_body, env);
            }

            return make_int(0);
        }
        case AST_BINOP: {
            const RuntimeValue left_val = eval(node->data.binop.left, env);
            const RuntimeValue right_val = eval(node->data.binop.right, env);
            const int op = node->data.binop.op;

            if (left_val.type == VAL_INT && right_val.type == VAL_INT) {
                if (op == TOKEN_PLUS) return make_int(left_val.data.int_value + right_val.data.int_value);
                if (op == TOKEN_MINUS) return make_int(left_val.data.int_value - right_val.data.int_value);
                if (op == TOKEN_EQ) return make_int(left_val.data.int_value == right_val.data.int_value);
                if (op == TOKEN_NOT_EQ) return make_int(left_val.data.int_value != right_val.data.int_value);
            }

            if (left_val.type == VAL_STRING && right_val.type == VAL_STRING) {
                if (op == TOKEN_EQ) {
                    return make_int(strcmp(left_val.data.string_value, right_val.data.string_value) == 0);
                }
                if (op == TOKEN_NOT_EQ) {
                    return make_int(strcmp(left_val.data.string_value, right_val.data.string_value) != 0);
                }
                if (op == TOKEN_MINUS || op == TOKEN_PLUS) {
                    printf("Runtime Error: Can't use +/- on strings yet!\n");
                    exit(1);
                }
            }

            if (left_val.type != right_val.type) {
                if (op == TOKEN_EQ) return make_int(0);      // Они точно не равны
                if (op == TOKEN_NOT_EQ) return make_int(1);  // Они точно разные
            }

            printf("Runtime Error: Unknown operation or type mismatch\n");
            exit(1);
        }

        case AST_WHILE: {
           while (eval(node->data.While.condition, env).data.int_value != 0) {
               eval(node->data.While.body, env);
           }
            return make_int(0);
        }

        case AST_VAR_DECL: {
            const RuntimeValue val = eval(node->data.var_decl.value, env);
            env_set(env, node->data.var_decl.name, val);
            return val;
        }

        case AST_ASSIGN: {
            const RuntimeValue val = eval(node->data.Assign.value, env);
            env_update_fast(env, node->data.Assign.name, node->data.Assign.id_hash, val);
            return val;
        }

        case AST_WRITELN: {
            const RuntimeValue val = eval(node->data.Writeln.expression, env);

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