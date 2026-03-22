#include "parser.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "../Utils/hash.h"

// --- Хелперы ---

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

// Прототипы для рекурсивного спуска
ASTNode* parser_parse_expression(Parser *parser);
ASTNode* parser_parse_logical_and(Parser *parser);
ASTNode* parser_parse_comparison(Parser *parser);
ASTNode* parser_parse_equality(Parser *parser);
ASTNode* parser_parse_math(Parser *parser);
ASTNode* parser_parse_term(Parser *parser);
ASTNode* parser_parse_factor(Parser *parser);
ASTNode* parser_parse_statement(Parser *parser);
ASTNode* parser_parse_block(Parser *parser);

// --- Парсинг Выражений (Expression) ---

ASTNode* parser_parse_expression(Parser *parser) {
    ASTNode *node = parser_parse_logical_and(parser);

    while (parser->current_token.type == TOKEN_OR) {
        int op = parser->current_token.type;
        int line = parser->current_token.line;
        parser_eat(parser, TOKEN_OR);
        node = create_ast_binop(node, parser_parse_logical_and(parser), op, line);
    }

    // Присваивание =:
    if (parser->current_token.type == TOKEN_ASSIGN) {
        int line = parser->current_token.line;
        parser_eat(parser, TOKEN_ASSIGN);
        ASTNode *rhs = parser_parse_expression(parser);

        // x =: 5 -> Быстрое присваивание по хешу
        if (node->type == AST_IDENTIFIER) {
            // 🔥 ФИКС: Создаем узел присваивания...
            ASTNode *assign_node = create_ast_assign(node->data.identifier.name, node->data.identifier.id_hash, rhs, line);

            // 🔥 УБИВАЕМ ПУСТУЮ КОРОБКУ ОТ ИДЕНТИФИКАТОРА (ТЕ САМЫЕ 48 БАЙТ!)
            free(node);

            return assign_node;
        }

        // arr[0] =: 5 -> Присваивание по индексу
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

    // 1. Числа
    if (parser->current_token.type == TOKEN_TYPE_NUMBER) {
        double val = parser->current_token.double_value;
        parser_eat(parser, TOKEN_TYPE_NUMBER);
        node = create_ast_num(val, line);
    }
    // 2. Строки
    else if (parser->current_token.type == TOKEN_TYPE_STRING) {
        char *str = strndup(parser->current_token.start, parser->current_token.length);
        parser_eat(parser, TOKEN_TYPE_STRING);
        node = create_ast_string(str, line);
    }
    // 3. Идентификаторы и вызовы функций
    else if (parser->current_token.type == TOKEN_IDENTIFIER) {
        char *name = strndup(parser->current_token.start, parser->current_token.length);
        unsigned int name_hash = get_hash(name);
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
            node = create_ast_func_call(name, name_hash, args, a_count, line);
        } else {
            node = create_ast_ident(name, name_hash, line);
        }
    }
    // 4. Массивы []
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
    // 5. Группировка ()
    else if (parser->current_token.type == TOKEN_LPAREL) {
        parser_eat(parser, TOKEN_LPAREL);
        node = parser_parse_expression(parser);
        parser_eat(parser, TOKEN_RPAREL);
    }
    else {
        printf("\n[MKS Syntax Error] Line %d: Unexpected factor '%d'\n", line, parser->current_token.type);
        exit(1);
    }

    // ПОСТФИКСЫ: Индексы a[i] и Методы a.len()
    while (parser->current_token.type == TOKEN_LBRACKET || parser->current_token.type == TOKEN_DOT) {
        if (parser->current_token.type == TOKEN_LBRACKET) {
            parser_eat(parser, TOKEN_LBRACKET);
            ASTNode *index_expr = parser_parse_expression(parser);
            parser_eat(parser, TOKEN_RBRACKET);
            node = create_ast_index(node, index_expr, line);
        }
        else if (parser->current_token.type == TOKEN_DOT) {
            parser_eat(parser, TOKEN_DOT);
            char *m_name = strndup(parser->current_token.start, parser->current_token.length);
            unsigned int m_hash = get_hash(m_name);
            parser_eat(parser, TOKEN_IDENTIFIER);

            ASTNode **args = calloc(16, sizeof(ASTNode*));
            int count = 0;
            // Методы могут вызываться как свойства (без скобок) или как функции
            if (parser->current_token.type == TOKEN_LPAREL) {
                parser_eat(parser, TOKEN_LPAREL);
                if (parser->current_token.type != TOKEN_RPAREL) {
                    do {
                        if (count > 0) parser_eat(parser, TOKEN_COMMA);
                        args[count++] = parser_parse_expression(parser);
                    } while (parser->current_token.type == TOKEN_COMMA);
                }
                parser_eat(parser, TOKEN_RPAREL);
            }
            node = create_ast_method_call(node, m_name, m_hash, args, count, line);
        }
    }
    return node;
}

// --- Инструкции (Statements) ---

ASTNode* parser_parse_statement(Parser *parser) {
    enum TokenType type = parser->current_token.type;
    int line = parser->current_token.line;

    // using "lib" as alias;
    if (type == TOKEN_KW_USING) {
        parser_eat(parser, TOKEN_KW_USING);
        char *path = strndup(parser->current_token.start, parser->current_token.length);
        parser_eat(parser, TOKEN_TYPE_STRING);
        char *alias = NULL;
        if (parser->current_token.type == TOKEN_IDENTIFIER) {
            // Умная проверка на 'as'
            if (parser->current_token.length == 2 && strncmp(parser->current_token.start, "as", 2) == 0) {
                parser_advance(parser); // Пропускаем 'as'
                alias = strndup(parser->current_token.start, parser->current_token.length);
                parser_eat(parser, TOKEN_IDENTIFIER);
            }
        }
        parser_eat(parser, TOKEN_SEMICOLON);
        return create_ast_using(path, alias, line);
    }

    // var x =: 5;
    if (type == TOKEN_KW_VAR) {
        parser_eat(parser, TOKEN_KW_VAR);
        char *name = strndup(parser->current_token.start, parser->current_token.length);
        unsigned int v_hash = get_hash(name);
        parser_eat(parser, TOKEN_IDENTIFIER);
        parser_eat(parser, TOKEN_ASSIGN);
        ASTNode *val = parser_parse_expression(parser);
        parser_eat(parser, TOKEN_SEMICOLON);
        return create_ast_var_decl(val, line, name, v_hash);
    }

    // if (cond) { ... } else { ... }
    if (type == TOKEN_KW_IF) {
        parser_eat(parser, TOKEN_KW_IF);
        parser_eat(parser, TOKEN_LPAREL);
        ASTNode *cond = parser_parse_expression(parser);
        parser_eat(parser, TOKEN_RPAREL);
        ASTNode *body = parser_parse_block(parser);
        ASTNode *else_b = NULL;
        if (parser->current_token.type == TOKEN_KW_ELSE) {
            parser_eat(parser, TOKEN_KW_ELSE);
            // Поддержка else if
            else_b = (parser->current_token.type == TOKEN_KW_IF) ? parser_parse_statement(parser) : parser_parse_block(parser);
        }
        return create_if_block(cond, body, else_b, line);
    }

    // while (cond) { ... }
    if (type == TOKEN_KW_WHILE) {
        parser_eat(parser, TOKEN_KW_WHILE);
        parser_eat(parser, TOKEN_LPAREL);
        ASTNode *cond = parser_parse_expression(parser);
        parser_eat(parser, TOKEN_RPAREL);
        return create_while_block(cond, parser_parse_block(parser), line);
    }

    // for (init; cond; step) { ... }
    if (type == TOKEN_KW_FOR) {
        parser_eat(parser, TOKEN_KW_FOR);
        parser_eat(parser, TOKEN_LPAREL);

        ASTNode *init = NULL;
        if (parser->current_token.type != TOKEN_SEMICOLON) {
            if (parser->current_token.type == TOKEN_KW_VAR) {
                int v_line = parser->current_token.line;
                parser_eat(parser, TOKEN_KW_VAR);
                char *v_name = strndup(parser->current_token.start, parser->current_token.length);
                unsigned int v_hash = get_hash(v_name);
                parser_eat(parser, TOKEN_IDENTIFIER);
                parser_eat(parser, TOKEN_ASSIGN);
                ASTNode *v_val = parser_parse_expression(parser);
                init = create_ast_var_decl(v_val, v_line, v_name, v_hash);
            } else {
                init = parser_parse_expression(parser);
            }
        }
        parser_eat(parser, TOKEN_SEMICOLON);

        ASTNode *cond = (parser->current_token.type != TOKEN_SEMICOLON) ? parser_parse_expression(parser) : NULL;
        parser_eat(parser, TOKEN_SEMICOLON);

        ASTNode *step = (parser->current_token.type != TOKEN_RPAREL) ? parser_parse_expression(parser) : NULL;
        parser_eat(parser, TOKEN_RPAREL);

        return create_ast_for(init, cond, step, parser_parse_block(parser));
    }

    // Writeln(...) / Write(...)
    if (type == TOKEN_KW_WRITELN || type == TOKEN_KW_WRITE) {
        bool nl = (type == TOKEN_KW_WRITELN);
        parser_advance(parser);
        parser_eat(parser, TOKEN_LPAREL);
        ASTNode **args = calloc(16, sizeof(ASTNode*));
        int count = 0;
        if (parser->current_token.type != TOKEN_RPAREL) {
            do {
                if (count > 0) parser_eat(parser, TOKEN_COMMA);
                args[count++] = parser_parse_expression(parser);
            } while (parser->current_token.type == TOKEN_COMMA);
        }
        parser_eat(parser, TOKEN_RPAREL);
        parser_eat(parser, TOKEN_SEMICOLON);
        return create_ast_output(args, count, nl, line);
    }

    // fnc name(params) { ... }
    if (type == TOKEN_KW_FNC) {
        parser_eat(parser, TOKEN_KW_FNC);
        char *f_name = strndup(parser->current_token.start, parser->current_token.length);
        parser_eat(parser, TOKEN_IDENTIFIER);
        parser_eat(parser, TOKEN_LPAREL);
        char **params = calloc(10, sizeof(char*));
        int p_count = 0;
        if (parser->current_token.type != TOKEN_RPAREL) {
            do {
                if (p_count > 0) parser_eat(parser, TOKEN_COMMA);
                params[p_count++] = strndup(parser->current_token.start, parser->current_token.length);
                parser_eat(parser, TOKEN_IDENTIFIER);
            } while (parser->current_token.type == TOKEN_COMMA);
        }
        parser_eat(parser, TOKEN_RPAREL);
        return create_ast_func_decl(f_name, params, p_count, parser_parse_block(parser), line);
    }

    // return expr;
    if (type == TOKEN_KW_RETURN) {
        parser_eat(parser, TOKEN_KW_RETURN);
        ASTNode *val = parser_parse_expression(parser);
        parser_eat(parser, TOKEN_SEMICOLON);
        return create_ast_return(val, line);
    }

    // Обычное выражение как инструкция
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