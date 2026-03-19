#include "lexer.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

void Token_init(struct Lexer *lexer, const char *source) {
    lexer->source = source;
    lexer->position = 0;
    lexer->line = 1;
    lexer->current_char = source[0];
}

void advance(struct Lexer *lexer) {
    if (lexer->current_char == '\n') {
        lexer->line++;
    }
    lexer->position++;
    lexer->current_char = lexer->source[lexer->position];
}

static void skip_whitespace(struct Lexer *lexer) {
    while (lexer->current_char != '\0') {
        if (isspace(lexer->current_char)) {
            advance(lexer);
        }
        else if (lexer->current_char == '/' && peek(lexer) == '/') {
            advance(lexer);
            advance(lexer);
            while (lexer->current_char != '\n' && lexer->current_char != '\0') {
                    advance(lexer);
            }
        }
        else {
            break;
        }
    }
}

struct Token Read_Number(struct Lexer *lexer) {
    int value = 0;
    while (isdigit(lexer->current_char)) {
        value = value * 10 + (lexer->current_char - '0');
        advance(lexer);
    }
    struct Token token;
    token.type = TOKEN_TYPE_NUMBER;
    token.int_value = value;
    token.line = lexer->line;
    token.lexeme = NULL;

    return token;
}
struct Token Read_String(struct Lexer *lexer) {
    advance(lexer);
    size_t start_pos = lexer->position;

    while (lexer->current_char != '"' && lexer->current_char != '\0') {
        advance(lexer);
    }

    size_t size = lexer->position - start_pos;
    char *str = malloc(size + 1);
    memcpy(str, lexer->source + start_pos, size);
    str[size] = '\0';

    if (lexer->current_char == '"') {
        advance(lexer);
    }

    struct Token token;
    token.type = TOKEN_TYPE_STRING;
    token.line = lexer->line;
    token.lexeme = str;

    return token;
}

char peek(const struct Lexer *lexer) {
    if (lexer->current_char == '\0') {
        return lexer->current_char;
    }
    return lexer->source[lexer->position + 1];
}


struct Token Read_Keywords(struct Lexer *lexer) {
    const size_t start_pos = lexer->position;
    while (isalpha(lexer->current_char) || lexer->current_char == '_') {
        advance(lexer);
    }

    const size_t size = lexer->position - start_pos;
    char *lexeme = malloc(size+1);

    memcpy(lexeme, lexer->source + start_pos, size);

    lexeme[size] = '\0';

    enum TokenType token_type;

    if (strcmp(lexeme, "var") == 0) {
        token_type = TOKEN_KW_VAR;
    }
    else if (strcmp(lexeme, "Writeln") == 0) {
        token_type = TOKEN_KW_WRITELN;
    }
    else {
        token_type = TOKEN_IDENTIFIER;
    }


    struct Token token;
    token.type = token_type;
    token.line = lexer->line;
    token.lexeme = lexeme;

    return token;
}

struct Token make_token(const int type, char* lexeme, const int line) {
    struct Token token;

    token.type = type;
    token.line = line;
    token.lexeme = lexeme;

    return token;
}

struct Token lexer_next(struct Lexer *lexer) {
    skip_whitespace(lexer);

    if (lexer->current_char == '\0') {
        return make_token(TOKEN_EOF, NULL, lexer->line);
    }

    const char c = lexer->current_char;


    switch (c) {
        case '=':
            if (peek(lexer) == ':') {
                advance(lexer);
                advance(lexer);
                return make_token(TOKEN_ASSIGN, NULL, lexer->line);
            }
            advance(lexer);
            return make_token(TOKEN_ERROR, NULL, lexer->line);
        case '+':
            if (peek(lexer) == '+') {
                advance(lexer);
                advance(lexer);
                return make_token(TOKEN_INCREMENT, NULL, lexer->line);
            }
            advance(lexer);
            return make_token(TOKEN_PLUS, NULL, lexer->line);
        case '-':
            if (peek(lexer) == '>') {
                advance(lexer);
                advance(lexer);
                return make_token(TOKEN_ARROW, NULL, lexer->line);
            }
            advance(lexer);
            return make_token(TOKEN_MINUS, NULL, lexer->line);
        case ';':
            advance(lexer);
            return make_token(TOKEN_SEMICOLON, NULL, lexer->line);
        case '(':
            advance(lexer);
            return make_token(TOKEN_LPAREL, NULL, lexer->line);
        case ')':
            advance(lexer);
            return make_token(TOKEN_RPAREL, NULL, lexer->line);
        case '"':
            return Read_String(lexer);

        default:
            if (isalpha(c) || c == '_') {
                return Read_Keywords(lexer);
            }
            if (isdigit(c)) {
                return Read_Number(lexer);
            }
            advance(lexer);
            return make_token(TOKEN_ERROR, "Error: Unknown Char", lexer->line);
    }
}
