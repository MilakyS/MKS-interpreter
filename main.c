#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "Lexer/lexer.h"
#include "Parser/parser.h"
#include "Eval/eval.h"
#include "GC/gc.h"
#include "env/env.h"
#include "Utils/hash.h"
#include "Runtime/output.h"

bool debug_mode = false;

static char *read_file(const char *filename) {
    FILE *file = fopen(filename, "rb");
    if (!file) {
        fprintf(stderr, "Error: Could not open file '%s'\n", filename);
        return NULL;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        fprintf(stderr, "Error: Could not seek file '%s'\n", filename);
        return NULL;
    }

    const long length = ftell(file);
    if (length < 0) {
        fclose(file);
        fprintf(stderr, "Error: Could not get size of file '%s'\n", filename);
        return NULL;
    }

    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        fprintf(stderr, "Error: Could not rewind file '%s'\n", filename);
        return NULL;
    }

    char *buffer = (char *)malloc((size_t)length + 1);
    if (!buffer) {
        fclose(file);
        fprintf(stderr, "Error: Out of memory while reading '%s'\n", filename);
        return NULL;
    }

    const size_t read_size = fread(buffer, 1, (size_t)length, file);
    if (ferror(file)) {
        fclose(file);
        free(buffer);
        fprintf(stderr, "Error: Failed to read file '%s'\n", filename);
        return NULL;
    }

    buffer[read_size] = '\0';
    fclose(file);
    return buffer;
}

static RuntimeValue native_Read(const RuntimeValue *args, const int arg_count) {
    if (arg_count > 0 && args[0].type == VAL_STRING) {
        if (args[0].data.managed_string != NULL &&
            args[0].data.managed_string->data != NULL) {
            fputs(args[0].data.managed_string->data, stdout);
        }
        fflush(stdout);
    }

    char buffer[8192];
    buffer[0] = '\0';

    if (!fgets(buffer, sizeof(buffer), stdin)) {
        return make_string("");
    }

    size_t len = strlen(buffer);
    while (len > 0 && (buffer[len - 1] == '\n' || buffer[len - 1] == '\r')) {
        buffer[len - 1] = '\0';
        len--;
    }

    if (arg_count > 0 &&
        args[0].type == VAL_INT &&
        args[0].data.float_value == 0) {
        char word[256] = {0};
        sscanf(buffer, "%255s", word);
        return make_string(word);
    }

    return make_string(buffer);
}

static void env_register_native(Environment *env, const char *name, NativeFn fn) {
    RuntimeValue val;
    val.type = VAL_NATIVE_FUNC;
    val.data.native_func = fn;
    val.original_type = VAL_NATIVE_FUNC;

    env_set_fast(env, name, get_hash(name), val);
}

int main(const int argc, char **argv) {
    if (argc < 2) {
        printf("Monkey Kernel Syntax (MKS) Interpreter\n");
        printf("Usage: %s <filename.mks>\n", argv[0]);
        printf("       %s --vm-test\n", argv[0]);
        return 1;
    }

    gc_init(1024 * 1024);
    const char *gc_debug = getenv("MKS_GC_DEBUG");
    if (gc_debug != NULL && strcmp(gc_debug, "1") == 0) {
        gc_set_debug(1);
        fprintf(stderr, "[MKS] GC debug enabled\n");
    }

    char *source = read_file(argv[1]);
    if (!source) {
        return 1;
    }

    struct Lexer lexer;
    Token_init(&lexer, source);

    Parser parser;
    parser_init(&parser, &lexer);

    Environment *env = (Environment *)gc_alloc(sizeof(Environment), GC_OBJ_ENV);
    env_init(env);
    gc_push_env(env);

    env_register_native(env, "Read", native_Read);

    ASTNode *program = parser_parse_program(&parser);
    if (program != NULL) {
        eval(program, env);
    }

    gc_collect(env, env);

    gc_pop_env();
    delete_ast_node(program);
    free(source);

    return 0;
}