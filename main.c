#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "Lexer/lexer.h"
#include "Parser/parser.h"
#include "Eval/eval.h"

bool debug_mode = false;

char* read_file(const char* filename) {
    FILE *file = fopen(filename, "rb");
    if (!file) { printf("Error: Could not open file '%s'\n", filename); return NULL; }
    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    fseek(file, 0, SEEK_SET);
    char *buffer = malloc(length + 1);
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

    char *source = read_file(argv[1]);
    if (!source) return 1;

    struct Lexer lexer;
    Token_init(&lexer, source);

    Parser parser;
    parser_init(&parser, &lexer);

    Environment env;
    env_init(&env);


    ASTNode *program = parser_parse_program(&parser);
    if (program != NULL) {
        eval(program, &env);
    }

    // Очистка памяти
    env_free(&env);
    delete_ast_node(program);
    free(source);

    return 0;
}