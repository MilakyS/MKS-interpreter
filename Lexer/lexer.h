#ifndef CMINUSINTERPRETATOR_LEXER_H
#define CMINUSINTERPRETATOR_LEXER_H
#include <stddef.h>
#include <stdbool.h>

enum TokenType {
    TOKEN_EOF,
    TOKEN_ERROR,
    TOKEN_IDENTIFIER,

    // Var types
    TOKEN_TYPE_NUMBER,
    TOKEN_TYPE_STRING,
    TOKEN_TYPE_CHAR,
    TOKEN_TYPE_BOOL,
    TOKEN_TYPE_NULL,

    //Operators
    TOKEN_ASSIGN,  // =:
    TOKEN_EQ,  // =?
    TOKEN_NOT_EQ, // !?
    TOKEN_PLUS,
    TOKEN_MINUS,


    TOKEN_LPAREL,
    TOKEN_RPAREL,
    TOKEN_COLON,
    TOKEN_SEMICOLON,
    TOKEN_COMMA,
    TOKEN_INCREMENT,


    // Keywords
    TOKEN_KW_WRITELN,
    TOKEN_KW_VAR,
    TOKEN_KW_IF,
    TOKEN_KW_ELSE,
    TOKEN_KW_WHILE,

    TOKEN_BLOCK_START, // ->
    TOKEN_BLOCK_END, // <-

};

struct Token {
    enum TokenType type;
    int line;
    char *lexeme;
    union {
        int int_value;
        float float_value;
        char *string_value;
        char char_value;
        bool bool_value;
    };
};

struct Lexer {
    const char *source;
    size_t position;
    char current_char;
    int line;
};


void Token_init(struct Lexer *lexer, const char *source);

void advance(struct Lexer *lexer);

char peek(const struct Lexer *lexer);

struct Token Read_Number(struct Lexer *lexer);

struct Token Read_Keywords(struct Lexer *lexer);

struct Token make_token(const int type, char *lexeme, const int line);

struct Token lexer_next(struct Lexer *lexer);




#endif //CMINUSINTERPRETATOR_LEXER_H