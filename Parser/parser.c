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
        printf("\n\033[1;31m[MKS Syntax Error]\033[0m\nLine %d: Unexpected token (Expected %d, got %d)\n",
               parser->current_token.line, expected_type, parser->current_token.type);
        exit(1);
    }
}

enum TokenType peek_token_type(Parser *parser) {
    if (parser->lexer == NULL) return TOKEN_EOF;
    struct Lexer temp_lexer = *(parser->lexer);
    struct Token next_token = lexer_next(&temp_lexer);
    return next_token.type;
}

ASTNode* parser_parse_expression(Parser *parser);
ASTNode* parser_parse_logical_and(Parser *parser);
ASTNode* parser_parse_comparison(Parser *parser);
ASTNode* parser_parse_equality(Parser *parser);
ASTNode* parser_parse_math(Parser *parser);
ASTNode* parser_parse_term(Parser *parser);
ASTNode* parser_parse_factor(Parser *parser);
ASTNode* parser_parse_statement(Parser *parser);
ASTNode* parser_parse_block(Parser *parser);

ASTNode* parser_parse_expression(Parser *parser) {
    // 1. Сначала парсим всю логику/математику
    ASTNode *node = parser_parse_logical_and(parser);

    while (parser->current_token.type == TOKEN_OR) {
        int op = parser->current_token.type;
        int line = parser->current_token.line;
        parser_eat(parser, TOKEN_OR);
        node = create_ast_binop(node, parser_parse_logical_and(parser), op, line);
    }

    // 2. ФИКС: Если видим '=:', значит слева было имя или индекс, в который мы пишем
    if (parser->current_token.type == TOKEN_ASSIGN) {
        int line = parser->current_token.line;
        parser_eat(parser, TOKEN_ASSIGN);
        // Рекурсия для цепочек типа a =: b =: 5
        ASTNode *rhs = parser_parse_expression(parser);
        return create_ast_index_assign(node, rhs, line);
    }

    return node;
}

ASTNode* parser_parse_logical_and(Parser *parser) {
    ASTNode *node = parser_parse_comparison(parser);
    while (parser->current_token.type == TOKEN_AND) {
        int op = parser->current_token.type;
        int line = parser->current_token.line;
        parser_eat(parser, TOKEN_AND);
        node = create_ast_binop(node, parser_parse_comparison(parser), op, line);
    }
    return node;
}

ASTNode* parser_parse_comparison(Parser *parser) {
    ASTNode *node = parser_parse_equality(parser);
    while (parser->current_token.type == TOKEN_LESS || parser->current_token.type == TOKEN_GREATER ||
           parser->current_token.type == TOKEN_LESS_EQUAL || parser->current_token.type == TOKEN_GREATER_EQUAL) {
        int op = parser->current_token.type;
        int line = parser->current_token.line;
        parser_eat(parser, op);
        node = create_ast_binop(node, parser_parse_equality(parser), op, line);
    }
    return node;
}

ASTNode* parser_parse_equality(Parser *parser) {
    ASTNode *node = parser_parse_math(parser);
    while (parser->current_token.type == TOKEN_EQ || parser->current_token.type == TOKEN_NOT_EQ) {
        int op = parser->current_token.type;
        int line = parser->current_token.line;
        parser_eat(parser, op);
        node = create_ast_binop(node, parser_parse_math(parser), op, line);
    }
    return node;
}

ASTNode* parser_parse_math(Parser *parser) {
    ASTNode *node = parser_parse_term(parser);
    while (parser->current_token.type == TOKEN_PLUS || parser->current_token.type == TOKEN_MINUS) {
        int op = parser->current_token.type;
        int line = parser->current_token.line;
        parser_eat(parser, op);
        node = create_ast_binop(node, parser_parse_term(parser), op, line);
    }
    return node;
}

ASTNode* parser_parse_term(Parser *parser) {
    ASTNode *node = parser_parse_factor(parser);
    while (parser->current_token.type == TOKEN_STAR || parser->current_token.type == TOKEN_SLASH || parser->current_token.type == TOKEN_MOD) {
        int op = parser->current_token.type;
        int line = parser->current_token.line;
        parser_eat(parser, op);
        node = create_ast_binop(node, parser_parse_factor(parser), op, line);
    }
    return node;
}

ASTNode* parser_parse_factor(Parser *parser) {
    ASTNode *node = NULL;
    int line = parser->current_token.line;

    if (parser->current_token.type == TOKEN_TYPE_NUMBER) {
        double val = parser->current_token.double_value;
        parser_eat(parser, TOKEN_TYPE_NUMBER);
        node = create_ast_num(val, line);
    }
    else if (parser->current_token.type == TOKEN_TYPE_STRING) {
        char *str = strndup(parser->current_token.start, parser->current_token.length);
        parser_eat(parser, TOKEN_TYPE_STRING);
        node = create_ast_string(str, line);
    }

    else if (parser->current_token.type == TOKEN_IDENTIFIER) {
        char *name = strndup(parser->current_token.start, parser->current_token.length);
        parser_eat(parser, TOKEN_IDENTIFIER);

        if (parser->current_token.type == TOKEN_LPAREL) {
            parser_eat(parser, TOKEN_LPAREL);
            ASTNode **args = calloc(16, sizeof(ASTNode*));
            int a_count = 0;
            if (parser->current_token.type != TOKEN_RPAREL) {
                do {
                    if (a_count > 0) parser_eat(parser, TOKEN_COMMA);
                    args[a_count++] = parser_parse_expression(parser);
                } while (parser->current_token.type == TOKEN_COMMA);
            }
            parser_eat(parser, TOKEN_RPAREL);
            node = create_ast_func_call(name, args, a_count, line);
        } else {
            node = create_ast_ident(name, line);
        }
    }
    else if (parser->current_token.type == TOKEN_LBRACKET) {
        parser_eat(parser, TOKEN_LBRACKET);
        size_t cap = 10, count = 0;
        ASTNode **elements = malloc(cap * sizeof(ASTNode*));
        if (parser->current_token.type != TOKEN_RBRACKET) {
            do {
                if (count > 0) parser_eat(parser, TOKEN_COMMA);
                if (count >= cap) { cap *= 2; elements = realloc(elements, cap * sizeof(ASTNode*)); }
                elements[count++] = parser_parse_expression(parser);
            } while (parser->current_token.type == TOKEN_COMMA);
        }
        parser_eat(parser, TOKEN_RBRACKET);
        node = create_ast_array(elements, (int)count, line);
    }
    else if (parser->current_token.type == TOKEN_KW_CALL) {
        parser_eat(parser, TOKEN_KW_CALL);
        char *name = strndup(parser->current_token.start, parser->current_token.length);
        parser_eat(parser, TOKEN_IDENTIFIER);
        parser_eat(parser, TOKEN_LPAREL);
        ASTNode **args = calloc(16, sizeof(ASTNode*));
        int a_count = 0;
        if (parser->current_token.type != TOKEN_RPAREL) {
            do {
                if (a_count > 0) parser_eat(parser, TOKEN_COMMA);
                args[a_count++] = parser_parse_expression(parser);
            } while (parser->current_token.type == TOKEN_COMMA);
        }
        parser_eat(parser, TOKEN_RPAREL);
        node = create_ast_func_call(name, args, a_count, line);
    }
    else if (parser->current_token.type == TOKEN_LPAREL) {
        parser_eat(parser, TOKEN_LPAREL);
        node = parser_parse_expression(parser);
        parser_eat(parser, TOKEN_RPAREL);
    }
    else {
        printf("\n[MKS Syntax Error] Line %d: Unexpected factor '%d'\n", line, parser->current_token.type);
        exit(1);
    }

    // ПОСТФИКСЫ: Индексы и Методы
    while (parser->current_token.type == TOKEN_LBRACKET || parser->current_token.type == TOKEN_DOT) {
        if (parser->current_token.type == TOKEN_LBRACKET) {
            parser_eat(parser, TOKEN_LBRACKET);
            ASTNode *index_expr = parser_parse_expression(parser);
            parser_eat(parser, TOKEN_RBRACKET);
            node = create_ast_index(node, index_expr, line);
        }
        else if (parser->current_token.type == TOKEN_DOT) {
            parser_eat(parser, TOKEN_DOT);
            char *name = strndup(parser->current_token.start, parser->current_token.length);
            parser_eat(parser, TOKEN_IDENTIFIER);
            ASTNode **args = calloc(16, sizeof(ASTNode*));
            int count = 0;
            if (parser->current_token.type == TOKEN_LPAREL) {
                parser_eat(parser, TOKEN_LPAREL);
                if (parser->current_token.type != TOKEN_RPAREL) {
                    do { if (count > 0) parser_eat(parser, TOKEN_COMMA); args[count++] = parser_parse_expression(parser); } while (parser->current_token.type == TOKEN_COMMA);
                }
                parser_eat(parser, TOKEN_RPAREL);
            }
            node = create_ast_method_call(node, name, args, count, line);
        }
    }
    return node;
}

// --- Инструкции (Statements) ---

ASTNode* parser_parse_statement(Parser *parser) {
    enum TokenType type = parser->current_token.type;
    int line = parser->current_token.line;

    if (type == TOKEN_KW_USING) {
        parser_eat(parser, TOKEN_KW_USING);
        char *path = strndup(parser->current_token.start, parser->current_token.length);
        parser_eat(parser, TOKEN_TYPE_STRING);
        char *alias = NULL;
        if (parser->current_token.type == TOKEN_IDENTIFIER && strncmp(parser->current_token.start, "as", 2) == 0) {
            parser_advance(parser);
            alias = strndup(parser->current_token.start, parser->current_token.length);
            parser_eat(parser, TOKEN_IDENTIFIER);
        }
        parser_eat(parser, TOKEN_SEMICOLON);
        return create_ast_using(path, alias, line);
    }

    if (type == TOKEN_KW_VAR) {
        parser_eat(parser, TOKEN_KW_VAR);
        char *name = strndup(parser->current_token.start, parser->current_token.length);
        parser_eat(parser, TOKEN_IDENTIFIER);
        parser_eat(parser, TOKEN_ASSIGN);
        ASTNode *val = parser_parse_expression(parser);
        parser_eat(parser, TOKEN_SEMICOLON);
        return create_ast_var_decl(val, line, name);
    }

    if (type == TOKEN_KW_IF) {
        parser_eat(parser, TOKEN_KW_IF);
        parser_eat(parser, TOKEN_LPAREL);
        ASTNode *cond = parser_parse_expression(parser);
        parser_eat(parser, TOKEN_RPAREL);
        ASTNode *body = parser_parse_block(parser);
        ASTNode *else_b = NULL;
        if (parser->current_token.type == TOKEN_KW_ELSE) {
            parser_eat(parser, TOKEN_KW_ELSE);
            else_b = (parser->current_token.type == TOKEN_KW_IF) ? parser_parse_statement(parser) : parser_parse_block(parser);
        }
        return create_if_block(cond, body, else_b, line);
    }

    if (type == TOKEN_KW_WHILE) {
        parser_eat(parser, TOKEN_KW_WHILE);
        parser_eat(parser, TOKEN_LPAREL);
        ASTNode *cond = parser_parse_expression(parser);
        parser_eat(parser, TOKEN_RPAREL);
        return create_while_block(cond, parser_parse_block(parser), line);
    }

    if (type == TOKEN_KW_FOR) {
        parser_eat(parser, TOKEN_KW_FOR);
        parser_eat(parser, TOKEN_LPAREL);

        ASTNode *init = NULL;
        if (parser->current_token.type != TOKEN_SEMICOLON) {
            if (parser->current_token.type == TOKEN_KW_VAR) {
                // Парсим VAR вручную, чтобы НЕ ЕСТЬ ';' в конце
                int v_line = parser->current_token.line;
                parser_eat(parser, TOKEN_KW_VAR);
                char *v_name = strndup(parser->current_token.start, parser->current_token.length);
                parser_eat(parser, TOKEN_IDENTIFIER);
                parser_eat(parser, TOKEN_ASSIGN);
                ASTNode *v_val = parser_parse_expression(parser);
                init = create_ast_var_decl(v_val, v_line, v_name);
            } else {
                init = parser_parse_expression(parser);
            }
        }
        parser_eat(parser, TOKEN_SEMICOLON); // Теперь это съест нужную точку с запятой

        ASTNode *cond = NULL;
        if (parser->current_token.type != TOKEN_SEMICOLON) {
            cond = parser_parse_expression(parser);
        }
        parser_eat(parser, TOKEN_SEMICOLON);

        ASTNode *step = NULL;
        if (parser->current_token.type != TOKEN_RPAREL) {
            step = parser_parse_expression(parser);
        }
        parser_eat(parser, TOKEN_RPAREL);

        ASTNode *body = parser_parse_block(parser);
        return create_ast_for(init, cond, step, body);
    }
    if (type == TOKEN_KW_WRITELN || type == TOKEN_KW_WRITE) {
        bool nl = (type == TOKEN_KW_WRITELN);
        parser_advance(parser); parser_eat(parser, TOKEN_LPAREL);
        ASTNode **args = calloc(16, sizeof(ASTNode*)); int count = 0;
        if (parser->current_token.type != TOKEN_RPAREL) {
            do { if (count > 0) parser_eat(parser, TOKEN_COMMA); args[count++] = parser_parse_expression(parser); } while (parser->current_token.type == TOKEN_COMMA);
        }
        parser_eat(parser, TOKEN_RPAREL); parser_eat(parser, TOKEN_SEMICOLON);
        return create_ast_output(args, count, nl, line);
    }

    if (type == TOKEN_KW_FNC) {
        parser_eat(parser, TOKEN_KW_FNC);
        char *name = strndup(parser->current_token.start, parser->current_token.length);
        parser_eat(parser, TOKEN_IDENTIFIER); parser_eat(parser, TOKEN_LPAREL);
        char **params = calloc(10, sizeof(char*)); int p_count = 0;
        if (parser->current_token.type != TOKEN_RPAREL) {
            do { if (p_count > 0) parser_eat(parser, TOKEN_COMMA); params[p_count++] = strndup(parser->current_token.start, parser->current_token.length); parser_eat(parser, TOKEN_IDENTIFIER); } while (parser->current_token.type == TOKEN_COMMA);
        }
        parser_eat(parser, TOKEN_RPAREL);
        return create_ast_func_decl(name, params, p_count, parser_parse_block(parser), line);
    }

    if (type == TOKEN_KW_RETURN) {
        parser_eat(parser, TOKEN_KW_RETURN);
        ASTNode *val = parser_parse_expression(parser); parser_eat(parser, TOKEN_SEMICOLON);
        return create_ast_return(val, line);
    }
    ASTNode *expr = parser_parse_expression(parser);
    if (parser->current_token.type == TOKEN_SEMICOLON) parser_eat(parser, TOKEN_SEMICOLON);
    return expr;
}

ASTNode* parser_parse_block(Parser *parser) {
    int line = parser->current_token.line;
    parser_eat(parser, TOKEN_BLOCK_START);
    size_t cap = 10, count = 0;
    ASTNode **items = malloc(cap * sizeof(ASTNode*));
    while (parser->current_token.type != TOKEN_EOF && parser->current_token.type != TOKEN_BLOCK_END) {
        items[count++] = parser_parse_statement(parser);
        if (count >= cap) { cap *= 2; items = realloc(items, cap * sizeof(ASTNode*)); }
    }
    parser_eat(parser, TOKEN_BLOCK_END);
    return create_ast_block(items, (int)count, line);
}

ASTNode* parser_parse_program(Parser *parser) {
    size_t cap = 20, count = 0;
    ASTNode **items = malloc(cap * sizeof(ASTNode*));
    while (parser->current_token.type != TOKEN_EOF) {
        items[count++] = parser_parse_statement(parser);
        if (count >= cap) { cap *= 2; items = realloc(items, cap * sizeof(ASTNode*)); }
    }
    return create_ast_block(items, (int)count, 1);
}

void parser_init(Parser *parser, struct Lexer *lexer) {
    parser->lexer = lexer;
    parser->current_token = lexer_next(lexer);
}