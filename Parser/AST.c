#include "AST.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

char *ast_dup(const char *str) {
    if (!str) return NULL;

    const size_t len = strlen(str);
    char *copy = malloc(len + 1);
    if (copy) strcpy(copy, str);

    return copy;
}


static ASTNode *create_ast(const enum ASTNodeType type, const int line) {
    ASTNode *node = malloc(sizeof(*node));

    if (!node) return NULL;

    memset(node, 0, sizeof(*node));
    node->line = line;
    node->type = type;

    return node;
}

unsigned int hash(const char *str) {
    unsigned int hash = 5381;
    int c;
    while ((c = *str++))
        hash = ((hash << 5) + hash) + c;
    return hash;
}

ASTNode *create_ast_ident(const char *name, const int line) {
    ASTNode *node = create_ast(AST_IDENTIFIER, line);
    node->data.identifier.name = ast_dup(name);
    node->data.var_decl.id_hash = hash(name);
    return node;
}

ASTNode *create_ast_assign(ASTNode *value, const char *name, const int line) {
    ASTNode *node = create_ast(AST_ASSIGN, line);
    node->data.Assign.name = ast_dup(name);
    node->data.Assign.value = value;
    node->data.Assign.id_hash = hash(name); // Считаем тут
    return node;
}

ASTNode *create_ast_var_decl(ASTNode *value, const int line, const char *name) {
    ASTNode *node = create_ast(AST_VAR_DECL, line);
    node->data.var_decl.name = ast_dup(name);
    node->data.var_decl.value = value;
    node->data.var_decl.id_hash = hash(name); // И тут
    return node;
}


ASTNode *create_ast_num(const int val, const int line) {
    ASTNode *node = create_ast(AST_NUMBER, line);
    node->data.number_value = val;

    return node;
}


ASTNode *create_ast_binop(ASTNode *left, ASTNode *right, const int op, const int line) {
    ASTNode *node = create_ast(AST_BINOP, line);
    node->data.binop.left = left;
    node->data.binop.right = right;
    node->data.binop.op = op;

    return node;
}


ASTNode* create_if_block(ASTNode *condition, ASTNode *body, ASTNode *else_block, int line) {
    ASTNode *node = create_ast(AST_IF_BLOCK, line);
    node->data.IfBlck.condition = condition;
    node->data.IfBlck.body = body;
    node->data.IfBlck.else_body = else_block;
    return node;
}

ASTNode* create_ast_block(ASTNode **items, int count, int line) {
    ASTNode *node = create_ast(AST_BLOCK, line);
    node->data.Block.items = items;
    node->data.Block.count = count;
    return node;
}


ASTNode *create_ast_writeln(ASTNode *expression, const int line) {
    ASTNode *node = create_ast(AST_WRITELN, line);
    node->data.Writeln.expression = expression;

    return node;
}
ASTNode* create_while_block(ASTNode *condition, ASTNode *body, const int line) {
    ASTNode *node = create_ast(AST_WHILE, line);
    node->data.While.condition = condition;
    node->data.While.body = body;

    return node;
}

ASTNode* create_ast_string(const char* val, const int line) {
    ASTNode *node = create_ast(AST_STRING, line);
    node->data.string_value = ast_dup(val);
    return node;
}

ASTNode *create_ast_arrow(ASTNode *target, const char* value, const int line) {
    ASTNode *node = create_ast(AST_ARROW, line);
    if (!node) {
        return NULL;
    }
    node->data.Arrow.target = target;
    node->data.Arrow.field = ast_dup(value);

    if (value && !node->data.Arrow.field) {
        free(node);
        return NULL;
    }

    return node;
}

void delete_ast_node(ASTNode *node) {
    if (!node) return;

    switch (node->type) {
        case AST_NUMBER:
            break;

        case AST_IDENTIFIER:
            free(node->data.identifier.name);
            break;
        case AST_BLOCK:
            for (int i = 0; i < node->data.Block.count; i++) {
                delete_ast_node(node->data.Block.items[i]);
            }
            free(node->data.Block.items);
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
            free(node->data.Assign.name);
            delete_ast_node(node->data.Assign.value);
            break;

        case AST_WRITELN:
            delete_ast_node(node->data.Writeln.expression);
            break;

        case AST_ARROW:
            delete_ast_node(node->data.Arrow.target);
            free(node->data.Arrow.field);
            break;

        default:
            break;
    }

    free(node);
}

