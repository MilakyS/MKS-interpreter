#ifndef CMINUSINTERPRETATOR_LEXER_H
#define CMINUSINTERPRETATOR_LEXER_H

#include <stddef.h>
#include <stdbool.h>

enum TokenType {
    TOKEN_EOF,
    TOKEN_ERROR,
    TOKEN_IDENTIFIER,
    TOKEN_TYPE_NUMBER,
    TOKEN_TYPE_STRING,
    TOKEN_ASSIGN,
    TOKEN_PLUS,
    TOKEN_MINUS,
    TOKEN_STAR,
    TOKEN_SLASH,
    TOKEN_MOD,
    TOKEN_INCREMENT,
    TOKEN_BLOCK_START,
    TOKEN_BLOCK_END,
    TOKEN_SEMICOLON,
    TOKEN_LPAREL,
    TOKEN_COMMA,
    TOKEN_RPAREL,
    TOKEN_EQ,
    TOKEN_NOT_EQ,

    // KeyWords
    TOKEN_KW_VAR,
    TOKEN_KW_WRITELN,
    TOKEN_KW_WRITE,
    TOKEN_KW_IF,
    TOKEN_KW_ELSE,
    TOKEN_KW_WHILE,
    TOKEN_KW_FNC,
    TOKEN_KW_CALL,
    TOKEN_KW_RETURN,
};

struct Lexer {
    const char *source;
    size_t position;
    int line;
    char current_char;
};

struct Token {
    enum TokenType type;
    int line;
    const char *start;
    int length;

    union {
        int int_value;
        float float_value;
        bool bool_value;
        char char_value;
    };
};

void Token_init(struct Lexer *lexer, const char *source);
struct Token lexer_next(struct Lexer *lexer);

#endif