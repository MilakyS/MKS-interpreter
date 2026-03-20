#include "parser.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

void parser_advance(Parser *parser) {
    if (parser->lexer != NULL) parser->current_token = lexer_next(parser->lexer);
}

void parser_eat(Parser *parser, const enum TokenType expected_type) {
    if (parser->current_token.type == expected_type) {
        parser_advance(parser);
    } else {
        printf("\n\033[1;31m[MKS Syntax Error]\033[0m\nLine %d: Unexpected token\n", parser->current_token.line);
        exit(1);
    }
}

ASTNode* parser_parse_expression(Parser *parser);
ASTNode* parser_parse_statement(Parser *parser);
ASTNode* parser_parse_block(Parser *parser);
ASTNode* parser_parse_math(Parser *parser);
ASTNode* parser_parse_term(Parser *parser);

ASTNode* parser_parse_factor(Parser *parser) {
    struct Token tok = parser->current_token;

    if (tok.type == TOKEN_TYPE_NUMBER) {
        int val = tok.int_value; int line = tok.line;
        parser_eat(parser, TOKEN_TYPE_NUMBER);
        return create_ast_num(val, line);
    }else if (tok.type == TOKEN_TYPE_STRING) {
        char *str = malloc(tok.length + 1);
        int j = 0;

        for (int i = 0; i < tok.length; i++) {
            if (tok.start[i] == '\\' && i + 1 < tok.length) {
                if (tok.start[i+1] == 'n') { str[j++] = '\n'; i++; }
                else if (tok.start[i+1] == 't') { str[j++] = '\t'; i++; }
                else if (tok.start[i+1] == '"') { str[j++] = '"'; i++; }
                else if (tok.start[i+1] == '\\') { str[j++] = '\\'; i++; }
                else { str[j++] = tok.start[i]; }
            } else {
                str[j++] = tok.start[i];
            }
        }
        str[j] = '\0';

        int line = tok.line;
        parser_eat(parser, TOKEN_TYPE_STRING);
        return create_ast_string(str, line);
    } else if (tok.type == TOKEN_IDENTIFIER) {
        char *name = strndup(tok.start, tok.length); int line = tok.line;
        parser_eat(parser, TOKEN_IDENTIFIER);
        return create_ast_ident(name, line);
    } else if (tok.type == TOKEN_KW_CALL) {
        int line = tok.line;
        parser_eat(parser, TOKEN_KW_CALL);
        char *name = strndup(parser->current_token.start, parser->current_token.length);
        parser_eat(parser, TOKEN_IDENTIFIER);
        parser_eat(parser, TOKEN_LPAREL);

        ASTNode **args = calloc(10, sizeof(ASTNode*));
        int a_count = 0;
        if (parser->current_token.type != TOKEN_RPAREL) {
            do {
                if (a_count > 0) parser_eat(parser, TOKEN_COMMA);
                args[a_count++] = parser_parse_expression(parser);
            } while (parser->current_token.type == TOKEN_COMMA);
        }
        parser_eat(parser, TOKEN_RPAREL);
        return create_ast_func_call(name, args, a_count, line);
    } else if (tok.type == TOKEN_LPAREL) {
        parser_eat(parser, TOKEN_LPAREL);
        ASTNode *node = parser_parse_expression(parser);
        parser_eat(parser, TOKEN_RPAREL);
        return node;
    }
    printf("\n[MKS Syntax Error] Line %d: Unexpected factor\n", tok.line);
    exit(1);
}

ASTNode* parser_parse_term(Parser *parser) {
    ASTNode *node = parser_parse_factor(parser);
    while (parser->current_token.type == TOKEN_STAR ||
           parser->current_token.type == TOKEN_SLASH ||
           parser->current_token.type == TOKEN_MOD) {
        enum TokenType op = parser->current_token.type;
        int line = parser->current_token.line;
        parser_eat(parser, op);
        node = create_ast_binop(node, parser_parse_factor(parser), op, line);
    }
    return node;
}

ASTNode* parser_parse_math(Parser *parser) {
    ASTNode *node = parser_parse_term(parser);
    while (parser->current_token.type == TOKEN_PLUS ||
           parser->current_token.type == TOKEN_MINUS) {
        enum TokenType op = parser->current_token.type;
        int line = parser->current_token.line;
        parser_eat(parser, op);
        node = create_ast_binop(node, parser_parse_term(parser), op, line);
    }
    return node;
}

ASTNode* parser_parse_expression(Parser *parser) {
    ASTNode *node = parser_parse_math(parser);
    while (parser->current_token.type == TOKEN_EQ ||
           parser->current_token.type == TOKEN_NOT_EQ) {
        enum TokenType op = parser->current_token.type;
        int line = parser->current_token.line;
        parser_eat(parser, op);
        node = create_ast_binop(node, parser_parse_math(parser), op, line);
    }
    return node;
}

ASTNode* parser_parse_statement(Parser *parser) {
    struct Token tok = parser->current_token;

    if (tok.type == TOKEN_KW_VAR) {
        int line = tok.line; parser_eat(parser, TOKEN_KW_VAR);
        char *name = strndup(parser->current_token.start, parser->current_token.length);
        parser_eat(parser, TOKEN_IDENTIFIER);
        parser_eat(parser, TOKEN_ASSIGN);
        ASTNode *value = parser_parse_expression(parser);
        parser_eat(parser, TOKEN_SEMICOLON);
        return create_ast_var_decl(value, line, name);
    } else if (tok.type == TOKEN_KW_IF) {
        int line = tok.line; parser_eat(parser, TOKEN_KW_IF); parser_eat(parser, TOKEN_LPAREL);
        ASTNode *condition = parser_parse_expression(parser); parser_eat(parser, TOKEN_RPAREL);
        ASTNode *if_body = parser_parse_block(parser);
        ASTNode *else_body = NULL;
        if (parser->current_token.type == TOKEN_KW_ELSE) {
            parser_eat(parser, TOKEN_KW_ELSE);
            if (parser->current_token.type == TOKEN_KW_IF) else_body = parser_parse_statement(parser);
            else else_body = parser_parse_block(parser);
        }
        return create_if_block(condition, if_body, else_body, line);
    } else if (tok.type == TOKEN_KW_WRITELN || tok.type == TOKEN_KW_WRITE) {
        int line = tok.line; bool is_newline = (tok.type == TOKEN_KW_WRITELN);
        parser_advance(parser); parser_eat(parser, TOKEN_LPAREL);
        ASTNode **args = calloc(16, sizeof(ASTNode*)); int count = 0;
        if (parser->current_token.type != TOKEN_RPAREL) {
            do {
                if (count > 0) parser_eat(parser, TOKEN_COMMA);
                args[count++] = parser_parse_expression(parser);
            } while (parser->current_token.type == TOKEN_COMMA);
        }
        parser_eat(parser, TOKEN_RPAREL); parser_eat(parser, TOKEN_SEMICOLON);
        return create_ast_output(args, count, is_newline, line);
    } else if (tok.type == TOKEN_KW_WHILE) {
        int line = tok.line; parser_eat(parser, TOKEN_KW_WHILE); parser_eat(parser, TOKEN_LPAREL);
        ASTNode *condition = parser_parse_expression(parser); parser_eat(parser, TOKEN_RPAREL);
        ASTNode *body = parser_parse_block(parser);
        return create_while_block(condition, body, line);
    } else if (tok.type == TOKEN_IDENTIFIER) {
        int line = tok.line; char *name = strndup(tok.start, tok.length);
        parser_eat(parser, TOKEN_IDENTIFIER); parser_eat(parser, TOKEN_ASSIGN);
        ASTNode *value = parser_parse_expression(parser); parser_eat(parser, TOKEN_SEMICOLON);
        return create_ast_assign(value, name, line);
    } else if (tok.type == TOKEN_KW_FNC) {
        int line = tok.line; parser_eat(parser, TOKEN_KW_FNC);
        char *name = strndup(parser->current_token.start, parser->current_token.length);
        parser_eat(parser, TOKEN_IDENTIFIER); parser_eat(parser, TOKEN_LPAREL);
        char **params = calloc(10, sizeof(char*)); int p_count = 0;
        if (parser->current_token.type != TOKEN_RPAREL) {
            do {
                if (p_count > 0) parser_eat(parser, TOKEN_COMMA);
                params[p_count++] = strndup(parser->current_token.start, parser->current_token.length);
                parser_eat(parser, TOKEN_IDENTIFIER);
            } while (parser->current_token.type == TOKEN_COMMA);
        }
        parser_eat(parser, TOKEN_RPAREL);
        ASTNode *body = parser_parse_block(parser);
        return create_ast_func_decl(name, params, p_count, body, line);
    } else if (tok.type == TOKEN_KW_RETURN) {
        int line = tok.line; parser_eat(parser, TOKEN_KW_RETURN);
        ASTNode *value = parser_parse_expression(parser); parser_eat(parser, TOKEN_SEMICOLON);
        return create_ast_return(value, line);
    } else {
        ASTNode *expr = parser_parse_expression(parser);
        if (parser->current_token.type == TOKEN_SEMICOLON) parser_eat(parser, TOKEN_SEMICOLON);
        return expr;
    }
}

ASTNode* parser_parse_block(Parser *parser) {
    int line = parser->current_token.line;
    parser_eat(parser, TOKEN_BLOCK_START);
    size_t capacity = 10; size_t count = 0;
    ASTNode **items = malloc(capacity * sizeof(ASTNode*));
    while (parser->current_token.type != TOKEN_EOF && parser->current_token.type != TOKEN_BLOCK_END) {
        items[count++] = parser_parse_statement(parser);
        if (count >= capacity) { capacity *= 2; items = realloc(items, capacity * sizeof(ASTNode*)); }
    }
    parser_eat(parser, TOKEN_BLOCK_END);
    return create_ast_block(items, (int)count, line);
}

ASTNode* parser_parse_program(Parser *parser) {
    size_t capacity = 20; size_t count = 0;
    ASTNode **items = malloc(capacity * sizeof(ASTNode*));
    while (parser->current_token.type != TOKEN_EOF) {
        items[count++] = parser_parse_statement(parser);
        if (count >= capacity) { capacity *= 2; items = realloc(items, capacity * sizeof(ASTNode*)); }
    }
    return create_ast_block(items, (int)count, 1);
}

void parser_init(Parser *parser, struct Lexer *lexer) {
    parser->lexer = lexer;
    parser->current_token = lexer_next(lexer);
}