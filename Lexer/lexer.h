#ifndef CMINUSINTERPRETATOR_LEXER_H
#define CMINUSINTERPRETATOR_LEXER_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

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
    TOKEN_LESS,
    TOKEN_GREATER,
    TOKEN_GREATER_EQUAL,
    TOKEN_LESS_EQUAL,
    TOKEN_AND,
    TOKEN_AMPERSAND,
    TOKEN_OR,
    TOKEN_LBRACKET, // [
    TOKEN_RBRACKET,  // ]
    TOKEN_DOT,

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
    TOKEN_KW_FOR,
    TOKEN_KW_USING,
    TOKEN_KW_EXPORT,
    TOKEN_SWAP,
    TOKEN_KW_ENTITY,
    TOKEN_KW_METHOD,
    TOKEN_KW_INIT,
    TOKEN_KW_EXTEND,
    TOKEN_KW_DEFER,
    TOKEN_KW_WATCH,
    TOKEN_KW_ON,
    TOKEN_KW_CHANGE,
    TOKEN_KW_BREAK,
    TOKEN_KW_CONTINUE,
    TOKEN_KW_REPEAT,
    TOKEN_KW_IN,
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
    bool is_float;

    int64_t int_value;
    double double_value;
    bool bool_value;
    char char_value;
};

void Token_init(struct Lexer *lexer, const char *source);
struct Token lexer_next(struct Lexer *lexer);
const char *lexer_last_error_hint(void);
void lexer_set_error_hint(const char *msg);

#endif
