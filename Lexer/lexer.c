#include "lexer.h"
#include <ctype.h>
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
    int value = 0;
    while (isdigit(lexer->current_char)) {
        value = value * 10 + (lexer->current_char - '0');
        advance(lexer);
    }

    struct Token token = make_token(TOKEN_TYPE_NUMBER, start, (lexer->source + lexer->position) - start, lexer->line);
    token.int_value = value;

    return token;
}

static struct Token Read_String(struct Lexer *lexer) {
    advance(lexer);
    const char *start = lexer->source + lexer->position;
    while (lexer->current_char != '"' && lexer->current_char != '\0') advance(lexer);
    int length = (lexer->source + lexer->position) - start;
    if (lexer->current_char == '"') advance(lexer);
    return make_token(TOKEN_TYPE_STRING, start, length, lexer->line);
}

static struct Token Read_Keywords(struct Lexer *lexer) {
    const char *start = lexer->source + lexer->position;
    while (isalnum(lexer->current_char) || lexer->current_char == '_') advance(lexer);
    int length = (lexer->source + lexer->position) - start;

    enum TokenType type = TOKEN_IDENTIFIER;
    if (length == 2 && strncmp(start, "if", 2) == 0) type = TOKEN_KW_IF;
    else if (length == 3 && strncmp(start, "var", 3) == 0) type = TOKEN_KW_VAR;
    else if (length == 4 && strncmp(start, "else", 4) == 0) type = TOKEN_KW_ELSE;
    else if (length == 5 && strncmp(start, "while", 5) == 0) type = TOKEN_KW_WHILE;
    else if (length == 5 && strncmp(start, "Write", 5) == 0) type = TOKEN_KW_WRITE;
    else if (length == 7 && strncmp(start, "Writeln", 7) == 0) type = TOKEN_KW_WRITELN;
    else if (length == 3 && strncmp(start, "fnc", 3) == 0) type = TOKEN_KW_FNC;
    else if (length == 4 && strncmp(start, "call", 4) == 0) type = TOKEN_KW_CALL;
    else if (length == 6 && strncmp(start, "return", 6) == 0) type = TOKEN_KW_RETURN;

    return make_token(type, start, length, lexer->line);
}

struct Token lexer_next(struct Lexer *lexer) {
    skip_whitespace(lexer);
    if (lexer->current_char == '\0') return make_token(TOKEN_EOF, lexer->source + lexer->position, 0, lexer->line);

    const char c = lexer->current_char;
    const char *start = lexer->source + lexer->position;

    switch (c) {
        case '=':
            if (peek(lexer) == ':') { advance(lexer); advance(lexer); return make_token(TOKEN_ASSIGN, start, 2, lexer->line); }
            advance(lexer); return make_token(TOKEN_ERROR, start, 1, lexer->line);
        case '-':
            if (peek(lexer) == '>') { advance(lexer); advance(lexer); return make_token(TOKEN_BLOCK_START, start, 2, lexer->line); }
            advance(lexer); return make_token(TOKEN_MINUS, start, 1, lexer->line);
        case '<':
            if (peek(lexer) == '-') { advance(lexer); advance(lexer); return make_token(TOKEN_BLOCK_END, start, 2, lexer->line); }
            advance(lexer); return make_token(TOKEN_ERROR, start, 1, lexer->line);
        case '?':
            if (peek(lexer) == '=') { advance(lexer); advance(lexer); return make_token(TOKEN_EQ, start, 2, lexer->line); }
            advance(lexer); return make_token(TOKEN_ERROR, start, 1, lexer->line);
        case '!':
            if (peek(lexer) == '?') { advance(lexer); advance(lexer); return make_token(TOKEN_NOT_EQ, start, 2, lexer->line); }
            advance(lexer); return make_token(TOKEN_ERROR, start, 1, lexer->line);
        case '+': advance(lexer); return make_token(TOKEN_PLUS, start, 1, lexer->line);
        case '*': advance(lexer); return make_token(TOKEN_STAR, start, 1, lexer->line);
        case '/': advance(lexer); return make_token(TOKEN_SLASH, start, 1, lexer->line);
        case '%': advance(lexer); return make_token(TOKEN_MOD, start, 1, lexer->line);
        case ',': advance(lexer); return make_token(TOKEN_COMMA, start, 1, lexer->line);
        case ';': advance(lexer); return make_token(TOKEN_SEMICOLON, start, 1, lexer->line);
        case '(': advance(lexer); return make_token(TOKEN_LPAREL, start, 1, lexer->line);
        case ')': advance(lexer); return make_token(TOKEN_RPAREL, start, 1, lexer->line);
        case '"': return Read_String(lexer);
        default:
            if (isalpha(c) || c == '_') return Read_Keywords(lexer);
            if (isdigit(c)) return Read_Number(lexer);
            advance(lexer);
            return make_token(TOKEN_ERROR, start, 1, lexer->line);
    }
}