#include "lexer.h"
#include "../Runtime/context.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

static void advance(struct Lexer *lexer) {
    if (lexer->current_char == '\n') {
        lexer->line++;
    }

    lexer->position++;
    lexer->current_char = lexer->source[lexer->position];
}

void Token_init(struct Lexer *lexer, const char *source) {
    lexer->source = source;
    lexer->position = 0;
    lexer->line = 1;
    lexer->current_char = source[0];
    lexer_set_error_hint(NULL);
}

static void skip_whitespace(struct Lexer *lexer) {
    while (lexer->current_char != '\0') {
        const char next = lexer->source[lexer->position + 1];
        if (isspace((unsigned char)lexer->current_char)) {
            advance(lexer);
        } else if (lexer->current_char == '/' && next == '/') {
            while (lexer->current_char != '\n' && lexer->current_char != '\0') {
                advance(lexer);
            }
        } else {
            break;
        }
    }
}

static struct Token make_token(const enum TokenType type,
                               const char *start,
                               const int length,
                               const int line) {
    struct Token token;
    token.type = type;
    token.line = line;
    token.start = start;
    token.length = length;
    token.is_float = false;
    token.int_value = 0;
    token.double_value = 0.0;
    return token;
}

static struct Token make_error_token(const char *start, const int length, const int line) {
    return make_token(TOKEN_ERROR, start, length, line);
}

const char *lexer_last_error_hint(void) {
    const char *hint = mks_context_current()->lexer_error_hint;
    return hint[0] ? hint : NULL;
}

void lexer_set_error_hint(const char *msg) {
    char *hint = mks_context_current()->lexer_error_hint;
    if (msg == NULL) {
        hint[0] = '\0';
        return;
    }
    strncpy(hint, msg, sizeof(mks_context_current()->lexer_error_hint) - 1);
    hint[sizeof(mks_context_current()->lexer_error_hint) - 1] = '\0';
}

static struct Token Read_Number(struct Lexer *lexer) {
    const char *start = lexer->source + lexer->position;
    const int line = lexer->line;

    while (isdigit((unsigned char)lexer->current_char)) {
        advance(lexer);
    }

    bool is_float = false;
    if (lexer->current_char == '.' &&
        isdigit((unsigned char)lexer->source[lexer->position + 1])) {
        is_float = true;
        advance(lexer);

        while (isdigit((unsigned char)lexer->current_char)) {
            advance(lexer);
        }
    }

    const int length = (int)((lexer->source + lexer->position) - start);

    struct Token token = make_token(TOKEN_TYPE_NUMBER, start, length, line);
    token.is_float = is_float;
    if (is_float) {
        token.double_value = strtod(start, NULL);
    } else {
        errno = 0;
        char *end = NULL;
        long long parsed = strtoll(start, &end, 10);
        if (errno == 0 && end == start + length) {
            token.int_value = (int64_t)parsed;
            token.double_value = (double)parsed;
        } else {
            token.is_float = true;
            token.double_value = strtod(start, NULL);
        }
    }
    return token;
}

static struct Token Read_String(struct Lexer *lexer) {
    const int line = lexer->line;

    advance(lexer);
    const char *start = lexer->source + lexer->position;

    while (lexer->current_char != '\0') {
        if (lexer->current_char == '\\') {
            advance(lexer);
            if (lexer->current_char != '\0') {
                advance(lexer);
            }
            continue;
        }

        if (lexer->current_char == '"') {
            break;
        }

        advance(lexer);
    }

    const int length = (int)((lexer->source + lexer->position) - start);

    if (lexer->current_char == '\0') {
        lexer_set_error_hint("Unterminated string literal — add a closing quote \"");
        return make_error_token(start, length, line);
    }

    advance(lexer);
    lexer_set_error_hint(NULL);
    return make_token(TOKEN_TYPE_STRING, start, length, line);
}

static struct Token Read_IdentifierOrKeyword(struct Lexer *lexer) {
    const char *start = lexer->source + lexer->position;
    const int line = lexer->line;

    while (isalnum((unsigned char)lexer->current_char) || lexer->current_char == '_') {
        advance(lexer);
    }

    const int length = (int)((lexer->source + lexer->position) - start);

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
        {"export", TOKEN_KW_EXPORT},
        {"entity", TOKEN_KW_ENTITY},
        {"method", TOKEN_KW_METHOD},
        {"init", TOKEN_KW_INIT},
        {"extend", TOKEN_KW_EXTEND},
        {"defer", TOKEN_KW_DEFER},
        {"watch", TOKEN_KW_WATCH},
        {"on", TOKEN_KW_ON},
        {"change", TOKEN_KW_CHANGE},
        {"break", TOKEN_KW_BREAK},
        {"continue", TOKEN_KW_CONTINUE},
        {"repeat", TOKEN_KW_REPEAT},
        {"in", TOKEN_KW_IN},
        {NULL, TOKEN_IDENTIFIER}
    };

    enum TokenType type = TOKEN_IDENTIFIER;

    for (int i = 0; keywords[i].kw != NULL; i++) {
        if (length == (int)strlen(keywords[i].kw) &&
            strncmp(start, keywords[i].kw, (size_t)length) == 0) {
            type = keywords[i].type;
            break;
        }
    }

    return make_token(type, start, length, line);
}

struct Token lexer_next(struct Lexer *lexer) {
    skip_whitespace(lexer);

    const char *start = lexer->source + lexer->position;
    const char next = lexer->current_char ? lexer->source[lexer->position + 1] : '\0';

    if (lexer->current_char == '\0') {
        return make_token(TOKEN_EOF, start, 0, lexer->line);
    }

    const char c = lexer->current_char;
    const int line = lexer->line;

    switch (c) {
        case '=':
            if (next == ':') {
                advance(lexer);
                advance(lexer);
                lexer_set_error_hint(NULL);
                return make_token(TOKEN_ASSIGN, start, 2, line);
            }
            advance(lexer);
                lexer_set_error_hint("Use =: for assignment");
            return make_error_token(start, 1, line);

        case '-':
            if (next == '>') {
                advance(lexer);
                advance(lexer);
                return make_token(TOKEN_BLOCK_START, start, 2, line);
            }
            advance(lexer);
            return make_token(TOKEN_MINUS, start, 1, line);

        case '<':
            if (next == '-') {
                if (lexer->source[lexer->position + 2] == '-' && lexer->source[lexer->position + 3] == '>') {
                    advance(lexer); advance(lexer); advance(lexer); advance(lexer);
                    return make_token(TOKEN_SWAP, start, 4, line);
                }
                advance(lexer);
                advance(lexer);
                return make_token(TOKEN_BLOCK_END, start, 2, line);
            }
            if (next == '=') {
                advance(lexer);
                advance(lexer);
                return make_token(TOKEN_LESS_EQUAL, start, 2, line);
            }
            advance(lexer);
            return make_token(TOKEN_LESS, start, 1, line);

        case '>':
            if (next == '=') {
                advance(lexer);
                advance(lexer);
                return make_token(TOKEN_GREATER_EQUAL, start, 2, line);
            }
            advance(lexer);
            return make_token(TOKEN_GREATER, start, 1, line);

        case '?':
            if (next == '=') {
                advance(lexer);
                advance(lexer);
                lexer_set_error_hint(NULL);
                return make_token(TOKEN_EQ, start, 2, line);
            }
            advance(lexer);
            lexer_set_error_hint("Expected comparison operator '?='");
            return make_error_token(start, 1, line);

        case '!':
            if (next == '?') {
                advance(lexer);
                advance(lexer);
                lexer_set_error_hint(NULL);
                return make_token(TOKEN_NOT_EQ, start, 2, line);
            }
            advance(lexer);
            lexer_set_error_hint("Use '!?', or remove '!'");
            return make_error_token(start, 1, line);

        case '&':
            if (next == '&') {
                advance(lexer);
                advance(lexer);
                return make_token(TOKEN_AND, start, 2, line);
            }
            advance(lexer);
            return make_token(TOKEN_AMPERSAND, start, 1, line);

        case '|':
            if (next == '|') {
                advance(lexer);
                advance(lexer);
                return make_token(TOKEN_OR, start, 2, line);
            }
            advance(lexer);
            return make_error_token(start, 1, line);

        case '+':
            advance(lexer);
            return make_token(TOKEN_PLUS, start, 1, line);

        case '*':
            advance(lexer);
            return make_token(TOKEN_STAR, start, 1, line);

        case '/':
            advance(lexer);
            return make_token(TOKEN_SLASH, start, 1, line);

        case '%':
            advance(lexer);
            return make_token(TOKEN_MOD, start, 1, line);

        case ',':
            advance(lexer);
            return make_token(TOKEN_COMMA, start, 1, line);

        case ';':
            advance(lexer);
            return make_token(TOKEN_SEMICOLON, start, 1, line);

        case '(':
            advance(lexer);
            return make_token(TOKEN_LPAREL, start, 1, line);

        case ')':
            advance(lexer);
            return make_token(TOKEN_RPAREL, start, 1, line);

        case '[':
            advance(lexer);
            return make_token(TOKEN_LBRACKET, start, 1, line);

        case ']':
            advance(lexer);
            return make_token(TOKEN_RBRACKET, start, 1, line);

        case '.':
            advance(lexer);
            return make_token(TOKEN_DOT, start, 1, line);

        case '"':
            return Read_String(lexer);

        default:
            if (isalpha((unsigned char)c) || c == '_') {
                lexer_set_error_hint(NULL);
                return Read_IdentifierOrKeyword(lexer);
            }

            if (isdigit((unsigned char)c)) {
                lexer_set_error_hint(NULL);
                return Read_Number(lexer);
            }

            advance(lexer);
            lexer_set_error_hint("Unknown character — check for typos or stray symbols");
            return make_error_token(start, 1, line);
    }
}
