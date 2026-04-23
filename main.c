#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Eval/eval.h"
#include "GC/gc.h"
#include "Lexer/lexer.h"
#include "Parser/parser.h"
#include "Runtime/context.h"
#include "Runtime/errors.h"
#include "Runtime/runner.h"

static int run_repl(MKSContext *ctx) {
    char line[2048];
    Environment *env = mks_create_global_env(ctx);

    runtime_set_file("<repl>");
    printf(":mks> type code, Ctrl+D to exit\n");
    while (fgets(line, sizeof(line), stdin)) {
        const int was_active = ctx->error_active;
        if (setjmp(ctx->error_jmp) != 0) {
            ctx->error_active = was_active;
            continue;
        }
        ctx->error_active = 1;
        ctx->error_status = 0;

        runtime_set_source(line);
        struct Lexer lexer;
        Token_init(&lexer, line);

        Parser parser;
        parser_init(&parser, &lexer);
        ASTNode *program = parser_parse_program(&parser);
        if (program != NULL) {
            eval(program, env);
        }
        delete_ast_node(program);
        ctx->error_active = was_active;
    }

    gc_collect(env, env);
    gc_pop_env();
    return 0;
}

int main(const int argc, char **argv) {
    if (argc < 2) {
        printf("Monkey Kernel Syntax (MKS) Interpreter\n");
        printf("Usage: %s <file.mks>\n", argv[0]);
        printf("       %s --repl\n", argv[0]);
        printf("       %s --version\n", argv[0]);
        printf("       %s --help\n", argv[0]);
        printf("       %s --vm-test\n", argv[0]);
        return 1;
    }

    MKSContext root_context;
    mks_context_init(&root_context, 1024 * 1024);

    const char *gc_debug = getenv("MKS_GC_DEBUG");
    if (gc_debug != NULL && strcmp(gc_debug, "1") == 0) {
        gc_set_debug(1);
        fprintf(stderr, "[MKS] GC debug enabled\n");
    }

    int argi = 1;
    int profile_flag = 0;

    if (strcmp(argv[argi], "--profile") == 0) {
        profile_flag = 1;
        argi++;
        if (argc <= argi) {
            fprintf(stderr, "Usage: %s --profile <file.mks>\n", argv[0]);
            mks_context_dispose(&root_context);
            return 1;
        }
    }

    if (strcmp(argv[argi], "--version") == 0) {
        printf("mks %s\n", "0.1.0");
        mks_context_dispose(&root_context);
        return 0;
    }

    if (strcmp(argv[argi], "--help") == 0) {
        printf("Usage: %s <file.mks> | --repl | --version | --help | --vm-test | --profile <file.mks>\n", argv[0]);
        printf("Options:\n");
        printf("  --repl       Start interactive REPL\n");
        printf("  --version    Show version\n");
        printf("  --help       Show this help\n");
        printf("  --vm-test    Internal VM test mode\n");
        printf("  --profile    Run file with simple AST profiler\n");
        mks_context_dispose(&root_context);
        return 0;
    }

    int status = 0;
    if (strcmp(argv[argi], "--repl") == 0) {
        status = run_repl(&root_context);
    } else {
        status = mks_run_file(&root_context, argv[argi], 1, profile_flag);
    }

    mks_context_dispose(&root_context);
    return status;
}
