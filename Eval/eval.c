#include "eval.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "../Lexer/lexer.h"
#include "../Parser/parser.h"



RuntimeValue make_int(const int val) {
    RuntimeValue v;
    v.type = VAL_INT; v.data.int_value = val; v.original_type = VAL_INT;
    return v;
}

RuntimeValue make_string(const char *str) {
    RuntimeValue v;
    v.type = VAL_STRING;
    v.data.string_value = str ? strdup(str) : strdup("");
    v.original_type = VAL_STRING;
    return v;
}

RuntimeValue make_array(RuntimeValue *elements, int count) {
    RuntimeValue v;
    v.type = VAL_ARRAY;
    v.data.array_data.elements = elements;
    v.data.array_data.count = count;
    v.original_type = VAL_ARRAY;
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
    if (!env) return;
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

void print_value(RuntimeValue val) {
    if (val.type == VAL_INT) printf("%d", val.data.int_value);
    else if (val.type == VAL_STRING) printf("%s", val.data.string_value ? val.data.string_value : "null");
    else if (val.type == VAL_ARRAY) {
        printf("[");
        for (int i = 0; i < val.data.array_data.count; i++) {
            print_value(val.data.array_data.elements[i]);
            if (i < val.data.array_data.count - 1) printf(", ");
        }
        printf("]");
    }
    else if (val.type == VAL_OBJECT) printf("<Module>");
    else if (val.type == VAL_FUNC) printf("<Function>");
}



RuntimeValue eval(const ASTNode *node, Environment *env) {
    if (node == NULL) return make_int(0);

    switch (node->type) {
        case AST_NUMBER: return make_int(node->data.number_value);
        case AST_STRING: return make_string(node->data.string_value);
        case AST_IDENTIFIER: return env_get_fast(env, node->data.identifier.name, node->data.identifier.id_hash);

        case AST_ARRAY: {
            int count = node->data.Array.item_count;
            RuntimeValue *elements = malloc(count * sizeof(RuntimeValue));
            for (int i = 0; i < count; i++) {
                elements[i] = eval(node->data.Array.items[i], env);
                if (elements[i].type == VAL_RETURN) elements[i].type = elements[i].original_type;
                if (elements[i].type == VAL_STRING) elements[i].data.string_value = strdup(elements[i].data.string_value);
            }
            return make_array(elements, count);
        }

        case AST_FUNC_DECL: {
            RuntimeValue func;
            func.type = VAL_FUNC;
            func.data.func.node = (ASTNode*)node;
            func.data.func.closure_env = env;
            env_set(env, node->data.FuncDecl.name, func);
            return func;
        }

        case AST_FUNC_CALL: {
            RuntimeValue val = env_get_fast(env, node->data.FuncCall.name, get_hash(node->data.FuncCall.name));

            if (val.type == VAL_NATIVE_FUNC) {
                int arg_count = node->data.FuncCall.arg_count;
                RuntimeValue *args = malloc(arg_count * sizeof(RuntimeValue));

                for (int i = 0; i < arg_count; i++) {
                    args[i] = eval(node->data.FuncCall.args[i], env);
                }
                RuntimeValue result = val.data.native_func(args, arg_count);
                free(args);
                return result;
            }
            if (val.type != VAL_FUNC) {
                printf("Runtime Error: '%s' is not a function\n", node->data.FuncCall.name);
                exit(1);
            }

            ASTNode *decl = val.data.func.node;
            Environment *local_env = malloc(sizeof(Environment));
            env_init(local_env);

            local_env->parent = val.data.func.closure_env;

            for (int i = 0; i < decl->data.FuncDecl.param_count; i++) {
                RuntimeValue arg_val = eval(node->data.FuncCall.args[i], env);
                env_set(local_env, decl->data.FuncDecl.params[i], arg_val);
            }

            RuntimeValue result = eval(decl->data.FuncDecl.body, local_env);
            if (result.type == VAL_RETURN) result.type = result.original_type;

            return result;
        }

        case AST_METHOD_CALL: {
            RuntimeValue target = eval(node->data.MethodCall.target, env);
            if (target.type == VAL_RETURN) target.type = target.original_type;
            char *method = node->data.MethodCall.method_name;

            if (target.type == VAL_OBJECT) {
                RuntimeValue member = env_get_fast(target.data.obj_env, method, get_hash(method));
                if (member.type == VAL_FUNC) {
                    ASTNode *decl = member.data.func.node;
                    Environment *local_env = malloc(sizeof(Environment));
                    env_init(local_env);
                    local_env->parent = target.data.obj_env;
                    for (int i = 0; i < decl->data.FuncDecl.param_count; i++) {
                        RuntimeValue arg_val = eval(node->data.MethodCall.args[i], env);
                        env_set(local_env, decl->data.FuncDecl.params[i], arg_val);
                    }
                    RuntimeValue result = eval(decl->data.FuncDecl.body, local_env);
                    return result;
                }
                return member;
            }

            if (target.type == VAL_ARRAY) {
                if (strcmp(method, "push") == 0) {
                    RuntimeValue val = eval(node->data.MethodCall.args[0], env);
                    int new_count = target.data.array_data.count + 1;
                    target.data.array_data.elements = realloc(target.data.array_data.elements, new_count * sizeof(RuntimeValue));

                    RuntimeValue copy = val;
                    if (val.type == VAL_STRING) copy.data.string_value = strdup(val.data.string_value);

                    target.data.array_data.elements[new_count - 1] = copy;
                    target.data.array_data.count = new_count;

                    ASTNode *base = node->data.MethodCall.target;
                    if (base->type == AST_IDENTIFIER) {
                        env_update_fast(env, base->data.identifier.name, base->data.identifier.id_hash, target);
                    }
                    return target;
                }
                if (strcmp(method, "len") == 0) return make_int(target.data.array_data.count);
            }

            if (target.type == VAL_STRING) {
                if (strcmp(method, "len") == 0) return make_int(strlen(target.data.string_value));
                if (strcmp(method, "upper") == 0) {
                    char *src = target.data.string_value;
                    char *res_str = malloc(strlen(src) + 1);
                    for (int i = 0; src[i]; i++) res_str[i] = (char)toupper(src[i]);
                    res_str[strlen(src)] = '\0';
                    RuntimeValue res = make_string(res_str); free(res_str);
                    return res;
                }
            }
            exit(1);
        }

        case AST_RETURN: {
            RuntimeValue val = eval(node->data.Return.value, env);
            RuntimeValue ret = val;
            ret.type = VAL_RETURN; ret.original_type = val.type;
            return ret;
        }

        case AST_BINOP: {
            const int op = node->data.binop.op;
            RuntimeValue left_val = eval(node->data.binop.left, env);
            if (left_val.type == VAL_RETURN) left_val.type = left_val.original_type;

            RuntimeValue right_val = eval(node->data.binop.right, env);
            if (right_val.type == VAL_RETURN) right_val.type = right_val.original_type;

            if (op == TOKEN_PLUS && (left_val.type == VAL_STRING || right_val.type == VAL_STRING)) {
                char buf_l[128], buf_r[128];
                char *s_l, *s_r;

                if (left_val.type == VAL_INT) { sprintf(buf_l, "%d", left_val.data.int_value); s_l = buf_l; }
                else if (left_val.type == VAL_STRING) { s_l = left_val.data.string_value; }
                else { s_l = "[Object]"; }

                if (right_val.type == VAL_INT) { sprintf(buf_r, "%d", right_val.data.int_value); s_r = buf_r; }
                else if (right_val.type == VAL_STRING) { s_r = right_val.data.string_value; }
                else { s_r = "[Object]"; }

                char *res = malloc(strlen(s_l) + strlen(s_r) + 1);
                strcpy(res, s_l); strcat(res, s_r);
                RuntimeValue str_val = make_string(res); free(res);
                return str_val;
            }

            if (left_val.type == VAL_INT && right_val.type == VAL_INT) {
                int l = left_val.data.int_value, r = right_val.data.int_value;
                switch (op) {
                    case TOKEN_PLUS:     return make_int(l + r);
                    case TOKEN_MINUS:    return make_int(l - r);
                    case TOKEN_STAR:     return make_int(l * r);
                    case TOKEN_SLASH:    if (r == 0) exit(1); return make_int(l / r);
                    case TOKEN_MOD:      return make_int(l % r);
                    case TOKEN_EQ:       return make_int(l == r);
                    case TOKEN_NOT_EQ:   return make_int(l != r);
                    case TOKEN_LESS:     return make_int(l < r);
                    case TOKEN_GREATER:  return make_int(l > r);
                    case TOKEN_LESS_EQUAL:    return make_int(l <= r);
                    case TOKEN_GREATER_EQUAL: return make_int(l >= r);
                    case TOKEN_AND:      return make_int(l && r);

                    case TOKEN_OR:       return make_int(l || r);
                }
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
            env_update_fast(env, node->data.Assign.name, node->data.Assign.id_hash, val);
            return val;
        }

        case AST_OUTPUT: {
            for (int i = 0; i < node->data.Output.arg_count; i++) {
                print_value(eval(node->data.Output.args[i], env));
                if (i < node->data.Output.arg_count - 1) printf(" ");
            }
            if (node->data.Output.is_newline) printf("\n");
            return make_int(0);
        }

        case AST_BLOCK: {
            RuntimeValue last = make_int(0);
            for (size_t i = 0; i < node->data.Block.count; i++) {
                last = eval(node->data.Block.items[i], env);
                if (last.type == VAL_RETURN) return last;
            }
            return last;
        }

        case AST_IF_BLOCK: {
            RuntimeValue cond = eval(node->data.IfBlck.condition, env);
            if (cond.data.int_value != 0) return eval(node->data.IfBlck.body, env);
            if (node->data.IfBlck.else_body) return eval(node->data.IfBlck.else_body, env);
            return make_int(0);
        }

        case AST_WHILE: {
            while (eval(node->data.While.condition, env).data.int_value) {
                RuntimeValue res = eval(node->data.While.body, env);
                if (res.type == VAL_RETURN) return res;
            }
            return make_int(0);
        }

        case AST_FOR: {
            Environment *local = malloc(sizeof(Environment));
            env_init(local); local->parent = env;

            if (node->data.For.init != NULL) {
                eval(node->data.For.init, local);
            }
            while (1) {
                if (node->data.For.condition != NULL) {
                    RuntimeValue cond = eval(node->data.For.condition, local);
                    if (cond.data.int_value == 0) break;
                }

                RuntimeValue res = eval(node->data.For.body, local);
                if (res.type == VAL_RETURN) return res;

                if (node->data.For.step != NULL) {
                    eval(node->data.For.step, local);
                }
            }
            return make_int(0);
        }

        case AST_INDEX: {
            RuntimeValue target = eval(node->data.Index.target, env);
            RuntimeValue idx = eval(node->data.Index.index, env);
            int i = idx.data.int_value;
            if (target.type == VAL_STRING) {
                char tmp[2] = { target.data.string_value[i], '\0' };
                return make_string(tmp);
            }
            if (target.type == VAL_ARRAY) return target.data.array_data.elements[i];
            exit(1);
        }

        case AST_USING: {
            char *path = node->data.Using.path;
            FILE *f = fopen(path, "rb");
            if (!f) exit(1);
            fseek(f, 0, SEEK_END); long len = ftell(f); fseek(f, 0, SEEK_SET);
            char *src = malloc(len + 1); fread(src, 1, len, f); src[len] = '\0'; fclose(f);

            Environment *mod_env = malloc(sizeof(Environment));
            env_init(mod_env);

            struct Lexer l; Token_init(&l, src);
            Parser p; parser_init(&p, &l);
            ASTNode *prog = parser_parse_program(&p);

            eval(prog, mod_env);

            RuntimeValue mod_obj;
            mod_obj.type = VAL_OBJECT;
            mod_obj.data.obj_env = mod_env;

            env_set(env, node->data.Using.alias, mod_obj);
            return make_int(0);
        }

        default: exit(1);
    }
}