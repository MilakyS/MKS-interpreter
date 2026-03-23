#include "parser.h"
#include <string.h>

static ASTNode* parser_parse_using(Parser *parser);
static ASTNode* parser_parse_var_decl(Parser *parser);
static ASTNode* parser_parse_if(Parser *parser);
static ASTNode* parser_parse_while(Parser *parser);
static ASTNode* parser_parse_for(Parser *parser);
static ASTNode* parser_parse_output(Parser *parser, bool newline);
static ASTNode* parser_parse_func_decl(Parser *parser);
static ASTNode* parser_parse_return(Parser *parser);

ASTNode* parser_parse_statement(Parser *parser) {
    const enum TokenType type = parser->current_token.type;

    switch (type) {
        case TOKEN_KW_USING:
            return parser_parse_using(parser);

        case TOKEN_KW_VAR:
            return parser_parse_var_decl(parser);

        case TOKEN_KW_IF:
            return parser_parse_if(parser);

        case TOKEN_KW_WHILE:
            return parser_parse_while(parser);

        case TOKEN_KW_FOR:
            return parser_parse_for(parser);

        case TOKEN_KW_WRITELN:
            return parser_parse_output(parser, true);

        case TOKEN_KW_WRITE:
            return parser_parse_output(parser, false);

        case TOKEN_KW_FNC:
            return parser_parse_func_decl(parser);

        case TOKEN_KW_RETURN:
            return parser_parse_return(parser);

        default: {
            ASTNode *expr = parser_parse_expression(parser);

            if (parser->current_token.type == TOKEN_SEMICOLON) {
                parser_eat(parser, TOKEN_SEMICOLON);
            }

            return expr;
        }
    }
}

ASTNode* parser_parse_block(Parser *parser) {
    const int line = parser->current_token.line;
    ASTNode **items = NULL;
    int count = 0;
    int cap = 0;

    parser_eat(parser, TOKEN_BLOCK_START);

    while (parser->current_token.type != TOKEN_EOF &&
           parser->current_token.type != TOKEN_BLOCK_END) {
        parser_push_ast(&items, &count, &cap, parser_parse_statement(parser));
    }

    parser_eat(parser, TOKEN_BLOCK_END);
    return create_ast_block(items, count, line);
}

ASTNode* parser_parse_program(Parser *parser) {
    ASTNode **items = NULL;
    int count = 0;
    int cap = 0;

    while (parser->current_token.type != TOKEN_EOF) {
        parser_push_ast(&items, &count, &cap, parser_parse_statement(parser));
    }

    return create_ast_block(items, count, 1);
}

static ASTNode* parser_parse_using(Parser *parser) {
    const int line = parser->current_token.line;
    char *path = NULL;
    char *alias = NULL;

    parser_eat(parser, TOKEN_KW_USING);

    if (parser->current_token.type != TOKEN_TYPE_STRING) {
        parser_error(parser, "Expected string path after 'using'");
    }

    path = mks_strndup(
        parser->current_token.start,
        (size_t)parser->current_token.length
    );
    parser_eat(parser, TOKEN_TYPE_STRING);

    if (parser_match_identifier(parser, "as")) {
        unsigned int ignored_hash = 0;
        alias = parser_take_identifier(parser, &ignored_hash);
    }

    parser_eat(parser, TOKEN_SEMICOLON);
    return create_ast_using(path, alias, line);
}

static ASTNode* parser_parse_var_decl(Parser *parser) {
    const int line = parser->current_token.line;
    unsigned int name_hash = 0;

    parser_eat(parser, TOKEN_KW_VAR);
    char *name = parser_take_identifier(parser, &name_hash);

    parser_eat(parser, TOKEN_ASSIGN);
    ASTNode *value = parser_parse_expression(parser);
    parser_eat(parser, TOKEN_SEMICOLON);

    return create_ast_var_decl(value, line, name, name_hash);
}

static ASTNode* parser_parse_if(Parser *parser) {
    const int line = parser->current_token.line;

    parser_eat(parser, TOKEN_KW_IF);
    parser_eat(parser, TOKEN_LPAREL);

    ASTNode *condition = parser_parse_expression(parser);

    parser_eat(parser, TOKEN_RPAREL);

    ASTNode *body = parser_parse_block(parser);
    ASTNode *else_body = NULL;

    if (parser->current_token.type == TOKEN_KW_ELSE) {
        parser_eat(parser, TOKEN_KW_ELSE);

        if (parser->current_token.type == TOKEN_KW_IF) {
            else_body = parser_parse_statement(parser);
        } else {
            else_body = parser_parse_block(parser);
        }
    }

    return create_if_block(condition, body, else_body, line);
}

static ASTNode* parser_parse_while(Parser *parser) {
    const int line = parser->current_token.line;

    parser_eat(parser, TOKEN_KW_WHILE);
    parser_eat(parser, TOKEN_LPAREL);

    ASTNode *condition = parser_parse_expression(parser);

    parser_eat(parser, TOKEN_RPAREL);

    ASTNode *body = parser_parse_block(parser);
    return create_while_block(condition, body, line);
}

static ASTNode* parser_parse_for(Parser *parser) {
    parser_eat(parser, TOKEN_KW_FOR);
    parser_eat(parser, TOKEN_LPAREL);

    ASTNode *init = NULL;
    ASTNode *condition = NULL;
    ASTNode *step = NULL;

    if (parser->current_token.type != TOKEN_SEMICOLON) {
        if (parser->current_token.type == TOKEN_KW_VAR) {
            const int v_line = parser->current_token.line;
            unsigned int v_hash = 0;

            parser_eat(parser, TOKEN_KW_VAR);
            char *v_name = parser_take_identifier(parser, &v_hash);

            parser_eat(parser, TOKEN_ASSIGN);
            ASTNode *v_value = parser_parse_expression(parser);

            init = create_ast_var_decl(v_value, v_line, v_name, v_hash);
        } else {
            init = parser_parse_expression(parser);
        }
    }
    parser_eat(parser, TOKEN_SEMICOLON);

    if (parser->current_token.type != TOKEN_SEMICOLON) {
        condition = parser_parse_expression(parser);
    }
    parser_eat(parser, TOKEN_SEMICOLON);

    if (parser->current_token.type != TOKEN_RPAREL) {
        step = parser_parse_expression(parser);
    }
    parser_eat(parser, TOKEN_RPAREL);

    return create_ast_for(init, condition, step, parser_parse_block(parser));
}

static ASTNode* parser_parse_output(Parser *parser, bool newline) {
    const int line = parser->current_token.line;
    ASTNode **args = NULL;
    int count = 0;
    int cap = 0;

    if (newline) {
        parser_eat(parser, TOKEN_KW_WRITELN);
    } else {
        parser_eat(parser, TOKEN_KW_WRITE);
    }

    parser_eat(parser, TOKEN_LPAREL);

    if (parser->current_token.type != TOKEN_RPAREL) {
        do {
            parser_push_ast(&args, &count, &cap, parser_parse_expression(parser));
        } while (parser_match(parser, TOKEN_COMMA));
    }

    parser_eat(parser, TOKEN_RPAREL);
    parser_eat(parser, TOKEN_SEMICOLON);

    return create_ast_output(args, count, newline, line);
}

static ASTNode* parser_parse_func_decl(Parser *parser) {
    const int line = parser->current_token.line;
    char **params = NULL;
    int param_count = 0;
    int param_cap = 0;

    parser_eat(parser, TOKEN_KW_FNC);

    unsigned int ignored_hash = 0;
    char *func_name = parser_take_identifier(parser, &ignored_hash);

    parser_eat(parser, TOKEN_LPAREL);

    if (parser->current_token.type != TOKEN_RPAREL) {
        do {
            unsigned int param_hash_ignored = 0;
            char *param = parser_take_identifier(parser, &param_hash_ignored);
            parser_push_str(&params, &param_count, &param_cap, param);
        } while (parser_match(parser, TOKEN_COMMA));
    }

    parser_eat(parser, TOKEN_RPAREL);

    ASTNode *body = parser_parse_block(parser);
    return create_ast_func_decl(func_name, params, param_count, body, line);
}

static ASTNode* parser_parse_return(Parser *parser) {
    const int line = parser->current_token.line;

    parser_eat(parser, TOKEN_KW_RETURN);
    ASTNode *value = parser_parse_expression(parser);
    parser_eat(parser, TOKEN_SEMICOLON);

    return create_ast_return(value, line);
}