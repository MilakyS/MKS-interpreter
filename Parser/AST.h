#ifndef CMINUSINTERPRETATOR_AST_H
#define CMINUSINTERPRETATOR_AST_H


enum ASTNodeType {
    AST_BINOP,
    AST_VAR_DECL,
    AST_WRITELN,
    AST_ERROR,
    AST_IDENTIFIER,
    AST_NUMBER,
    AST_ASSIGN,
    AST_ARROW,
};

typedef struct ASTNode{
    enum ASTNodeType type;
    int line;

    union {
        int number_value;
        char* identifier_name;

        struct {
            struct ASTNode *left;
            struct ASTNode *right;
            int op;
        }binop;
        struct {
            char* name;
            struct ASTNode *value;
        }var_decl;
        struct {
            struct ASTNode *expression;
        }Writeln;
        struct {
            char* name;
            struct ASTNode *value;
        }Assign;
        struct {
            struct ASTNode *target;
            char* field;
        }Arrow;
    }data;
}ASTNode;

ASTNode* create_ast_binop(ASTNode *left, ASTNode *right, int op, int line);
ASTNode* create_ast_num(int val, int line);
ASTNode* create_ast_ident(const char* name, int line);
ASTNode* create_ast_var_decl(ASTNode *value, int line, const char* name);
ASTNode* create_ast_writeln(ASTNode *expression, int line);
ASTNode* create_ast_arrow(ASTNode *target, const char* value, int line);
ASTNode* create_ast_assign(ASTNode *value, const char *name, int line);

void delete_ast_node(ASTNode *node);

#endif //CMINUSINTERPRETATOR_AST_H