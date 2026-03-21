#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#define _GNU_SOURCE
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

RuntimeValue native_Read(RuntimeValue *args, int arg_count) {
    if (arg_count > 0 && args[0].type == VAL_STRING) {
        printf("%s", args[0].data.string_value);
        fflush(stdout);
    }

    if (arg_count > 0 && args[0].type == VAL_INT && args[0].data.int_value == 0) {
        static char word[256];
        if (scanf("%255s", word) == 1) {
            return make_string(word);
        }
        return make_string("");
    }

    char *line = NULL;
    size_t len = 0;
    if (getline(&line, &len, stdin) != -1) {
        if (line[strlen(line) - 1] == '\n') line[strlen(line) - 1] = '\0';
        RuntimeValue res = make_string(line);
        free(line);
        return res;
    }
    free(line);
    return make_string("");
}

void env_register_native(Environment *env, const char *name, NativeFn fn) {
    RuntimeValue val;
    val.type = VAL_NATIVE_FUNC;
    val.data.native_func = fn;
    val.original_type = VAL_NATIVE_FUNC;
    env_set(env, name, val);
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
    env_register_native(&env, "Read", native_Read);


    ASTNode *program = parser_parse_program(&parser);
    if (program != NULL) {
        eval(program, &env);
    }


    env_free(&env);
    delete_ast_node(program);
    free(source);

    return 0;
}