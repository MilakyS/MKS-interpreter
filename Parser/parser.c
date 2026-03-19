#include "parser.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "../main.h"

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
ASTNode* parser_parse_block(Parser *parser);

ASTNode* parser_parse_factor(Parser *parser) {
    const struct Token *tok = parser->current_token;

    if (tok->type == TOKEN_TYPE_NUMBER) {
        const int val = tok->int_value;
        const int line = tok->line;
        parser_eat(parser, TOKEN_TYPE_NUMBER);
        return create_ast_num(val, line);
    }
    else if (tok->type == TOKEN_TYPE_STRING) {
        const char *str = strdup(tok->lexeme);
        const int line = tok->line;
        parser_eat(parser, TOKEN_TYPE_STRING);
        return create_ast_string(str, line);
    }
    else if (tok->type == TOKEN_IDENTIFIER) {
        const char *name = strdup(tok->lexeme);
        const int line = tok->line;
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
          (parser->current_token->type == TOKEN_PLUS ||
           parser->current_token->type == TOKEN_MINUS ||
           parser->current_token->type == TOKEN_EQ ||
           parser->current_token->type == TOKEN_NOT_EQ)) {

        int op = parser->current_token->type;
        int line = parser->current_token->line;

        parser_eat(parser, op);

        ASTNode *right = parser_parse_factor(parser);
        node = create_ast_binop(node, right, op, line);
           }
    return node;
}

ASTNode* parser_parse_statement(Parser *parser) {
    const struct Token *tok = parser->current_token;

    // 1. Объявление переменной: var x =: 10;
    if (tok->type == TOKEN_KW_VAR) {
        const int line = tok->line;
        parser_eat(parser, TOKEN_KW_VAR);

        const char *name = strdup(parser->current_token->lexeme);
        parser_eat(parser, TOKEN_IDENTIFIER);

        parser_eat(parser, TOKEN_ASSIGN);

        ASTNode *value = parser_parse_expression(parser);
        parser_eat(parser, TOKEN_SEMICOLON);

        return create_ast_var_decl(value, line, name);
    }
    else if (tok->type == TOKEN_KW_IF) {
        int line = tok->line;
        parser_eat(parser, TOKEN_KW_IF);

        parser_eat(parser, TOKEN_LPAREL); // (
        ASTNode *condition = parser_parse_expression(parser);
        parser_eat(parser, TOKEN_RPAREL); // )

        ASTNode *if_body = parser_parse_block(parser);
        ASTNode *else_body = NULL;

        if (parser->current_token != NULL && parser->current_token->type == TOKEN_KW_ELSE) {
            parser_eat(parser, TOKEN_KW_ELSE);
            if (parser->current_token->type == TOKEN_KW_IF) {
                else_body = parser_parse_statement(parser);
            } else {
                else_body = parser_parse_block(parser);
            }
        }

        return create_if_block(condition, if_body, else_body, line);
    }
    else if (tok->type == TOKEN_KW_WRITELN) {
        const int line = tok->line;
        parser_eat(parser, TOKEN_KW_WRITELN);
        parser_eat(parser, TOKEN_LPAREL);

        ASTNode *expr = parser_parse_expression(parser);

        parser_eat(parser, TOKEN_RPAREL); // )
        parser_eat(parser, TOKEN_SEMICOLON); // ;

        return create_ast_writeln(expr, line);
    }
    else if (tok->type == TOKEN_KW_WHILE) {
        const int line = tok->line;
        parser_eat(parser, TOKEN_KW_WHILE);
        parser_eat(parser, TOKEN_LPAREL);

        ASTNode *condition = parser_parse_expression(parser);
        parser_eat(parser, TOKEN_RPAREL);

        ASTNode *body = parser_parse_block(parser);
        return create_while_block(condition, body, line);
    }

    else if (tok->type == TOKEN_IDENTIFIER) {
        const int line = tok->line;
        const char *name = strdup(tok->lexeme);
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
ASTNode* parser_parse_block(Parser *parser) {
    int line = parser->current_token->line;
    parser_eat(parser, TOKEN_BLOCK_START);

    int capacity = 10;
    int count = 0;
    ASTNode **items = malloc(capacity * sizeof(ASTNode*));

    while (parser->current_token != NULL && parser->current_token->type != TOKEN_BLOCK_END) {
        items[count++] = parser_parse_statement(parser);

        if (count >= capacity) {
            capacity *= 2;
            items = realloc(items, capacity * sizeof(ASTNode*));
        }
    }
    parser_eat(parser, TOKEN_BLOCK_END);
    return create_ast_block(items, count, line);
}



void parser_init(Parser *parser, struct Lexer *lexer, struct Token *current_token) {
    if (debug_mode == true) {
        struct Token t;
        do {
            t = lexer_next(lexer);
            printf("Token: type=%d, lexeme='%s' on line %d\n", t.type, t.lexeme ? t.lexeme : "NULL", t.line);
        } while (t.type != TOKEN_EOF && t.type != TOKEN_ERROR);
    }

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