#include "AST.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

char* ast_dup(const char* str){
    if (!str) return nullptr;

    const size_t len = strlen(str);
    char* copy = (char*)malloc(len + 1);
    if (copy) strcpy(copy, str);

    return copy;
}



ASTNode* create_ast_num(const int val, const int line) {
    ASTNode* new_node = malloc(sizeof(ASTNode));
    new_node->type = AST_NUMBER;
    new_node->line = line;
    new_node->data.number_value = val;

    return new_node;
}

ASTNode* create_ast_ident(char* name, const int line) {
    ASTNode* new_node = malloc(sizeof(ASTNode));
    new_node->type = AST_IDENTIFIER;
    new_node->line = line;
    new_node->data.identifier_name = ast_dup(name);

    return new_node;
}

ASTNode* create_ast_binop(ASTNode *left, ASTNode *right, const int op, const int line) {
    ASTNode* new_node = malloc(sizeof(ASTNode));
    new_node->type = AST_BINOP;
    new_node->line = line;
    new_node->data.binop.left = left;
    new_node->data.binop.right = right;
    new_node->data.binop.op = op;

    return new_node;
}

ASTNode* create_ast_var_decl(ASTNode *value, const int line, char* name) {
    ASTNode* new_node = malloc(sizeof(ASTNode));
    new_node->type = AST_VAR_DECL;
    new_node->line = line;
    new_node->data.var_decl.name = ast_dup(name);

    return new_node;
}
ASTNode* create_ast_assign(ASTNode *value, char *name, int line) {
    ASTNode* new_node = malloc(sizeof(ASTNode));
    new_node->type = AST_ASSIGN;
    new_node->line = line;
    new_node->data.Assign.name = ast_dup(name);
    new_node->data.Assign.value = value;

    return new_node;
}
ASTNode* create_ast_writeln(ASTNode *expression, int line) {
    ASTNode *node = malloc(sizeof(ASTNode));
    node->type = AST_WRITELN;
    node->line = line;
    node->data.Writeln.expression = expression;
    return node;
}

ASTNode* create_ast_arrow(ASTNode *target, ASTNode *value, int line) {
    ASTNode* new_node = malloc(sizeof(ASTNode));
    new_node->type = AST_ARROW;
    new_node->line = line;
    new_node->data.Arrow.target = target;
    new_node->data.Arrow.value = value;
    return new_node;
}

void delete_ast_node(ASTNode* node) {

}

