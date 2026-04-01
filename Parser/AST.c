#include "AST.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static ASTNode *create_ast(ASTNodeType type, int line) {
    ASTNode *node = malloc(sizeof(ASTNode));
    if (node == NULL) {
        fprintf(stderr, "[MKS AST Error] Out of memory while creating AST node\n");
        exit(1);
    }

    memset(node, 0, sizeof(ASTNode));
    node->type = type;
    node->line = line;
    return node;
}

ASTNode *create_ast_ident(char *name, unsigned int id_hash, int line) {
    ASTNode *node = create_ast(AST_IDENTIFIER, line);
    node->data.identifier.name = name;
    node->data.identifier.id_hash = id_hash;
    return node;
}

ASTNode *create_ast_var_decl(ASTNode *value, int line, char *name, unsigned int id_hash) {
    ASTNode *node = create_ast(AST_VAR_DECL, line);
    node->data.var_decl.name = name;
    node->data.var_decl.id_hash = id_hash;
    node->data.var_decl.value = value;
    return node;
}

ASTNode *create_ast_assign(char *name, unsigned int id_hash, ASTNode *value, int line) {
    ASTNode *node = create_ast(AST_ASSIGN, line);
    node->data.assign.name = name;
    node->data.assign.id_hash = id_hash;
    node->data.assign.value = value;
    return node;
}

ASTNode *create_ast_num(double val, int line) {
    ASTNode *node = create_ast(AST_NUMBER, line);
    node->data.number_value = val;
    return node;
}

ASTNode *create_ast_string(char *val, int line) {
    ASTNode *node = create_ast(AST_STRING, line);
    node->data.string_value = val;
    return node;
}

ASTNode *create_ast_array(ASTNode **elements, int count, int line) {
    ASTNode *node = create_ast(AST_ARRAY, line);
    node->data.array.items = elements;
    node->data.array.item_count = count;
    return node;
}

ASTNode *create_ast_binop(ASTNode *left, ASTNode *right, int op, int line) {
    ASTNode *node = create_ast(AST_BINOP, line);
    node->data.binop.left = left;
    node->data.binop.right = right;
    node->data.binop.op = op;
    return node;
}

ASTNode *create_ast_index(ASTNode *target, ASTNode *index, int line) {
    ASTNode *node = create_ast(AST_INDEX, line);
    node->data.index.target = target;
    node->data.index.index = index;
    return node;
}

ASTNode *create_ast_index_assign(ASTNode *left, ASTNode *right, int line) {
    ASTNode *node = create_ast(AST_INDEX_ASSIGN, line);
    node->data.index_assign.left = left;
    node->data.index_assign.right = right;
    return node;
}

ASTNode *create_ast_block(ASTNode **items, int count, int line) {
    ASTNode *node = create_ast(AST_BLOCK, line);
    node->data.block.items = items;
    node->data.block.count = count;
    return node;
}

ASTNode *create_if_block(ASTNode *condition, ASTNode *body, ASTNode *else_block, int line) {
    ASTNode *node = create_ast(AST_IF_BLOCK, line);
    node->data.if_block.condition = condition;
    node->data.if_block.body = body;
    node->data.if_block.else_body = else_block;
    return node;
}

ASTNode *create_while_block(ASTNode *condition, ASTNode *body, int line) {
    ASTNode *node = create_ast(AST_WHILE, line);
    node->data.while_block.condition = condition;
    node->data.while_block.body = body;
    return node;
}

ASTNode *create_ast_for(ASTNode *init, ASTNode *condition, ASTNode *step, ASTNode *body, int line) {
    ASTNode *node = create_ast(AST_FOR, line);
    node->data.for_block.init = init;
    node->data.for_block.condition = condition;
    node->data.for_block.step = step;
    node->data.for_block.body = body;
    return node;
}

ASTNode *create_ast_return(ASTNode *value, int line) {
    ASTNode *node = create_ast(AST_RETURN, line);
    node->data.return_stmt.value = value;
    return node;
}

ASTNode *create_ast_func_decl(char *name, char **params,unsigned int *param_hashes, const int param_count,ASTNode *body, const int line) {
    ASTNode *node = create_ast(AST_FUNC_DECL, line);
    node->data.func_decl.name = name;
    node->data.func_decl.params = params;
    node->data.func_decl.param_hashes = param_hashes;
    node->data.func_decl.param_count = param_count;
    node->data.func_decl.body = body;
    return node;
}

ASTNode *create_ast_func_call(char *name, unsigned int id_hash, ASTNode **args, int arg_count, int line) {
    ASTNode *node = create_ast(AST_FUNC_CALL, line);
    node->data.func_call.name = name;
    node->data.func_call.id_hash = id_hash;
    node->data.func_call.args = args;
    node->data.func_call.arg_count = arg_count;
    return node;
}

ASTNode *create_ast_method_call(ASTNode *target, char *name, unsigned int id_hash, ASTNode **args, int arg_count, int line) {
    ASTNode *node = create_ast(AST_METHOD_CALL, line);
    node->data.method_call.target = target;
    node->data.method_call.method_name = name;
    node->data.method_call.method_hash = id_hash;
    node->data.method_call.args = args;
    node->data.method_call.arg_count = arg_count;
    return node;
}

ASTNode *create_ast_using(char *path, char *alias, int line) {
    ASTNode *node = create_ast(AST_USING, line);
    node->data.using_stmt.path = path;
    node->data.using_stmt.alias = alias;
    return node;
}

ASTNode *create_ast_output(ASTNode **args, int count, bool is_newline, int line) {
    ASTNode *node = create_ast(AST_OUTPUT, line);
    node->data.output.args = args;
    node->data.output.arg_count = count;
    node->data.output.is_newline = is_newline;
    return node;
}

void delete_ast_node(ASTNode *node) {
    if (node == NULL) {
        return;
    }

    switch (node->type) {
        case AST_BLOCK:
            if (node->data.block.items != NULL) {
                for (int i = 0; i < node->data.block.count; i++) {
                    delete_ast_node(node->data.block.items[i]);
                }
                free(node->data.block.items);
            }
            break;

        case AST_IF_BLOCK:
            delete_ast_node(node->data.if_block.condition);
            delete_ast_node(node->data.if_block.body);
            delete_ast_node(node->data.if_block.else_body);
            break;

        case AST_WHILE:
            delete_ast_node(node->data.while_block.condition);
            delete_ast_node(node->data.while_block.body);
            break;

        case AST_BINOP:
            delete_ast_node(node->data.binop.left);
            delete_ast_node(node->data.binop.right);
            break;

        case AST_VAR_DECL:
            free(node->data.var_decl.name);
            delete_ast_node(node->data.var_decl.value);
            break;

        case AST_ASSIGN:
            free(node->data.assign.name);
            delete_ast_node(node->data.assign.value);
            break;

        case AST_IDENTIFIER:
            free(node->data.identifier.name);
            break;

        case AST_STRING:
            free(node->data.string_value);
            break;

        case AST_OUTPUT:
            if (node->data.output.args != NULL) {
                for (int i = 0; i < node->data.output.arg_count; i++) {
                    delete_ast_node(node->data.output.args[i]);
                }
                free(node->data.output.args);
            }
            break;

        case AST_RETURN:
            delete_ast_node(node->data.return_stmt.value);
            break;

        case AST_FUNC_DECL:
            free(node->data.func_decl.name);

            if (node->data.func_decl.params != NULL) {
                for (int i = 0; i < node->data.func_decl.param_count; i++) {
                    free(node->data.func_decl.params[i]);
                }
                free(node->data.func_decl.params);
            }

            free(node->data.func_decl.param_hashes);

            delete_ast_node(node->data.func_decl.body);
            break;

        case AST_FUNC_CALL:
            free(node->data.func_call.name);
            if (node->data.func_call.args != NULL) {
                for (int i = 0; i < node->data.func_call.arg_count; i++) {
                    delete_ast_node(node->data.func_call.args[i]);
                }
                free(node->data.func_call.args);
            }
            break;

        case AST_FOR:
            delete_ast_node(node->data.for_block.init);
            delete_ast_node(node->data.for_block.condition);
            delete_ast_node(node->data.for_block.step);
            delete_ast_node(node->data.for_block.body);
            break;

        case AST_INDEX:
            delete_ast_node(node->data.index.target);
            delete_ast_node(node->data.index.index);
            break;

        case AST_ARRAY:
            if (node->data.array.items != NULL) {
                for (int i = 0; i < node->data.array.item_count; i++) {
                    delete_ast_node(node->data.array.items[i]);
                }
                free(node->data.array.items);
            }
            break;

        case AST_METHOD_CALL:
            delete_ast_node(node->data.method_call.target);
            free(node->data.method_call.method_name);
            if (node->data.method_call.args != NULL) {
                for (int i = 0; i < node->data.method_call.arg_count; i++) {
                    delete_ast_node(node->data.method_call.args[i]);
                }
                free(node->data.method_call.args);
            }
            break;

        case AST_USING:
            free(node->data.using_stmt.path);
            free(node->data.using_stmt.alias);
            break;

        case AST_INDEX_ASSIGN:
            delete_ast_node(node->data.index_assign.left);
            delete_ast_node(node->data.index_assign.right);
            break;

        case AST_NUMBER:
        default:
            break;
    }

    free(node);
}