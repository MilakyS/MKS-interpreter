#ifndef CMINUSINTERPRETATOR_AST_H
#define CMINUSINTERPRETATOR_AST_H
#include <stddef.h>


enum ASTNodeType {
    AST_BINOP,
    AST_VAR_DECL,
    AST_WRITELN,
    AST_ERROR,
    AST_IDENTIFIER,
    AST_NUMBER,
    AST_ASSIGN,
    AST_ARROW,
    AST_BLOCK,
    AST_STRING,
    AST_IF_BLOCK,
    AST_WHILE,
};

typedef struct ASTNode{
    enum ASTNodeType type;
    int line;

    union {
        int number_value;
        struct {
            char* name;
            unsigned int id_hash;
        } identifier;
        char* string_value;

        struct {
            struct ASTNode *left;
            struct ASTNode *right;
            int op;
        }binop;
        struct {
            char* name;
            struct ASTNode *value;
            unsigned int id_hash;
        }var_decl;
        struct {
            struct ASTNode *expression;
        }Writeln;
        struct {
            char* name;
            struct ASTNode *value;
            unsigned int id_hash;
        }Assign;
        struct {
            struct ASTNode *target;
            char* field;
        }Arrow;
        struct {
            struct ASTNode **items;
            size_t count;
            size_t capacity;
        }Block;
        struct {
            struct ASTNode *condition;
            struct ASTNode *body;
            struct ASTNode *else_body;
        }IfBlck;
        struct {
            struct ASTNode *condition;
            struct ASTNode *body;
        }While;
    }data;
}ASTNode;


ASTNode* create_ast_string(const char* val, int line);
ASTNode* create_ast_binop(ASTNode *left, ASTNode *right, int op, int line);
ASTNode* create_ast_num(int val, int line);
ASTNode* create_ast_ident(const char* name, int line);
ASTNode* create_ast_var_decl(ASTNode *value, int line, const char* name);
ASTNode* create_ast_writeln(ASTNode *expression, int line);
ASTNode* create_ast_arrow(ASTNode *target, const char* value, int line);
ASTNode* create_ast_assign(ASTNode *value, const char *name, int line);
ASTNode* create_if_block(ASTNode *condition, ASTNode *body, ASTNode *else_block, int line);
ASTNode* create_ast_block(ASTNode **items, int count, int line);
ASTNode* create_while_block(ASTNode *condition, ASTNode *body, int line);

void delete_ast_node(ASTNode *node);

#endif //CMINUSINTERPRETATOR_AST_H