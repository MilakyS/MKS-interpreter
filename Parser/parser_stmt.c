#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include "parser.h"
#include <string.h>

static ASTNode* parser_parse_using(Parser *parser);
static ASTNode* parser_parse_export(Parser *parser);
static ASTNode* parser_parse_func_decl(Parser *parser);
static ASTNode* parser_parse_entity(Parser *parser);
static ASTNode* parser_parse_extend(Parser *parser);


static ASTNode* parser_parse_var_decl(Parser *parser);
static ASTNode* parser_parse_if(Parser *parser);
static ASTNode* parser_parse_while(Parser *parser);
static ASTNode* parser_parse_for(Parser *parser);
static ASTNode* parser_parse_output(Parser *parser, bool newline);
static ASTNode* parser_parse_return(Parser *parser);
static ASTNode* parser_parse_test(Parser *parser);
static ASTNode* parser_parse_defer(Parser *parser);
static ASTNode* parser_parse_watch(Parser *parser);
static ASTNode* parser_parse_on_change(Parser *parser);
static ASTNode* parser_parse_break(Parser *parser);
static ASTNode* parser_parse_continue(Parser *parser);
static ASTNode* parser_parse_repeat(Parser *parser);
static ASTNode* parser_parse_switch(Parser *parser);

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

ASTNode* parser_parse_statement(Parser *parser) {
    const enum TokenType type = parser->current_token.type;

    if (type == TOKEN_IDENTIFIER && parser_current_is_identifier(parser, "test")) {
        return parser_parse_test(parser);
    }

    switch (type) {
        case TOKEN_KW_BREAK:
            return parser_parse_break(parser);

        case TOKEN_KW_CONTINUE:
            return parser_parse_continue(parser);

        case TOKEN_KW_REPEAT:
            return parser_parse_repeat(parser);

        case TOKEN_KW_SWITCH:
            return parser_parse_switch(parser);

        case TOKEN_KW_DEFER:
            return parser_parse_defer(parser);

        case TOKEN_KW_WATCH:
            return parser_parse_watch(parser);

        case TOKEN_KW_ON:
            if (peek_token_type(parser) == TOKEN_KW_CHANGE) {
                return parser_parse_on_change(parser);
            }
            break;
        case TOKEN_KW_USING:
            return parser_parse_using(parser);

        case TOKEN_KW_EXPORT:
            return parser_parse_export(parser);

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

        case TOKEN_KW_ENTITY:
            return parser_parse_entity(parser);

        case TOKEN_KW_EXTEND:
            return parser_parse_extend(parser);

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

    return NULL;
}

static ASTNode* parser_parse_break(Parser *parser) {
    const int line = parser->current_token.line;
    const int col = parser_current_col(parser);
    parser_eat(parser, TOKEN_KW_BREAK);
    if (parser->current_token.type == TOKEN_SEMICOLON) {
        parser_eat(parser, TOKEN_SEMICOLON);
    }
    return create_ast_break(line, col);
}

static ASTNode* parser_parse_continue(Parser *parser) {
    const int line = parser->current_token.line;
    const int col = parser_current_col(parser);
    parser_eat(parser, TOKEN_KW_CONTINUE);
    if (parser->current_token.type == TOKEN_SEMICOLON) {
        parser_eat(parser, TOKEN_SEMICOLON);
    }
    return create_ast_continue(line, col);
}

static ASTNode* parser_parse_repeat(Parser *parser) {
    const int line = parser->current_token.line;
    const int col = parser_current_col(parser);
    parser_eat(parser, TOKEN_KW_REPEAT);

    bool has_iter = false;
    char *iter_name = NULL;
    unsigned int iter_hash = 0;
    ASTNode *count_expr = NULL;

    if (parser->current_token.type == TOKEN_IDENTIFIER &&
        peek_token_type(parser) == TOKEN_KW_IN) {
        has_iter = true;
        iter_name = parser_take_identifier(parser, &iter_hash);
        parser_eat(parser, TOKEN_KW_IN);
    }

    count_expr = parser_parse_expression(parser);
    ASTNode *body = parser_parse_block(parser);
    return create_ast_repeat(has_iter, iter_name, iter_hash, count_expr, body, line, col);
}

static ASTNode* parser_parse_defer(Parser *parser) {
    const int line = parser->current_token.line;
    const int col = parser_current_col(parser);
    parser_eat(parser, TOKEN_KW_DEFER);
    ASTNode *body = parser_parse_block(parser);
    return create_ast_defer(body, line, col);
}

static ASTNode* parser_parse_switch(Parser *parser) {
    const int line = parser->current_token.line;
    const int col = parser_current_col(parser);
    ASTNode **case_values = NULL;
    ASTNode **case_bodies = NULL;
    int case_count = 0;
    int case_cap = 0;
    ASTNode *default_body = NULL;

    parser_eat(parser, TOKEN_KW_SWITCH);
    parser_eat(parser, TOKEN_LPAREL);
    ASTNode *value = parser_parse_expression(parser);
    parser_eat(parser, TOKEN_RPAREL);
    parser_eat(parser, TOKEN_BLOCK_START);

    while (parser->current_token.type != TOKEN_BLOCK_END &&
           parser->current_token.type != TOKEN_EOF) {
        if (parser->current_token.type == TOKEN_KW_CASE) {
            parser_eat(parser, TOKEN_KW_CASE);
            ASTNode *case_value = parser_parse_expression(parser);
            ASTNode *case_body = parser_parse_block(parser);
            int old_case_cap = case_cap;
            parser_push_ast(&case_values, &case_count, &case_cap, case_value);
            if (case_bodies == NULL) {
                case_bodies = (ASTNode **)parser_xcalloc((size_t)case_cap, sizeof(ASTNode *));
            } else if (case_cap != old_case_cap) {
                case_bodies = (ASTNode **)parser_xrealloc(case_bodies, sizeof(ASTNode *) * (size_t)case_cap);
            }
            case_bodies[case_count - 1] = case_body;
            continue;
        }

        if (parser->current_token.type == TOKEN_KW_DEFAULT) {
            if (default_body != NULL) {
                parser_error(parser, "Duplicate default in switch");
            }
            parser_eat(parser, TOKEN_KW_DEFAULT);
            default_body = parser_parse_block(parser);
            continue;
        }

        parser_error(parser, "Unexpected token in switch block");
    }

    parser_eat(parser, TOKEN_BLOCK_END);
    return create_ast_switch(value, case_values, case_bodies, case_count, default_body, line, col);
}

static ASTNode* parser_parse_watch(Parser *parser) {
    const int line = parser->current_token.line;
    const int col = parser_current_col(parser);
    parser_eat(parser, TOKEN_KW_WATCH);

    unsigned int h = 0;
    char *name = parser_take_identifier(parser, &h);
    parser_eat(parser, TOKEN_SEMICOLON);
    return create_ast_watch(name, h, line, col);
}

static ASTNode* parser_parse_on_change(Parser *parser) {
    const int line = parser->current_token.line;
    const int col = parser_current_col(parser);
    parser_eat(parser, TOKEN_KW_ON);
    parser_eat(parser, TOKEN_KW_CHANGE);
    unsigned int h = 0;
    char *name = parser_take_identifier(parser, &h);
    ASTNode *body = parser_parse_block(parser);
    return create_ast_on_change(name, h, body, line, col);
}

ASTNode* parser_parse_block(Parser *parser) {
    const int line = parser->current_token.line;
    const int col = parser_current_col(parser);
    ASTNode **items = NULL;
    int count = 0;
    int cap = 0;

    parser_eat(parser, TOKEN_BLOCK_START);

    while (parser->current_token.type != TOKEN_EOF &&
           parser->current_token.type != TOKEN_BLOCK_END) {
        parser_push_ast(&items, &count, &cap, parser_parse_statement(parser));
    }

    parser_eat(parser, TOKEN_BLOCK_END);
    return create_ast_block(items, count, line, col);
}

ASTNode* parser_parse_program(Parser *parser) {
    ASTNode **items = NULL;
    int count = 0;
    int cap = 0;

    while (parser->current_token.type != TOKEN_EOF) {
        parser_push_ast(&items, &count, &cap, parser_parse_statement(parser));
    }

    return create_ast_block(items, count, 1, 1);
}

static ASTNode* parser_parse_using(Parser *parser) {
    const int line = parser->current_token.line;
    parser_eat(parser, TOKEN_KW_USING);

    bool is_legacy = false;
    bool star = false;
    char *module_id = NULL;
    char *alias = NULL;

    if (parser->current_token.type == TOKEN_TYPE_STRING) {
        /* legacy path form */
        is_legacy = true;
        module_id = mks_strndup(parser->current_token.start, (size_t)parser->current_token.length);
        parser_eat(parser, TOKEN_TYPE_STRING);
    } else {
        /* parse dotted identifier: ident(.ident)* optionally ending with .* */
        if (parser->current_token.type != TOKEN_IDENTIFIER) {
            parser_error(parser, "Expected module id after 'using'");
        }

        /* build module_id string */
        size_t buf_cap = 64; size_t len = 0;
        module_id = parser_xmalloc(buf_cap);
        module_id[0] = '\0';

        while (true) {
            const char *start = parser->current_token.start;
            int l = parser->current_token.length;
            if (len + (size_t)l + 2 >= buf_cap) {
                buf_cap *= 2;
                module_id = parser_xrealloc(module_id, buf_cap);
            }
            memcpy(module_id + len, start, (size_t)l);
            len += (size_t)l;
            module_id[len] = '\0';
            parser_eat(parser, TOKEN_IDENTIFIER);

            if (parser->current_token.type == TOKEN_DOT) {
                parser_eat(parser, TOKEN_DOT);
                module_id[len++] = '.';
                module_id[len] = '\0';
                continue;
            } else if (parser->current_token.type == TOKEN_STAR) {
                /* using foo.bar.* is not supported; reject early */
                parser_error(parser, "Star-import is not supported. Use namespace import only.");
            }
            break;
        }
    }

    if (parser_match_identifier(parser, "as")) {
        unsigned int ignored_hash = 0;
        alias = parser_take_identifier(parser, &ignored_hash);
    }

    parser_eat(parser, TOKEN_SEMICOLON);
    return create_ast_using(module_id, alias, is_legacy, star, line, parser_current_col(parser));
}

static ASTNode* parser_parse_export(Parser *parser) {
    const int line = parser->current_token.line;
    parser_eat(parser, TOKEN_KW_EXPORT);

    ASTNode *decl = NULL;
    char *name_override = NULL;

    if (parser->current_token.type == TOKEN_KW_FNC) {
        decl = parser_parse_func_decl(parser);
    } else if (parser->current_token.type == TOKEN_KW_VAR) {
        decl = parser_parse_var_decl(parser);
    } else if (parser->current_token.type == TOKEN_KW_ENTITY) {
        decl = parser_parse_entity(parser);
    } else {
        parser_error(parser, "Expected fnc/var/entity after export");
    }

    return create_ast_export(decl, name_override, line, parser_current_col(parser));
}

static ASTNode* parser_parse_var_decl(Parser *parser) {
    const int line = parser->current_token.line;
    unsigned int name_hash = 0;

    parser_eat(parser, TOKEN_KW_VAR);
    char *name = parser_take_identifier(parser, &name_hash);

    parser_eat(parser, TOKEN_ASSIGN);
    ASTNode *value = parser_parse_expression(parser);
    parser_eat(parser, TOKEN_SEMICOLON);

    return create_ast_var_decl(value, line, parser_current_col(parser), name, name_hash);
}

static ASTNode* parser_parse_method(Parser *parser) {
    const int line = parser->current_token.line;
    parser_eat(parser, TOKEN_KW_METHOD);

    unsigned int name_hash = 0;
    char *name = parser_take_identifier(parser, &name_hash);

    parser_eat(parser, TOKEN_LPAREL);

    char **params = NULL;
    unsigned int *param_hashes = NULL;
    int param_count = 0, param_cap = 0;

    if (parser->current_token.type != TOKEN_RPAREL) {
        do {
            unsigned int ph = 0;
            char *p = parser_take_identifier(parser, &ph);
            parser_push_str(&params, &param_count, &param_cap, p);

            unsigned int *tmp = (unsigned int *)parser_xrealloc(param_hashes, sizeof(unsigned int) * (size_t)param_count);
            param_hashes = tmp;
            param_hashes[param_count - 1] = ph;
        } while (parser_match(parser, TOKEN_COMMA));
    }

    parser_eat(parser, TOKEN_RPAREL);

    ASTNode *body = parser_parse_block(parser);

    return create_ast_func_decl(name, name_hash, params, param_hashes, param_count, body, line, parser_current_col(parser));
}

static ASTNode* parser_parse_entity(Parser *parser) {
    const int line = parser->current_token.line;
    parser_eat(parser, TOKEN_KW_ENTITY);

    unsigned int name_hash = 0;
    char *name = parser_take_identifier(parser, &name_hash);

    parser_eat(parser, TOKEN_LPAREL);
    char **params = NULL; unsigned int *param_hashes = NULL; int param_count = 0, param_cap = 0;
    if (parser->current_token.type != TOKEN_RPAREL) {
        do {
            unsigned int ph = 0;
            char *p = parser_take_identifier(parser, &ph);
            parser_push_str(&params, &param_count, &param_cap, p);

            unsigned int *tmp = (unsigned int *)parser_xrealloc(param_hashes, sizeof(unsigned int) * (size_t)param_count);
            param_hashes = tmp;
            param_hashes[param_count - 1] = ph;
        } while (parser_match(parser, TOKEN_COMMA));
    }
    parser_eat(parser, TOKEN_RPAREL);

    parser_eat(parser, TOKEN_BLOCK_START);

    ASTNode *init_body = NULL;
    ASTNode **methods = NULL; int method_count = 0, method_cap = 0;

    while (parser->current_token.type != TOKEN_BLOCK_END &&
           parser->current_token.type != TOKEN_EOF) {
        if (parser->current_token.type == TOKEN_KW_INIT) {
            parser_eat(parser, TOKEN_KW_INIT);
            init_body = parser_parse_block(parser);
            continue;
        }
        if (parser->current_token.type == TOKEN_KW_METHOD) {
            ASTNode *m = parser_parse_method(parser);
            parser_push_ast(&methods, &method_count, &method_cap, m);
            continue;
        }
        parser_error(parser, "Unexpected token in entity block");
    }

    parser_eat(parser, TOKEN_BLOCK_END);

    return create_ast_entity(name, name_hash, params, param_hashes, param_count, init_body, methods, method_count, line, parser_current_col(parser));
}

static int parse_extend_target(Parser *parser) {
    if (parser->current_token.type != TOKEN_IDENTIFIER) {
        parser_error(parser, "Expected type name after extend");
    }
    int target = -1;
    if (parser_current_is_identifier(parser, "array")) target = 0;
    else if (parser_current_is_identifier(parser, "string")) target = 1;
    else if (parser_current_is_identifier(parser, "number")) target = 2;
    else parser_error(parser, "Unsupported extend target");
    parser_advance(parser);
    return target;
}

static ASTNode* parser_parse_extend(Parser *parser) {
    const int line = parser->current_token.line;
    parser_eat(parser, TOKEN_KW_EXTEND);
    int target = parse_extend_target(parser);
    parser_eat(parser, TOKEN_BLOCK_START);

    ASTNode **methods = NULL; int mc = 0, mcap = 0;
    while (parser->current_token.type != TOKEN_BLOCK_END &&
           parser->current_token.type != TOKEN_EOF) {
        if (parser->current_token.type == TOKEN_KW_METHOD) {
            ASTNode *m = parser_parse_method(parser);
            parser_push_ast(&methods, &mc, &mcap, m);
            continue;
        }
        parser_error(parser, "Unexpected token in extend block");
    }

    parser_eat(parser, TOKEN_BLOCK_END);
    return create_ast_extend(target, methods, mc, line, parser_current_col(parser));
}

static ASTNode* parser_parse_test(Parser *parser) {
    const int line = parser->current_token.line;
    parser_eat(parser, TOKEN_IDENTIFIER);

    if (parser->current_token.type != TOKEN_TYPE_STRING) {
        parser_error(parser, "Expected test name string");
    }
    char *name = mks_strndup(parser->current_token.start, (size_t)parser->current_token.length);
    parser_eat(parser, TOKEN_TYPE_STRING);

    ASTNode *body = parser_parse_block(parser);
    return create_ast_test(name, body, line, parser_current_col(parser));
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

    return create_if_block(condition, body, else_body, line, parser_current_col(parser));
}

static ASTNode* parser_parse_while(Parser *parser) {
    const int line = parser->current_token.line;

    parser_eat(parser, TOKEN_KW_WHILE);
    parser_eat(parser, TOKEN_LPAREL);

    ASTNode *condition = parser_parse_expression(parser);

    parser_eat(parser, TOKEN_RPAREL);

    ASTNode *body = parser_parse_block(parser);
    return create_while_block(condition, body, line, parser_current_col(parser));
}

static ASTNode* parser_parse_for(Parser *parser) {
    const int line = parser->current_token.line;

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

            init = create_ast_var_decl(v_value, v_line, parser_current_col(parser), v_name, v_hash);
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

    ASTNode *body = parser_parse_block(parser);
    return create_ast_for(init, condition, step, body, line, parser_current_col(parser));
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

    return create_ast_output(args, count, newline, line, parser_current_col(parser));
}

static ASTNode* parser_parse_func_decl(Parser *parser) {
    const int line = parser->current_token.line;
    char **params = NULL;
    unsigned int *param_hashes = NULL;
    int param_count = 0;

    parser_eat(parser, TOKEN_KW_FNC);

    unsigned int func_hash = 0;
    char *func_name = parser_take_identifier(parser, &func_hash);

    parser_eat(parser, TOKEN_LPAREL);

    if (parser->current_token.type != TOKEN_RPAREL) {
        int param_cap = 0;
        do {
            unsigned int param_hash = 0;
            char *param = parser_take_identifier(parser, &param_hash);

            if (param_count >= param_cap) {
                const int new_cap = (param_cap == 0) ? 4 : param_cap * 2;

                char **tmp_params = realloc(params, sizeof(char *) * (size_t)new_cap);
                if (tmp_params == NULL) {
                    fprintf(stderr, "[MKS Parser Error] Out of memory while growing params array\n");
                    exit(1);
                }

                unsigned int *tmp_hashes =
                    realloc(param_hashes, sizeof(unsigned int) * (size_t)new_cap);
                if (tmp_hashes == NULL) {
                    fprintf(stderr, "[MKS Parser Error] Out of memory while growing param hashes array\n");
                    exit(1);
                }

                params = tmp_params;
                param_hashes = tmp_hashes;
                param_cap = new_cap;
            }
            if (params == NULL || param_hashes == NULL) {
                fprintf(stderr, "[MKS Parser Error] Internal allocation failure\n");
                exit(1);
            }
            params[param_count] = param;
            param_hashes[param_count] = param_hash;
            param_count++;
        } while (parser_match(parser, TOKEN_COMMA));
    }

    parser_eat(parser, TOKEN_RPAREL);

    ASTNode *body = parser_parse_block(parser);
    return create_ast_func_decl(func_name, func_hash, params, param_hashes, param_count, body, line, parser_current_col(parser));
}

static ASTNode* parser_parse_return(Parser *parser) {
    const int line = parser->current_token.line;

    parser_eat(parser, TOKEN_KW_RETURN);
    ASTNode *value = parser_parse_expression(parser);
    parser_eat(parser, TOKEN_SEMICOLON);

    return create_ast_return(value, line, parser_current_col(parser));
}
