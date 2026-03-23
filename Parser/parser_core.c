#include "parser.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#define PARSER_SNIPPET_MAX 64

char *mks_strndup(const char *s, size_t n) {
    if (s == NULL) {
        fprintf(stderr, "\n[MKS Parser Error] mks_strndup got NULL source\n");
        exit(1);
    }

    size_t len = 0;
    while (len < n && s[len] != '\0') {
        len++;
    }

    char *copy = (char *)malloc(len + 1);
    if (copy == NULL) {
        fprintf(stderr, "\n[MKS Parser Error] Out of memory in mks_strndup\n");
        exit(1);
    }

    memcpy(copy, s, len);
    copy[len] = '\0';
    return copy;
}

static void make_printable_snippet(char *dst, size_t dst_size, const char *src, int len) {
    if (dst == NULL || dst_size == 0) {
        return;
    }

    if (src == NULL || len <= 0) {
        dst[0] = '\0';
        return;
    }

    size_t j = 0;
    for (int i = 0; i < len && j + 1 < dst_size; i++) {
        unsigned char c = (unsigned char)src[i];
        dst[j++] = (c >= 32 && c < 127) ? (char)c : '?';
    }

    dst[j] = '\0';
}

static void parser_vpanic(Parser *parser, const char *prefix, const char *message) {
    const char *safe_prefix = (prefix != NULL) ? prefix : "MKS Parser Error";
    const char *safe_message = (message != NULL) ? message : "Unknown parser error";

    if (parser != NULL) {
        fprintf(stderr,
                "\n\033[1;31m[%s]\033[0m\n"
                "Line %d: %s\n",
                safe_prefix,
                parser->current_token.line,
                safe_message);
    } else {
        fprintf(stderr,
                "\n\033[1;31m[%s]\033[0m\n"
                "Line ?: %s\n",
                safe_prefix,
                safe_message);
    }

    exit(1);
}

static void parser_check_lexer_error(Parser *parser) {
    if (parser == NULL) {
        parser_vpanic(NULL, "MKS Parser Error", "parser_check_lexer_error got NULL parser");
    }

    if (parser->current_token.type != TOKEN_ERROR) {
        return;
    }

    char buffer[256];
    char snippet[PARSER_SNIPPET_MAX + 1];

    int len = parser->current_token.length;
    if (len < 0) {
        len = 0;
    }
    if (len > PARSER_SNIPPET_MAX) {
        len = PARSER_SNIPPET_MAX;
    }

    make_printable_snippet(
        snippet,
        sizeof(snippet),
        parser->current_token.start,
        len
    );

    snprintf(
        buffer,
        sizeof(buffer),
        "Lexer produced TOKEN_ERROR near '%s'",
        snippet
    );

    parser_vpanic(parser, "MKS Syntax Error", buffer);
}

void parser_error(Parser *parser, const char *message) {
    parser_vpanic(parser, "MKS Syntax Error", message);
}

void parser_error_expected(Parser *parser, enum TokenType expected_type) {
    if (parser == NULL) {
        parser_vpanic(NULL, "MKS Syntax Error", "parser_error_expected got NULL parser");
    }

    char buffer[256];
    snprintf(
        buffer,
        sizeof(buffer),
        "Unexpected token (expected %d, got %d)",
        expected_type,
        parser->current_token.type
    );

    parser_vpanic(parser, "MKS Syntax Error", buffer);
}

void parser_advance(Parser *parser) {
    if (parser == NULL || parser->lexer == NULL) {
        parser_vpanic(parser, "MKS Parser Error", "Invalid parser or lexer in parser_advance()");
    }

    parser->current_token = lexer_next(parser->lexer);
    parser_check_lexer_error(parser);
}

void parser_eat(Parser *parser, const enum TokenType expected_type) {
    if (parser == NULL) {
        parser_vpanic(NULL, "MKS Parser Error", "parser_eat got NULL parser");
    }

    if (parser->current_token.type == expected_type) {
        parser_advance(parser);
        return;
    }

    parser_error_expected(parser, expected_type);
}

bool parser_match(Parser *parser, enum TokenType type) {
    if (parser == NULL) {
        parser_vpanic(NULL, "MKS Parser Error", "parser_match got NULL parser");
    }

    if (parser->current_token.type != type) {
        return false;
    }

    parser_advance(parser);
    return true;
}

enum TokenType peek_token_type(Parser *parser) {
    if (parser == NULL || parser->lexer == NULL) {
        return TOKEN_EOF;
    }

    struct Lexer temp_lexer = *(parser->lexer);
    struct Token next_token = lexer_next(&temp_lexer);
    return next_token.type;
}

bool parser_current_is_identifier(Parser *parser, const char *text) {
    if (parser == NULL || text == NULL) {
        return false;
    }

    if (parser->current_token.type != TOKEN_IDENTIFIER) {
        return false;
    }

    size_t text_len = strlen(text);
    if ((size_t)parser->current_token.length != text_len) {
        return false;
    }

    return strncmp(parser->current_token.start, text, text_len) == 0;
}

bool parser_match_identifier(Parser *parser, const char *text) {
    if (!parser_current_is_identifier(parser, text)) {
        return false;
    }

    parser_advance(parser);
    return true;
}

char *parser_take_identifier(Parser *parser, unsigned int *out_hash) {
    if (parser == NULL) {
        parser_vpanic(NULL, "MKS Parser Error", "parser_take_identifier got NULL parser");
    }

    if (parser->current_token.type != TOKEN_IDENTIFIER) {
        parser_error(parser, "Expected identifier");
    }

    char *name = mks_strndup(
        parser->current_token.start,
        (size_t)parser->current_token.length
    );

    if (out_hash != NULL) {
        *out_hash = get_hash(name);
    }

    parser_eat(parser, TOKEN_IDENTIFIER);
    return name;
}

void *parser_xmalloc(size_t size) {
    if (size == 0) {
        fprintf(stderr, "\n[MKS Parser Error] parser_xmalloc called with size 0\n");
        exit(1);
    }

    void *ptr = malloc(size);
    if (ptr == NULL) {
        fprintf(stderr, "\n[MKS Parser Error] Out of memory in malloc\n");
        exit(1);
    }

    return ptr;
}

void *parser_xcalloc(size_t count, size_t size) {
    if (count == 0 || size == 0) {
        fprintf(stderr, "\n[MKS Parser Error] parser_xcalloc called with zero count or size\n");
        exit(1);
    }

    void *ptr = calloc(count, size);
    if (ptr == NULL) {
        fprintf(stderr, "\n[MKS Parser Error] Out of memory in calloc\n");
        exit(1);
    }

    return ptr;
}

void *parser_xrealloc(void *ptr, size_t size) {
    if (size == 0) {
        free(ptr);
        fprintf(stderr, "\n[MKS Parser Error] parser_xrealloc called with size 0\n");
        exit(1);
    }

    void *new_ptr = realloc(ptr, size);
    if (new_ptr == NULL) {
        fprintf(stderr, "\n[MKS Parser Error] Out of memory in realloc\n");
        exit(1);
    }

    return new_ptr;
}

void parser_push_ast(ASTNode ***items, int *count, int *cap, ASTNode *node) {
    if (items == NULL || count == NULL || cap == NULL) {
        fprintf(stderr, "\n[MKS Parser Error] parser_push_ast got invalid arguments\n");
        exit(1);
    }

    if (node == NULL) {
        fprintf(stderr, "\n[MKS Parser Error] parser_push_ast got NULL node\n");
        exit(1);
    }

    if (*count < 0 || *cap < 0) {
        fprintf(stderr, "\n[MKS Parser Error] parser_push_ast got negative count/cap\n");
        exit(1);
    }

    if (*items == NULL || *cap == 0) {
        *cap = 8;
        *items = (ASTNode **)parser_xmalloc(sizeof(ASTNode *) * (size_t)(*cap));
    } else if (*count >= *cap) {
        if (*cap > (int)(2147483647 / 2)) {
            fprintf(stderr, "\n[MKS Parser Error] parser_push_ast capacity overflow\n");
            exit(1);
        }

        *cap *= 2;
        *items = (ASTNode **)parser_xrealloc(
            *items,
            sizeof(ASTNode *) * (size_t)(*cap)
        );
    }

    (*items)[*count] = node;
    (*count)++;
}

void parser_push_str(char ***items, int *count, int *cap, char *value) {
    if (items == NULL || count == NULL || cap == NULL) {
        fprintf(stderr, "\n[MKS Parser Error] parser_push_str got invalid arguments\n");
        exit(1);
    }

    if (value == NULL) {
        fprintf(stderr, "\n[MKS Parser Error] parser_push_str got NULL value\n");
        exit(1);
    }

    if (*count < 0 || *cap < 0) {
        fprintf(stderr, "\n[MKS Parser Error] parser_push_str got negative count/cap\n");
        exit(1);
    }

    if (*items == NULL || *cap == 0) {
        *cap = 8;
        *items = (char **)parser_xmalloc(sizeof(char *) * (size_t)(*cap));
    } else if (*count >= *cap) {
        if (*cap > (int)(2147483647 / 2)) {
            fprintf(stderr, "\n[MKS Parser Error] parser_push_str capacity overflow\n");
            exit(1);
        }

        *cap *= 2;
        *items = (char **)parser_xrealloc(
            *items,
            sizeof(char *) * (size_t)(*cap)
        );
    }

    (*items)[*count] = value;
    (*count)++;
}

void parser_init(Parser *parser, struct Lexer *lexer) {
    if (parser == NULL || lexer == NULL) {
        parser_vpanic(NULL, "MKS Parser Error", "parser_init got NULL parser or lexer");
    }

    parser->lexer = lexer;
    parser->current_token = lexer_next(lexer);
    parser_check_lexer_error(parser);
}