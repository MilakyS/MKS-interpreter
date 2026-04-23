#ifndef MKS_AST_H
#define MKS_AST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum NumberKind {
    NUMBER_INT,
    NUMBER_FLOAT
} NumberKind;

typedef struct NumberLiteral {
    NumberKind kind;
    int64_t int_value;
    double float_value;
} NumberLiteral;

struct Environment;
struct EnvVar;

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
    AST_SWAP,
    AST_TEST,
    AST_OBJ_GET,
    AST_OBJ_SET,
    AST_ENTITY,
    AST_EXTEND,
    AST_DEFER,
    AST_WATCH,
    AST_ON_CHANGE,
    AST_BREAK,
    AST_CONTINUE,
    AST_REPEAT,
    AST_EXPORT,
    AST_ADDRESS_OF,
    AST_DEREF,
    AST_DEREF_ASSIGN,
} ASTNodeType;

typedef struct ASTNode {
    ASTNodeType type;
    int line;

    union {
        NumberLiteral number;
        char *string_value;

        struct {
            char *name;
            unsigned int id_hash;
            struct EnvVar *cached_entry;
            const struct Environment *cached_env;
            size_t cached_env_version;
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
            struct EnvVar *cached_entry;
            const struct Environment *cached_env;
            size_t cached_env_version;
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
            unsigned int name_hash;
            char **params;
            unsigned int *param_hashes;
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
            struct ASTNode *target;
        } address_of;

        struct {
            struct ASTNode *target;
        } deref;

        struct {
            struct ASTNode *target;
            struct ASTNode *value;
        } deref_assign;

        struct {
            struct ASTNode *object;
            char *field;
            unsigned int field_hash;
        } obj_get;

        struct {
            struct ASTNode *object;
            char *field;
            unsigned int field_hash;
            struct ASTNode *value;
        } obj_set;

        struct {
            struct ASTNode *left;
            struct ASTNode *right;
        } swap_stmt;

        struct {
            char *name;
            struct ASTNode *body;
        } test_block;

        struct {
            char *name;
            unsigned int name_hash;
            char **params;
            unsigned int *param_hashes;
            int param_count;
            struct ASTNode *init_body;
            struct ASTNode **methods;
            int method_count;
        } entity;

        struct {
            int target_type; /* enum ExtTarget */
            struct ASTNode **methods;
            int method_count;
        } extend;

        struct {
            struct ASTNode *body;
        } defer_stmt;

        struct {
            char *name;
            unsigned int hash;
        } watch_stmt;

        struct {
            char *name;
            unsigned int hash;
            struct ASTNode *body;
        } on_change_stmt;

        struct {
            char *path;
            char *alias;
            bool is_legacy_path;
            bool star_import;
        } using_stmt;

        struct {
            struct ASTNode *decl; /* wrapped declaration */
            char *name_override; /* optional explicit name for export var if needed */
        } export_stmt;

        struct {
            bool has_iterator;
            char *iter_name;
            unsigned int iter_hash;
            struct ASTNode *count_expr;
            struct ASTNode *body;
        } repeat_stmt;

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

ASTNode *create_ast_int(int64_t val, int line);
ASTNode *create_ast_float(double val, int line);
ASTNode *create_ast_num(double val, int line);
ASTNode *create_ast_string(char *val, int line);
ASTNode *create_ast_array(ASTNode **elements, int count, int line);

ASTNode *create_ast_binop(ASTNode *left, ASTNode *right, int op, int line);
ASTNode *create_ast_index(ASTNode *target, ASTNode *index, int line);
ASTNode *create_ast_index_assign(ASTNode *left, ASTNode *right, int line);
ASTNode *create_ast_address_of(ASTNode *target, int line);
ASTNode *create_ast_deref(ASTNode *target, int line);
ASTNode *create_ast_deref_assign(ASTNode *target, ASTNode *value, int line);
ASTNode *create_ast_swap(ASTNode *l, ASTNode *r, int line);
ASTNode *create_ast_test(char *name, ASTNode *body, int line);
ASTNode *create_ast_obj_get(ASTNode *object, char *field, unsigned int hash, int line);
ASTNode *create_ast_obj_set(ASTNode *object, char *field, unsigned int hash, ASTNode *value, int line);

ASTNode *create_ast_block(ASTNode **items, int count, int line);
ASTNode *create_if_block(ASTNode *condition, ASTNode *body, ASTNode *else_block, int line);
ASTNode *create_while_block(ASTNode *condition, ASTNode *body, int line);
ASTNode *create_ast_for(ASTNode *init, ASTNode *condition, ASTNode *step, ASTNode *body, int line);
ASTNode *create_ast_return(ASTNode *value, int line);

ASTNode *create_ast_func_decl(char *name, unsigned int name_hash, char **params,unsigned int *param_hashes, int param_count,ASTNode *body, int line);
ASTNode *create_ast_func_call(char *name, unsigned int id_hash, ASTNode **args, int arg_count, int line);
ASTNode *create_ast_method_call(ASTNode *target, char *name, unsigned int id_hash, ASTNode **args, int arg_count, int line);

ASTNode *create_ast_using(char *path, char *alias, bool is_legacy_path, bool star_import, int line);
ASTNode *create_ast_output(ASTNode **args, int count, bool is_newline, int line);

ASTNode *create_ast_entity(char *name, unsigned int hash, char **params, unsigned int *param_hashes, int param_count, ASTNode *init_body, ASTNode **methods, int method_count, int line);
ASTNode *create_ast_extend(int target_type, ASTNode **methods, int method_count, int line);
ASTNode *create_ast_defer(ASTNode *body, int line);
ASTNode *create_ast_watch(char *name, unsigned int hash, int line);
ASTNode *create_ast_on_change(char *name, unsigned int hash, ASTNode *body, int line);
ASTNode *create_ast_break(int line);
ASTNode *create_ast_continue(int line);
ASTNode *create_ast_repeat(bool has_iter, char *iter, unsigned int iter_hash, ASTNode *count_expr, ASTNode *body, int line);
ASTNode *create_ast_export(ASTNode *decl, char *name_override, int line);


void delete_ast_node(ASTNode *node);

#endif
