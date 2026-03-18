#include "parser.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

void parser_advance(Parser *parser) {
    if (parser->current_token == NULL) {
        parser->current_token = malloc(sizeof(struct Token));
    }

    if (parser->lexer != NULL) {
        *parser->current_token = lexer_next(parser->lexer);
    }
}

void parser_eat(Parser *parser, int expected_type) {
    if (parser->current_token != NULL && parser->current_token->type == expected_type) {
        parser_advance(parser);
    } else {
        printf("Syntax Error at line %d: Expected token type %d, but got %d\n",
               parser->current_token->line, expected_type, parser->current_token->type);
        exit(1);
    }
}

ASTNode* parser_parse_expression(Parser *parser);
ASTNode* parser_parse_statement(Parser *parser);

ASTNode* parser_parse_factor(Parser *parser) {
    struct Token *tok = parser->current_token;

    if (tok->type == TOKEN_TYPE_NUMBER) {
        int val = tok->int_value;
        int line = tok->line;
        parser_eat(parser, TOKEN_TYPE_NUMBER);
        return create_ast_num(val, line);
    }
    else if (tok->type == TOKEN_TYPE_STRING) {
        char *str = strdup(tok->lexeme);
        int line = tok->line;
        parser_eat(parser, TOKEN_TYPE_STRING);
        return create_ast_string(str, line);
    }
    else if (tok->type == TOKEN_IDENTIFIER) {
        char *name = strdup(tok->lexeme);
        int line = tok->line;
        parser_eat(parser, TOKEN_IDENTIFIER);
        return create_ast_ident(name, line);
    }
    else if (tok->type == TOKEN_LPAREL) {
        parser_eat(parser, TOKEN_LPAREL);
        ASTNode *node = parser_parse_expression(parser);
        parser_eat(parser, TOKEN_RPAREL);
        return node;
    }

    printf("Syntax Error: expected NUMBER, IDENTIFIER or '(' at line %d\n", tok->line);
    exit(1);
    return NULL;
}

ASTNode* parser_parse_expression(Parser *parser) {
    ASTNode *node = parser_parse_factor(parser);

    while (parser->current_token != NULL &&
          (parser->current_token->type == TOKEN_PLUS || parser->current_token->type == TOKEN_MINUS)) {

        int op = parser->current_token->type;
        int line = parser->current_token->line;

        if (op == TOKEN_PLUS) parser_eat(parser, TOKEN_PLUS);
        else parser_eat(parser, TOKEN_MINUS);

        ASTNode *right = parser_parse_factor(parser);
        node = create_ast_binop(node, right, op, line);
    }
    return node;
}

ASTNode* parser_parse_statement(Parser *parser) {
    struct Token *tok = parser->current_token;

    // 1. Объявление переменной: var x =: 10;
    if (tok->type == TOKEN_KW_VAR) {
        int line = tok->line;
        parser_eat(parser, TOKEN_KW_VAR);

        char *name = strdup(parser->current_token->lexeme);
        parser_eat(parser, TOKEN_IDENTIFIER);

        parser_eat(parser, TOKEN_ASSIGN);

        ASTNode *value = parser_parse_expression(parser);
        parser_eat(parser, TOKEN_SEMICOLON);

        return create_ast_var_decl(value, line, name);
    }
    else if (tok->type == TOKEN_KW_WRITELN) {
        int line = tok->line;
        parser_eat(parser, TOKEN_KW_WRITELN);
        parser_eat(parser, TOKEN_LPAREL);

        ASTNode *expr = parser_parse_expression(parser);

        parser_eat(parser, TOKEN_RPAREL); // )
        parser_eat(parser, TOKEN_SEMICOLON); // ;

        return create_ast_writeln(expr, line);
    }

    else if (tok->type == TOKEN_IDENTIFIER) {
        int line = tok->line;
        char *name = strdup(tok->lexeme);
        parser_eat(parser, TOKEN_IDENTIFIER);

        parser_eat(parser, TOKEN_ASSIGN);

        ASTNode *value = parser_parse_expression(parser);
        parser_eat(parser, TOKEN_SEMICOLON);

        return create_ast_assign(value, name, line);
    }

    printf("Syntax Error: unexpected statement at line %d (Token Type: %d)\n", tok->line, tok->type);
    exit(1);
    return NULL;
}


void parser_init(Parser *parser, struct Lexer *lexer, struct Token *current_token) {
    parser->lexer = lexer;
    parser->current_token = current_token;

    if (parser->current_token == NULL) {
        parser_advance(parser);
    }
}

ASTNode* parser_parse(Parser *parser) {
    if (parser->current_token != NULL && parser->current_token->type != TOKEN_EOF) {
        return parser_parse_statement(parser);
    }
    return NULL;
}