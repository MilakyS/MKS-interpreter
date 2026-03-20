#ifndef CMINUSINTERPRETATOR_AST_H
#define CMINUSINTERPRETATOR_AST_H

#include <stddef.h>
#include <stdbool.h>

enum ASTNodeType {
    AST_BINOP,
    AST_VAR_DECL,
    AST_OUTPUT,
    AST_IDENTIFIER,
    AST_NUMBER,
    AST_ASSIGN,
    AST_RETURN,
    AST_BLOCK,
    AST_STRING,
    AST_IF_BLOCK,
    AST_WHILE,
    AST_FUNC_DECL,
    AST_FUNC_CALL,
};

typedef struct ASTNode {
    enum ASTNodeType type;
    int line;

    union {
        int number_value;
        struct { char* name; unsigned int id_hash; } identifier;
        char* string_value;

        struct { struct ASTNode *left; struct ASTNode *right; int op; } binop;
        struct { char* name; struct ASTNode *value; unsigned int id_hash; } var_decl;
        struct { char* name; struct ASTNode *value; unsigned int id_hash; } Assign;
        struct { struct ASTNode **items; size_t count; } Block;
        struct { struct ASTNode *condition; struct ASTNode *body; struct ASTNode *else_body; } IfBlck;
        struct { struct ASTNode *condition; struct ASTNode *body; } While;
        struct { struct ASTNode *value; } Return;

        struct {
            char *name; char **params; int param_count; struct ASTNode *body;
        } FuncDecl;
        struct {
            char *name; struct ASTNode **args; int arg_count;
        } FuncCall;
        struct {
            struct ASTNode **args; int arg_count; bool is_newline;
        } Output;
    } data;
} ASTNode;

ASTNode* create_ast_string(const char* val, int line);
ASTNode* create_ast_binop(ASTNode *left, ASTNode *right, int op, int line);
ASTNode* create_ast_num(int val, int line);
ASTNode* create_ast_ident(const char* name, int line);
ASTNode* create_ast_var_decl(ASTNode *value, int line, const char* name);
ASTNode* create_ast_assign(ASTNode *value, const char *name, int line);
ASTNode* create_if_block(ASTNode *condition, ASTNode *body, ASTNode *else_block, int line);
ASTNode* create_ast_block(ASTNode **items, int count, int line);
ASTNode* create_while_block(ASTNode *condition, ASTNode *body, int line);
ASTNode* create_ast_func_call(const char *name, ASTNode **args, int arg_count, int line);
ASTNode* create_ast_func_decl(const char *name, char **params, int param_count, ASTNode *body, int line);
ASTNode* create_ast_return(ASTNode *value, int line);
ASTNode* create_ast_output(ASTNode **args, int count, bool is_newline, int line);

void delete_ast_node(ASTNode *node);

#endif