#ifndef MKS_AST_H
#define MKS_AST_H

#include <stdbool.h>

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

typedef struct ASTNode {
    ASTNodeType type;
    int line;

    union {
        double number_value;
        char *string_value;

        struct {
            char *name;
            unsigned int id_hash;
        } identifier;

        struct {
            char *name;
            unsigned int id_hash;
            struct ASTNode *value;
        } var_decl;

        struct {
            char *name;
            unsigned int id_hash;
            struct ASTNode *value;
        } assign;

        struct {
            struct ASTNode *left;
            struct ASTNode *right;
            int op;
        } binop;

        struct {
            char *name;
            unsigned int id_hash;
            struct ASTNode **args;
            int arg_count;
        } func_call;

        struct {
            struct ASTNode *target;
            char *method_name;
            unsigned int method_hash;
            struct ASTNode **args;
            int arg_count;
        } method_call;

        struct {
            struct ASTNode **items;
            int count;
        } block;

        struct {
            struct ASTNode *condition;
            struct ASTNode *body;
            struct ASTNode *else_body;
        } if_block;

        struct {
            struct ASTNode *condition;
            struct ASTNode *body;
        } while_block;

        struct {
            struct ASTNode *value;
        } return_stmt;

        struct {
            char *name;
            char **params;
            int param_count;
            struct ASTNode *body;
        } func_decl;

        struct {
            struct ASTNode *init;
            struct ASTNode *condition;
            struct ASTNode *step;
            struct ASTNode *body;
        } for_block;

        struct {
            struct ASTNode *target;
            struct ASTNode *index;
        } index;

        struct {
            struct ASTNode **items;
            int item_count;
        } array;

        struct {
            struct ASTNode *left;
            struct ASTNode *right;
        } index_assign;

        struct {
            char *path;
            char *alias;
        } using_stmt;

        struct {
            struct ASTNode **args;
            int arg_count;
            bool is_newline;
        } output;
    } data;
} ASTNode;


ASTNode *create_ast_ident(char *name, unsigned int id_hash, int line);
ASTNode *create_ast_var_decl(ASTNode *value, int line, char *name, unsigned int id_hash);
ASTNode *create_ast_assign(char *name, unsigned int id_hash, ASTNode *value, int line);

ASTNode *create_ast_num(double val, int line);
ASTNode *create_ast_string(char *val, int line);
ASTNode *create_ast_array(ASTNode **elements, int count, int line);

ASTNode *create_ast_binop(ASTNode *left, ASTNode *right, int op, int line);
ASTNode *create_ast_index(ASTNode *target, ASTNode *index, int line);
ASTNode *create_ast_index_assign(ASTNode *left, ASTNode *right, int line);

ASTNode *create_ast_block(ASTNode **items, int count, int line);
ASTNode *create_if_block(ASTNode *condition, ASTNode *body, ASTNode *else_block, int line);
ASTNode *create_while_block(ASTNode *condition, ASTNode *body, int line);
ASTNode *create_ast_for(ASTNode *init, ASTNode *condition, ASTNode *step, ASTNode *body, int line);
ASTNode *create_ast_return(ASTNode *value, int line);

ASTNode *create_ast_func_decl(char *name, char **params, int param_count, ASTNode *body, int line);
ASTNode *create_ast_func_call(char *name, unsigned int id_hash, ASTNode **args, int arg_count, int line);
ASTNode *create_ast_method_call(ASTNode *target, char *name, unsigned int id_hash, ASTNode **args, int arg_count, int line);

ASTNode *create_ast_using(char *path, char *alias, int line);
ASTNode *create_ast_output(ASTNode **args, int count, bool is_newline, int line);


void delete_ast_node(ASTNode *node);

#endif