#include "parser.h"
#include <stdlib.h>
#include <stdbool.h>

static ASTNode* parser_parse_assignment(Parser *parser);
static ASTNode* parser_parse_logical_or(Parser *parser);
static ASTNode* parser_parse_logical_and(Parser *parser);
static ASTNode* parser_parse_comparison(Parser *parser);
static ASTNode* parser_parse_equality(Parser *parser);
static ASTNode* parser_parse_math(Parser *parser);
static ASTNode* parser_parse_term(Parser *parser);
static ASTNode* parser_parse_unary(Parser *parser);
static ASTNode* parser_parse_primary(Parser *parser);
static ASTNode* parser_parse_postfix(Parser *parser, ASTNode *node);

static int parser_current_col(Parser *parser) {
    const char *start = parser->current_token.start;
    if (start == NULL || parser->lexer == NULL || parser->lexer->source == NULL) {
        return 1;
    }

    const char *p = start;
    while (p > parser->lexer->source && p[-1] != '\n') {
        p--;
    }

    return (int)(start - p) + 1;
}

ASTNode* parser_parse_expression(Parser *parser) {
    return parser_parse_assignment(parser);
}

static ASTNode* parser_parse_assignment(Parser *parser) {
    ASTNode *node = parser_parse_logical_or(parser);

    if (parser->current_token.type == TOKEN_ASSIGN || parser->current_token.type == TOKEN_SWAP) {
        const int line = parser->current_token.line;
        const int col = parser_current_col(parser);
        int is_swap = (parser->current_token.type == TOKEN_SWAP);
        parser_eat(parser, parser->current_token.type);

        ASTNode *rhs = parser_parse_assignment(parser);

        if (is_swap) {
            return create_ast_swap(node, rhs, line, col);
        }

        if (node->type == AST_IDENTIFIER) {
            ASTNode *assign_node = create_ast_assign(
                node->data.identifier.name,
                node->data.identifier.id_hash,
                rhs,
                line,
                col
            );

            free(node);
            return assign_node;
        }

        if (node->type == AST_INDEX) {
            return create_ast_index_assign(node, rhs, line, col);
        }

        if (node->type == AST_DEREF) {
            ASTNode *target = node->data.deref.target;
            free(node);
            return create_ast_deref_assign(target, rhs, line, col);
        }

        if (node->type == AST_OBJ_GET) {
            return create_ast_obj_set(node->data.obj_get.object,
                                      node->data.obj_get.field,
                                      node->data.obj_get.field_hash,
                                      rhs,
                                      line,
                                      col);
        }

        parser_error(parser, "Invalid assignment target");
    }

    return node;
}

static ASTNode* parser_parse_logical_or(Parser *parser) {
    ASTNode *node = parser_parse_logical_and(parser);

    while (parser->current_token.type == TOKEN_OR) {
        const int op = parser->current_token.type;
        const int line = parser->current_token.line;
        const int col = parser_current_col(parser);
        parser_eat(parser, TOKEN_OR);
        node = create_ast_binop(node, parser_parse_logical_and(parser), op, line, col);
    }

    return node;
}

static ASTNode* parser_parse_logical_and(Parser *parser) {
    ASTNode *node = parser_parse_comparison(parser);

    while (parser->current_token.type == TOKEN_AND) {
        const int op = parser->current_token.type;
        const int line = parser->current_token.line;
        const int col = parser_current_col(parser);
        parser_eat(parser, TOKEN_AND);
        node = create_ast_binop(node, parser_parse_comparison(parser), op, line, col);
    }

    return node;
}

static ASTNode* parser_parse_comparison(Parser *parser) {
    ASTNode *node = parser_parse_equality(parser);

    while (parser->current_token.type == TOKEN_LESS ||
           parser->current_token.type == TOKEN_GREATER ||
           parser->current_token.type == TOKEN_LESS_EQUAL ||
           parser->current_token.type == TOKEN_GREATER_EQUAL) {

        const int op = parser->current_token.type;
        const int line = parser->current_token.line;
        const int col = parser_current_col(parser);
        parser_eat(parser, op);
        node = create_ast_binop(node, parser_parse_equality(parser), op, line, col);
    }

    return node;
}

static ASTNode* parser_parse_equality(Parser *parser) {
    ASTNode *node = parser_parse_math(parser);

    while (parser->current_token.type == TOKEN_EQ ||
           parser->current_token.type == TOKEN_NOT_EQ) {

        const int op = parser->current_token.type;
        const int line = parser->current_token.line;
        const int col = parser_current_col(parser);
        parser_eat(parser, op);
        node = create_ast_binop(node, parser_parse_math(parser), op, line, col);
    }

    return node;
}

static ASTNode* parser_parse_math(Parser *parser) {
    ASTNode *node = parser_parse_term(parser);

    while (parser->current_token.type == TOKEN_PLUS ||
           parser->current_token.type == TOKEN_MINUS) {

        const int op = parser->current_token.type;
        const int line = parser->current_token.line;
        const int col = parser_current_col(parser);
        parser_eat(parser, op);
        node = create_ast_binop(node, parser_parse_term(parser), op, line, col);
    }

    return node;
}

static ASTNode* parser_parse_term(Parser *parser) {
    ASTNode *node = parser_parse_unary(parser);

    while (parser->current_token.type == TOKEN_STAR ||
           parser->current_token.type == TOKEN_SLASH ||
           parser->current_token.type == TOKEN_MOD) {

        const int op = parser->current_token.type;
        const int line = parser->current_token.line;
        const int col = parser_current_col(parser);
        parser_eat(parser, op);
        node = create_ast_binop(node, parser_parse_unary(parser), op, line, col);
    }

    return node;
}

static ASTNode* parser_parse_unary(Parser *parser) {
    if (parser->current_token.type == TOKEN_MINUS) {
        const int line = parser->current_token.line;
        const int col = parser_current_col(parser);
        parser_eat(parser, TOKEN_MINUS);
        ASTNode *right = parser_parse_unary(parser);
        ASTNode *zero = create_ast_num(0, line, col);
        return create_ast_binop(zero, right, TOKEN_MINUS, line, col);
    }

    if (parser->current_token.type == TOKEN_PLUS) {
        parser_eat(parser, TOKEN_PLUS);
        return parser_parse_unary(parser);
    }

    if (parser->current_token.type == TOKEN_AMPERSAND) {
        const int line = parser->current_token.line;
        const int col = parser_current_col(parser);
        parser_eat(parser, TOKEN_AMPERSAND);
        return create_ast_address_of(parser_parse_unary(parser), line, col);
    }

    if (parser->current_token.type == TOKEN_STAR) {
        const int line = parser->current_token.line;
        const int col = parser_current_col(parser);
        parser_eat(parser, TOKEN_STAR);
        return create_ast_deref(parser_parse_unary(parser), line, col);
    }

    return parser_parse_primary(parser);
}

static ASTNode* parser_parse_primary(Parser *parser) {
    ASTNode *node = NULL;
    const int line = parser->current_token.line;

    if (parser->current_token.type == TOKEN_TYPE_NUMBER) {
        const int is_float = parser->current_token.is_float;
        const int64_t int_value = parser->current_token.int_value;
        const double float_value = parser->current_token.double_value;
        const int col = parser_current_col(parser);
        parser_eat(parser, TOKEN_TYPE_NUMBER);
        node = is_float
            ? create_ast_float(float_value, line, col)
            : create_ast_int(int_value, line, col);
        return parser_parse_postfix(parser, node);
    }

    if (parser->current_token.type == TOKEN_TYPE_STRING) {
        char *str = mks_strndup(
            parser->current_token.start,
            (size_t)parser->current_token.length
        );
        const int col = parser_current_col(parser);
        parser_eat(parser, TOKEN_TYPE_STRING);
        node = create_ast_string(str, line, col);
        return parser_parse_postfix(parser, node);
    }

    if (parser->current_token.type == TOKEN_KW_NULL) {
        const int col = parser_current_col(parser);
        parser_eat(parser, TOKEN_KW_NULL);
        node = create_ast_null(line, col);
        return parser_parse_postfix(parser, node);
    }

    if (parser->current_token.type == TOKEN_KW_TRUE) {
        const int col = parser_current_col(parser);
        parser_eat(parser, TOKEN_KW_TRUE);
        node = create_ast_bool(true, line, col);
        return parser_parse_postfix(parser, node);
    }

    if (parser->current_token.type == TOKEN_KW_FALSE) {
        const int col = parser_current_col(parser);
        parser_eat(parser, TOKEN_KW_FALSE);
        node = create_ast_bool(false, line, col);
        return parser_parse_postfix(parser, node);
    }

    if (parser->current_token.type == TOKEN_IDENTIFIER) {
        unsigned int name_hash = 0;
        char *name = parser_take_identifier(parser, &name_hash);

        if (parser->current_token.type == TOKEN_LPAREL) {
            ASTNode **args = NULL;
            int arg_count = 0;
            int arg_cap = 0;
            const int col = parser_current_col(parser);

            parser_eat(parser, TOKEN_LPAREL);

            if (parser->current_token.type != TOKEN_RPAREL) {
                do {
                    ASTNode *arg = parser_parse_expression(parser);
                    parser_push_ast(&args, &arg_count, &arg_cap, arg);
                } while (parser_match(parser, TOKEN_COMMA));
            }

            parser_eat(parser, TOKEN_RPAREL);
            node = create_ast_func_call(name, name_hash, args, arg_count, line, col);
        } else {
            node = create_ast_ident(name, name_hash, line, parser_current_col(parser));
        }

        return parser_parse_postfix(parser, node);
    }

    if (parser->current_token.type == TOKEN_LBRACKET) {
        ASTNode **elements = NULL;
        int count = 0;
        int cap = 0;

        parser_eat(parser, TOKEN_LBRACKET);

        if (parser->current_token.type != TOKEN_RBRACKET) {
            do {
                ASTNode *elem = parser_parse_expression(parser);
                parser_push_ast(&elements, &count, &cap, elem);
            } while (parser_match(parser, TOKEN_COMMA));
        }

        parser_eat(parser, TOKEN_RBRACKET);
        node = create_ast_array(elements, count, line, parser_current_col(parser));
        return parser_parse_postfix(parser, node);
    }

    if (parser->current_token.type == TOKEN_LPAREL) {
        parser_eat(parser, TOKEN_LPAREL);
        node = parser_parse_expression(parser);
        parser_eat(parser, TOKEN_RPAREL);
        return parser_parse_postfix(parser, node);
    }

    parser_error(parser, "Unexpected token in expression");
    return NULL;
}

static ASTNode* parser_parse_postfix(Parser *parser, ASTNode *node) {
    while (parser->current_token.type == TOKEN_LBRACKET ||
           parser->current_token.type == TOKEN_DOT ||
           parser->current_token.type == TOKEN_INCREMENT ||
           parser->current_token.type == TOKEN_DECREMENT) {

        if (parser->current_token.type == TOKEN_LBRACKET) {
            const int line = parser->current_token.line;
            const int col = parser_current_col(parser);
            parser_eat(parser, TOKEN_LBRACKET);
            ASTNode *index_expr = parser_parse_expression(parser);
            parser_eat(parser, TOKEN_RBRACKET);
            node = create_ast_index(node, index_expr, line, col);
            continue;
        }

        if (parser->current_token.type == TOKEN_INCREMENT) {
            const int line = parser->current_token.line;
            const int col = parser_current_col(parser);
            parser_eat(parser, TOKEN_INCREMENT);
            node = create_ast_inc_op(node, 0, line, col);
            continue;
        }

        if (parser->current_token.type == TOKEN_DECREMENT) {
            const int line = parser->current_token.line;
            const int col = parser_current_col(parser);
            parser_eat(parser, TOKEN_DECREMENT);
            node = create_ast_inc_op(node, 1, line, col);
            continue;
        }

        if (parser->current_token.type == TOKEN_DOT) {
            const int line = parser->current_token.line;
            const int col = parser_current_col(parser);
            parser_eat(parser, TOKEN_DOT);

            unsigned int method_hash = 0;
            char *method_name = parser_take_identifier(parser, &method_hash);

            ASTNode **args = NULL;
            int arg_count = 0;
            int arg_cap = 0;

            bool had_parens = false;
            if (parser->current_token.type == TOKEN_LPAREL) {
                had_parens = true;
                parser_eat(parser, TOKEN_LPAREL);

                if (parser->current_token.type != TOKEN_RPAREL) {
                    do {
                        ASTNode *arg = parser_parse_expression(parser);
                        parser_push_ast(&args, &arg_count, &arg_cap, arg);
                    } while (parser_match(parser, TOKEN_COMMA));
                }

                parser_eat(parser, TOKEN_RPAREL);
            }

            if (had_parens) {
                node = create_ast_method_call(node, method_name, method_hash, args, arg_count, line, col);
            } else {
                node = create_ast_obj_get(node, method_name, method_hash, line, col);
            }
        }
    }

    return node;
}
