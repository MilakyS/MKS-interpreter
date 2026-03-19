#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Lexer/lexer.h"
#include "Parser/parser.h"
#include "Eval/eval.h"


bool debug_mode = false;




char* read_file(const char* filename) {
    FILE *file = fopen(filename, "rb");
    if (!file) {
        printf("Error: Could not open file '%s'\n", filename);
        return NULL;
    }

    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    fseek(file, 0, SEEK_SET);

    char *buffer = malloc(length + 1);
    if (!buffer) {
        printf("Error: Memory allocation failed\n");
        fclose(file);
        return NULL;
    }

    fread(buffer, 1, length, file);
    buffer[length] = '\0';

    fclose(file);
    return buffer;
}

int main(int argc, char **argv) {

    if (argc < 2) {
        printf("Monkey Kernel Syntax (MKS) Interpreter\n");
        printf("Usage: %s <filename.mks>\n", argv[0]);
        return 1;
    }

    const char *filename = argv[1];
    char *source = read_file(filename);

    if (source == NULL) {
        return 1;
    }
    if (argc >= 3 && strcmp(argv[2], "-d") == 0) {
        debug_mode = true;
    }


    struct Lexer lexer;
    Token_init(&lexer, source);

    Parser parser;
    parser_init(&parser, &lexer, NULL);

    Environment env;
    env_init(&env);

    while (parser.current_token != NULL && parser.current_token->type != TOKEN_EOF) {
        ASTNode *stmt = parser_parse(&parser);

        if (stmt != NULL) {
            eval(stmt, &env);
        } else {
            break;
        }
    }

    free(source);

    return 0;
}