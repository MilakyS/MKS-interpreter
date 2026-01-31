#ifndef CMINUSINTERPRETATOR_PARSER_H
#define CMINUSINTERPRETATOR_PARSER_H

#include "../Lexer/lexer.h"
#include "AST.h"


typedef struct {
    struct Lexer *lexer;
    struct Token *current_token;
}Parser;

void parser_init(Parser *parser, struct Lexer *lexer, struct Token *current_token);

ASTNode* parser_parse(Parser *parser);




#endif //CMINUSINTERPRETATOR_PARSER_H