#ifndef MKS_PARSER_H
#define MKS_PARSER_H

#include <stdbool.h>
#include <stddef.h>

#include "../Lexer/lexer.h"
#include "AST.h"
#include "../Utils/hash.h"

typedef struct Parser {
    struct Lexer *lexer;
    struct Token current_token;
} Parser;

/* public API */
void parser_init(Parser *parser, struct Lexer *lexer);
ASTNode* parser_parse_program(Parser *parser);

/* core */
void parser_advance(Parser *parser);
void parser_eat(Parser *parser, enum TokenType expected_type);
enum TokenType peek_token_type(Parser *parser);

/* errors */
void parser_error(Parser *parser, const char *message);
void parser_error_expected(Parser *parser, enum TokenType expected_type);

/* parser entry points shared between parser_expr.c / parser_stmt.c */
ASTNode* parser_parse_expression(Parser *parser);
ASTNode* parser_parse_statement(Parser *parser);
ASTNode* parser_parse_block(Parser *parser);

/* helpers */
bool parser_match(Parser *parser, enum TokenType type);
bool parser_current_is_identifier(Parser *parser, const char *text);
bool parser_match_identifier(Parser *parser, const char *text);

char *mks_strndup(const char *s, size_t n);
char *parser_take_identifier(Parser *parser, unsigned int *out_hash);

/* memory helpers */
void *parser_xmalloc(size_t size);
void *parser_xcalloc(size_t count, size_t size);
void *parser_xrealloc(void *ptr, size_t size);

/* dynamic arrays */
void parser_push_ast(ASTNode ***items, int *count, int *cap, ASTNode *node);
void parser_push_str(char ***items, int *count, int *cap, char *value);

#endif