#include "eval.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "../Lexer/lexer.h"
#include "../Parser/parser.h"
#include "../Runtime/value.h"
#include "../env/env.h"
#include "../Utils/hash.h"


#include "../Runtime/output.h"
#include "../Runtime/operators.h"

RuntimeValue eval(const ASTNode *node, Environment *env) {
    if (node == NULL) return make_int(0);

    switch (node->type) {
        case AST_NUMBER: return make_int(node->data.number_value);
        case AST_STRING: return make_string(node->data.string_value);

        case AST_IDENTIFIER:
            return env_get_fast(env, node->data.identifier.name, node->data.identifier.id_hash);

        case AST_ARRAY: {
            int count = node->data.Array.item_count;
            RuntimeValue *elements = malloc(count * sizeof(RuntimeValue));
            for (int i = 0; i < count; i++) {
                elements[i] = eval(node->data.Array.items[i], env);
                if (elements[i].type == VAL_RETURN) elements[i].type = elements[i].original_type;
                if (elements[i].type == VAL_STRING)
                    elements[i].data.string_value = strdup(elements[i].data.string_value);
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
            Environment *local_env = env_create_child(val.data.func.closure_env);

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
                    Environment *local_env = env_create_child(target.data.obj_env);
                    for (int i = 0; i < decl->data.FuncDecl.param_count; i++) {
                        RuntimeValue arg_val = eval(node->data.MethodCall.args[i], env);
                        env_set(local_env, decl->data.FuncDecl.params[i], arg_val);
                    }
                    return eval(decl->data.FuncDecl.body, local_env);
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
                    RuntimeValue res = make_string(res_str);
                    free(res_str);
                    return res;
                }
            }
            exit(1);
        }

        case AST_RETURN: {
            RuntimeValue val = eval(node->data.Return.value, env);
            RuntimeValue ret = val;
            ret.type = VAL_RETURN;
            ret.original_type = val.type;
            return ret;
        }

        case AST_BINOP: {
            return eval_binop(node, env);
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
            return eval_output(node, env);
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
            if (cond.data.float_value != 0.0)
                return eval(node->data.IfBlck.body, env);
            if (node->data.IfBlck.else_body)
                return eval(node->data.IfBlck.else_body, env);
            return make_int(0);
        }

        case AST_WHILE: {
            while (eval(node->data.While.condition, env).data.float_value != 0.0) {
                RuntimeValue res = eval(node->data.While.body, env);
                if (res.type == VAL_RETURN) return res;
            }
            return make_int(0);
        }

        case AST_FOR: {
            Environment *local = env_create_child(env);
            if (node->data.For.init != NULL) eval(node->data.For.init, local);
            while (1) {
                if (node->data.For.condition != NULL) {
                    RuntimeValue cond = eval(node->data.For.condition, local);
                    if (cond.data.float_value == 0.0) break;
                }
                RuntimeValue res = eval(node->data.For.body, local);
                if (res.type == VAL_RETURN) return res;
                if (node->data.For.step != NULL) eval(node->data.For.step, local);
            }
            return make_int(0);
        }

        case AST_INDEX: {
            RuntimeValue target = eval(node->data.Index.target, env);
            RuntimeValue idx = eval(node->data.Index.index, env);
            int i = (int)idx.data.float_value;

            if (target.type == VAL_STRING) {
                int len = (int)strlen(target.data.string_value);
                if (i < 0 || i >= len) exit(1);
                char tmp[2] = { target.data.string_value[i], '\0' };
                return make_string(tmp);
            }
            if (target.type == VAL_ARRAY) {
                if (i < 0 || i >= target.data.array_data.count) exit(1);
                return target.data.array_data.elements[i];
            }
            exit(1);
        }

        case AST_USING: {
            char *path = node->data.Using.path;
            FILE *f = fopen(path, "rb");
            if (!f) exit(1);
            fseek(f, 0, SEEK_END);
            long len = ftell(f);
            fseek(f, 0, SEEK_SET);
            char *src = malloc(len + 1);
            fread(src, 1, len, f);
            src[len] = '\0';
            fclose(f);

            Environment *mod_env = env_create_child(NULL);
            struct Lexer l;
            Token_init(&l, src);
            Parser p;
            parser_init(&p, &l);
            ASTNode *prog = parser_parse_program(&p);
            eval(prog, mod_env);

            RuntimeValue mod_obj;
            mod_obj.type = VAL_OBJECT;
            mod_obj.data.obj_env = mod_env;
            env_set(env, node->data.Using.alias, mod_obj);
            return make_int(0);
        }

        case AST_INDEX_ASSIGN: {
            ASTNode *lhs = node->data.IndexAssign.left;
            ASTNode *rhs = node->data.IndexAssign.right;
            RuntimeValue val = eval(rhs, env);
            if (val.type == VAL_RETURN) val.type = val.original_type;

            if (lhs->type == AST_IDENTIFIER) {
                env_update_fast(env, lhs->data.identifier.name, lhs->data.identifier.id_hash, val);
                return val;
            }
            if (lhs->type == AST_INDEX) {
                RuntimeValue target = eval(lhs->data.Index.target, env);
                RuntimeValue idx_val = eval(lhs->data.Index.index, env);
                int i = (int)idx_val.data.float_value;
                if (target.type == VAL_ARRAY) {
                    if (i < 0 || i >= target.data.array_data.count) exit(1);
                    if (val.type == VAL_STRING) val.data.string_value = strdup(val.data.string_value);
                    target.data.array_data.elements[i] = val;
                    return val;
                }
            }
            exit(1);
        }

        default:
            exit(1);
    }
}