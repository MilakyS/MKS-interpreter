#include "AST.h"
#include <stdlib.h>
#include <string.h>

static ASTNode *create_ast(const enum ASTNodeType type, const int line) {
    ASTNode *node = malloc(sizeof(*node));
    if (!node) return NULL;
    memset(node, 0, sizeof(*node));
    node->line = line;
    node->type = type;
    return node;
}

unsigned int hash(const char *str) {
    unsigned int hash = 5381; int c;
    while ((c = *str++)) hash = ((hash << 5) + hash) + c;
    return hash;
}

ASTNode *create_ast_ident(const char *name, const int line) {
    ASTNode *node = create_ast(AST_IDENTIFIER, line);
    node->data.identifier.name = (char*)name;
    node->data.identifier.id_hash = hash(name);
    return node;
}

ASTNode *create_ast_assign(ASTNode *value, const char *name, const int line) {
    ASTNode *node = create_ast(AST_ASSIGN, line);
    node->data.Assign.name = (char*)name;
    node->data.Assign.value = value;
    node->data.Assign.id_hash = hash(name);
    return node;
}

ASTNode *create_ast_var_decl(ASTNode *value, const int line, const char *name) {
    ASTNode *node = create_ast(AST_VAR_DECL, line);
    node->data.var_decl.name = (char*)name;
    node->data.var_decl.value = value;
    node->data.var_decl.id_hash = hash(name);
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
ASTNode* create_ast_for(ASTNode *init, ASTNode *condition, ASTNode *step, ASTNode *body) {
    int line = init ? init->line : 0;
    ASTNode *node = create_ast(AST_FOR, line);
    node->data.For.init = init;
    node->data.For.condition = condition;
    node->data.For.step = step;
    node->data.For.body = body;
    return node;
}

ASTNode* create_while_block(ASTNode *condition, ASTNode *body, const int line) {
    ASTNode *node = create_ast(AST_WHILE, line);
    node->data.While.condition = condition;
    node->data.While.body = body;
    return node;
}
ASTNode* create_ast_index(ASTNode *target, ASTNode *index, int line) {
    ASTNode *node = create_ast(AST_INDEX, line);
    node->data.Index.target = target;
    node->data.Index.index = index;
    return node;
}

ASTNode* create_ast_string(const char* val, const int line) {
    ASTNode *node = create_ast(AST_STRING, line);
    node->data.string_value = (char*)val;
    return node;
}

ASTNode* create_ast_func_decl(const char *name, char **params, int param_count, ASTNode *body, int line) {
    ASTNode *node = create_ast(AST_FUNC_DECL, line);
    node->data.FuncDecl.name = (char*)name;
    node->data.FuncDecl.params = params;
    node->data.FuncDecl.param_count = param_count;
    node->data.FuncDecl.body = body;
    return node;
}

ASTNode* create_ast_return(ASTNode *value, int line) {
    ASTNode *node = create_ast(AST_RETURN, line);
    node->data.Return.value = value;
    return node;
}
ASTNode* create_ast_array(ASTNode **elements, int count, int line) {
    ASTNode *node = create_ast(AST_ARRAY, line);
    node->data.Array.items = elements;
    node->data.Array.item_count = count;
    return node;
}

ASTNode* create_ast_func_call(const char *name, ASTNode **args, int arg_count, int line) {
    ASTNode *node = create_ast(AST_FUNC_CALL, line);
    node->data.FuncCall.name = (char*)name;
    node->data.FuncCall.args = args;
    node->data.FuncCall.arg_count = arg_count;
    return node;
}
ASTNode* create_ast_using(const char* path, const char* alias, int line) {
    ASTNode *node = create_ast(AST_USING, line);
    node->data.Using.path = (char*)path;
    node->data.Using.alias = (char*)alias;
    return node;
}


ASTNode* create_ast_output(ASTNode **args, int count, bool is_newline, int line) {
    ASTNode *node = create_ast(AST_OUTPUT, line);
    node->data.Output.args = args;
    node->data.Output.arg_count = count;
    node->data.Output.is_newline = is_newline;
    return node;
}
ASTNode* create_ast_method_call(ASTNode *target, const char *name, ASTNode **args, int arg_count, int line) {
    ASTNode *node = create_ast(AST_METHOD_CALL, line);
    node->data.MethodCall.target = target;
    node->data.MethodCall.method_name = (char*)name;
    node->data.MethodCall.args = args;
    node->data.MethodCall.arg_count = arg_count;
    return node;
}

void delete_ast_node(ASTNode *node) {
    if (!node) return;

    switch (node->type) {
        case AST_NUMBER: break;
        case AST_STRING: free(node->data.string_value); break;
        case AST_IDENTIFIER: free(node->data.identifier.name); break;
        case AST_BLOCK:
            for (size_t i = 0; i < node->data.Block.count; i++) delete_ast_node(node->data.Block.items[i]);
            free(node->data.Block.items);
            break;
        case AST_IF_BLOCK:
            delete_ast_node(node->data.IfBlck.condition);
            delete_ast_node(node->data.IfBlck.body);
            delete_ast_node(node->data.IfBlck.else_body);
            break;
        case AST_WHILE:
            delete_ast_node(node->data.While.condition);
            delete_ast_node(node->data.While.body);
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
        case AST_OUTPUT:
            for (int i = 0; i < node->data.Output.arg_count; i++) delete_ast_node(node->data.Output.args[i]);
            free(node->data.Output.args);
            break;
        case AST_RETURN:
            delete_ast_node(node->data.Return.value);
            break;
        case AST_FUNC_DECL:
            free(node->data.FuncDecl.name);
            for (int i = 0; i < node->data.FuncDecl.param_count; i++) free(node->data.FuncDecl.params[i]);
            free(node->data.FuncDecl.params);
            delete_ast_node(node->data.FuncDecl.body);
            break;
        case AST_FUNC_CALL:
            free(node->data.FuncCall.name);
            for (int i = 0; i < node->data.FuncCall.arg_count; i++) delete_ast_node(node->data.FuncCall.args[i]);
            free(node->data.FuncCall.args);
            break;
        case AST_FOR:
            delete_ast_node(node->data.For.init);
            delete_ast_node(node->data.For.condition);
            delete_ast_node(node->data.For.step);
            delete_ast_node(node->data.For.body);
            break;
        case AST_INDEX:
            delete_ast_node(node->data.Index.target);
            delete_ast_node(node->data.Index.index);
            break;
        case AST_ARRAY:
            for (int i = 0; i < node->data.Array.item_count; i++) {
                delete_ast_node(node->data.Array.items[i]);
            }
            free(node->data.Array.items);
            break;
        case AST_METHOD_CALL:
            delete_ast_node(node->data.MethodCall.target);
            free(node->data.MethodCall.method_name);
            for (int i = 0; i < node->data.MethodCall.arg_count; i++) {
                delete_ast_node(node->data.MethodCall.args[i]);
            }
            free(node->data.MethodCall.args);
            break;
        case AST_USING:
            free(node->data.Using.path);
            if (node->data.Using.alias) free(node->data.Using.alias);
            break;
    }
    free(node);
}