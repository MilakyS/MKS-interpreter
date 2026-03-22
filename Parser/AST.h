#ifndef CMINUSINTERPRETATOR_AST_H
#define CMINUSINTERPRETATOR_AST_H

#include <stddef.h>
#include <stdbool.h>

// Все типы узлов нашего дерева
typedef enum ASTNodeType {
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
    AST_FOR,
    AST_INDEX,
    AST_ARRAY,
    AST_METHOD_CALL,
    AST_USING,
    AST_INDEX_ASSIGN,
} ASTNodeType;

// Основная структура узла
typedef struct ASTNode {
    ASTNodeType type;
    int line;

    union {
        double number_value;
        char* string_value;

        struct { char* name; unsigned int id_hash; } identifier;
        struct { char* name; unsigned int id_hash; struct ASTNode *value; } var_decl;
        struct { char* name; unsigned int id_hash; struct ASTNode *value; } Assign;


        struct { struct ASTNode *left; struct ASTNode *right; int op; } binop;
        struct { char *name; unsigned int id_hash; struct ASTNode **args; int arg_count; } FuncCall;
        struct {
            struct ASTNode *target;
            char *method_name;
            unsigned int method_hash;
            struct ASTNode **args;
            int arg_count;
        } MethodCall;


        struct { struct ASTNode **items; size_t count; } Block;
        struct { struct ASTNode *condition; struct ASTNode *body; struct ASTNode *else_body; } IfBlck;
        struct { struct ASTNode *condition; struct ASTNode *body; } While;
        struct { struct ASTNode *value; } Return;


        struct {
            char *name;
            char **params;
            int param_count;
            struct ASTNode *body;
        } FuncDecl;


        struct {
            struct ASTNode *init;
            struct ASTNode *condition;
            struct ASTNode *step;
            struct ASTNode *body;
        } For;


        struct { struct ASTNode *target; struct ASTNode *index; } Index;
        struct { struct ASTNode **items; int item_count; } Array;
        struct { struct ASTNode *left; struct ASTNode *right; } IndexAssign;

        struct { char *path; char *alias; } Using;


        struct { struct ASTNode **args; int arg_count; bool is_newline; } Output;
    } data;
} ASTNode;

ASTNode* create_ast_ident(const char* name, unsigned int id_hash, int line);
ASTNode* create_ast_var_decl(struct ASTNode *value, int line, const char* name, unsigned int id_hash);
ASTNode* create_ast_assign(const char *name, unsigned int id_hash, struct ASTNode *value, int line);
ASTNode* create_ast_func_call(const char *name, unsigned int id_hash, struct ASTNode **args, int arg_count, int line);
ASTNode* create_ast_method_call(struct ASTNode *target, const char *name, unsigned int id_hash, struct ASTNode **args, int arg_count, int line);


ASTNode* create_ast_num(double val, int line);
ASTNode* create_ast_string(const char* val, int line);
ASTNode* create_ast_array(struct ASTNode **elements, int count, int line);


ASTNode* create_ast_binop(struct ASTNode *left, struct ASTNode *right, int op, int line);
ASTNode* create_ast_index(struct ASTNode *target, struct ASTNode *index, int line);
ASTNode* create_ast_index_assign(struct ASTNode *left, struct ASTNode *right, int line);


ASTNode* create_ast_block(struct ASTNode **items, int count, int line);
ASTNode* create_if_block(struct ASTNode *condition, struct ASTNode *body, struct ASTNode *else_block, int line);
ASTNode* create_while_block(struct ASTNode *condition, struct ASTNode *body, int line);
ASTNode* create_ast_for(struct ASTNode *init, struct ASTNode *condition, struct ASTNode *step, struct ASTNode *body);
ASTNode* create_ast_return(struct ASTNode *value, int line);


ASTNode* create_ast_func_decl(const char *name, char **params, int param_count, struct ASTNode *body, int line);
ASTNode* create_ast_using(const char* path, const char* alias, int line);
ASTNode* create_ast_output(struct ASTNode **args, int count, bool is_newline, int line);

void delete_ast_node(ASTNode *node);

#endif // CMINUSINTERPRETATOR_AST_H