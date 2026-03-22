#include "lexer.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static void advance(struct Lexer *lexer) {
    if (lexer->current_char == '\n') lexer->line++;
    lexer->position++;
    lexer->current_char = lexer->source[lexer->position];
}

static char peek(const struct Lexer *lexer) {
    if (lexer->current_char == '\0') return '\0';
    return lexer->source[lexer->position + 1];
}

void Token_init(struct Lexer *lexer, const char *source) {
    lexer->source = source;
    lexer->position = 0;
    lexer->line = 1;
    lexer->current_char = source[0];
}

static void skip_whitespace(struct Lexer *lexer) {
    while (lexer->current_char != '\0') {
        if (isspace(lexer->current_char)) {
            advance(lexer);
        } else if (lexer->current_char == '/' && peek(lexer) == '/') {
            while (lexer->current_char != '\n' && lexer->current_char != '\0') {
                advance(lexer);
            }
        } else {
            break;
        }
    }
}

static struct Token make_token(enum TokenType type, const char* start, int length, int line) {
    struct Token token;
    token.type = type;
    token.line = line;
    token.start = start;
    token.length = length;
    return token;
}

static struct Token Read_Number(struct Lexer *lexer) {
    const char *start = lexer->source + lexer->position;
    int line = lexer->line;

    while (isdigit(lexer->current_char)) advance(lexer);

    if (lexer->current_char == '.' && isdigit(lexer->source[lexer->position + 1])) {
        advance(lexer);
        while (isdigit(lexer->current_char)) advance(lexer);
    }

    int length = (int)(lexer->source + lexer->position - start);


    char temp_buf[64];
    if (length < 63) {
        strncpy(temp_buf, start, length);
        temp_buf[length] = '\0';
    }

    struct Token token;
    token.type = TOKEN_TYPE_NUMBER;
    token.start = start;
    token.length = length;
    token.line = line;
    token.double_value = atof(temp_buf);

    return token;
}

static struct Token Read_String(struct Lexer *lexer) {
    int line = lexer->line;
    advance(lexer);
    const char *start = lexer->source + lexer->position;

    while (lexer->current_char != '"' && lexer->current_char != '\0') {
        advance(lexer);
    }

    int length = (lexer->source + lexer->position) - start;

    if (lexer->current_char == '"') {
        advance(lexer);
    }

    return make_token(TOKEN_TYPE_STRING, start, length, line);
}

static struct Token Read_Keywords(struct Lexer *lexer) {
    const char *start = lexer->source + lexer->position;
    int line = lexer->line;

    while (isalnum(lexer->current_char) || lexer->current_char == '_') {
        advance(lexer);
    }

    int length = (int)(lexer->source + lexer->position - start);

    static const struct {
        const char *kw;
        enum TokenType type;
    } keywords[] = {
        {"if", TOKEN_KW_IF},
        {"var", TOKEN_KW_VAR},
        {"else", TOKEN_KW_ELSE},
        {"while", TOKEN_KW_WHILE},
        {"Write", TOKEN_KW_WRITE},
        {"Writeln", TOKEN_KW_WRITELN},
        {"fnc", TOKEN_KW_FNC},
        {"call", TOKEN_KW_CALL},
        {"return", TOKEN_KW_RETURN},
        {"for", TOKEN_KW_FOR},
        {"using", TOKEN_KW_USING},
        {NULL, TOKEN_IDENTIFIER}
    };

    enum TokenType type = TOKEN_IDENTIFIER;
    for (int i = 0; keywords[i].kw != NULL; i++) {
        if (length == (int)strlen(keywords[i].kw) && strncmp(start, keywords[i].kw, length) == 0) {
            type = keywords[i].type;
            break;
        }
    }

    return make_token(type, start, length, line);
}

struct Token lexer_next(struct Lexer *lexer) {
    skip_whitespace(lexer);

    const char *start = lexer->source + lexer->position;
    if (lexer->current_char == '\0')
        return make_token(TOKEN_EOF, start, 0, lexer->line);

    const char c = lexer->current_char;

    switch (c) {
        case '=':
            if (peek(lexer) == ':') { advance(lexer); advance(lexer); return make_token(TOKEN_ASSIGN, start, 2, lexer->line); }
            advance(lexer); return make_token(TOKEN_ERROR, start, 1, lexer->line);
        case '-':
            if (peek(lexer) == '>') { advance(lexer); advance(lexer); return make_token(TOKEN_BLOCK_START, start, 2, lexer->line); }
            advance(lexer); return make_token(TOKEN_MINUS, start, 1, lexer->line);
        case '<':
            if (peek(lexer) == '-') { advance(lexer); advance(lexer); return make_token(TOKEN_BLOCK_END, start, 2, lexer->line); }
            if (peek(lexer) == '=') { advance(lexer); advance(lexer); return make_token(TOKEN_LESS_EQUAL, start, 2, lexer->line); }
            advance(lexer); return make_token(TOKEN_LESS, start, 1, lexer->line);
        case '>':
            if (peek(lexer) == '=') { advance(lexer); advance(lexer); return make_token(TOKEN_GREATER_EQUAL, start, 2, lexer->line); }
            advance(lexer); return make_token(TOKEN_GREATER, start, 1, lexer->line);
        case '?':
            if (peek(lexer) == '=') { advance(lexer); advance(lexer); return make_token(TOKEN_EQ, start, 2, lexer->line); }
            advance(lexer); return make_token(TOKEN_ERROR, start, 1, lexer->line);
        case '!':
            if (peek(lexer) == '?') { advance(lexer); advance(lexer); return make_token(TOKEN_NOT_EQ, start, 2, lexer->line); }
            advance(lexer); return make_token(TOKEN_ERROR, start, 1, lexer->line);

        case '&':
            if (peek(lexer) == '&') { advance(lexer); advance(lexer); return make_token(TOKEN_AND, start, 2, lexer->line); }
            advance(lexer); return make_token(TOKEN_ERROR, start, 1, lexer->line);
        case '|':
            if (peek(lexer) == '|') { advance(lexer); advance(lexer); return make_token(TOKEN_OR, start, 2, lexer->line); }
            advance(lexer); return make_token(TOKEN_ERROR, start, 1, lexer->line);

        case '+': advance(lexer); return make_token(TOKEN_PLUS, start, 1, lexer->line);
        case '*': advance(lexer); return make_token(TOKEN_STAR, start, 1, lexer->line);
        case '/': advance(lexer); return make_token(TOKEN_SLASH, start, 1, lexer->line);
        case '%': advance(lexer); return make_token(TOKEN_MOD, start, 1, lexer->line);
        case ',': advance(lexer); return make_token(TOKEN_COMMA, start, 1, lexer->line);
        case ';': advance(lexer); return make_token(TOKEN_SEMICOLON, start, 1, lexer->line);
        case '(': advance(lexer); return make_token(TOKEN_LPAREL, start, 1, lexer->line);
        case ')': advance(lexer); return make_token(TOKEN_RPAREL, start, 1, lexer->line);
        case '[': advance(lexer); return make_token(TOKEN_LBRACKET, start, 1, lexer->line);
        case ']': advance(lexer); return make_token(TOKEN_RBRACKET, start, 1, lexer->line);
        case '.': advance(lexer); return make_token(TOKEN_DOT, start, 1, lexer->line);
        case '"': return Read_String(lexer);

        default:
            if (isalpha(c) || c == '_') return Read_Keywords(lexer);
            if (isdigit(c)) return Read_Number(lexer);
            advance(lexer);
            return make_token(TOKEN_ERROR, start, 1, lexer->line);
    }
}