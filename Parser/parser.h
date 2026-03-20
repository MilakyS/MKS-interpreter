#ifndef CMINUSINTERPRETATOR_PARSER_H
#define CMINUSINTERPRETATOR_PARSER_H

#include "../Lexer/lexer.h"
#include "AST.h"

typedef struct Parser {
    struct Lexer *lexer;
    struct Token current_token;
} Parser;

void parser_init(Parser *parser, struct Lexer *lexer);
ASTNode* parser_parse_program(Parser *parser);

#endif