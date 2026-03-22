#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>


#include "Lexer/lexer.h"
#include "Parser/parser.h"
#include "Eval/eval.h"
#include "GC/gc.h"
#include "env/env.h"

bool debug_mode = false;

char* read_file(const char* filename) {
    FILE *file = fopen(filename, "rb");
    if (!file) {
        printf("Error: Could not open file '%s'\n", filename);
        return NULL;
    }

    fseek(file, 0, SEEK_END);
    const long length = ftell(file);
    fseek(file, 0, SEEK_SET);

    char *buffer = (char*)malloc(length + 1);
    if (!buffer) {
        fclose(file);
        return NULL;
    }

    size_t read_size = fread(buffer, 1, length, file);
    buffer[read_size] = '\0';
    fclose(file);

    return buffer;
}

RuntimeValue native_Read(const RuntimeValue *args, const int arg_count) {
    if (arg_count > 0 && args[0].type == VAL_STRING) {
        if (args[0].data.managed_string && args[0].data.managed_string->data) {
            printf("%s", args[0].data.managed_string->data);
        }
        fflush(stdout);
    }

    char buffer[8192];
    buffer[0] = '\0';

    if (!fgets(buffer, sizeof(buffer), stdin)) {
        return make_string("");
    }

    size_t l = strlen(buffer);
    while (l > 0 && (buffer[l - 1] == '\n' || buffer[l - 1] == '\r')) {
        buffer[l - 1] = '\0';
        l--;
    }

    if (arg_count > 0 && args[0].type == VAL_INT && args[0].data.float_value == 0) {
        char word[256] = {0};
        sscanf(buffer, "%255s", word);
        return make_string(word);
    }

    return make_string(buffer);
}

void env_register_native(Environment *env, const char *name, NativeFn fn) {
    RuntimeValue val;
    val.type = VAL_NATIVE_FUNC;
    val.data.native_func = fn;
    val.original_type = VAL_NATIVE_FUNC;
    env_set(env, name, val);
}

int main(const int argc, char **argv) {
    if (argc < 2) {
        printf("Monkey Kernel Syntax (MKS) Interpreter\n");
        printf("Usage: %s <filename.mks>\n", argv[0]);
        return 1;
    }
    gc_init(1024 * 1024);

    char *source = read_file(argv[1]);
    if (!source) return 1;

    struct Lexer lexer;
    Token_init(&lexer, source);

    Parser parser;
    parser_init(&parser, &lexer);

    Environment *env = (Environment*)gc_alloc(sizeof(Environment), GC_OBJ_ENV);
    env_init(env);

    env_register_native(env, "Read", native_Read);

    ASTNode *program = parser_parse_program(&parser);
    if (program != NULL) {
        eval(program, env);
    }

    gc_collect(env, env);

    delete_ast_node(program);
    free(source);

    return 0;
}