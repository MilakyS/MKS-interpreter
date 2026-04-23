#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "../../Eval/eval.h"
#include "../../GC/gc.h"
#include "../../Lexer/lexer.h"
#include "../../Parser/parser.h"
#include "../../Runtime/context.h"
#include "../../Runtime/module.h"
#include "../../Runtime/runner.h"
#include "../../Runtime/value.h"
#include "../../Utils/hash.h"

typedef void (*TestFn)(void);

static int tests_failed = 0;

#define ASSERT_TRUE(expr) \
    do { \
        if (!(expr)) { \
            fprintf(stderr, "%s:%d: assertion failed: %s\n", __FILE__, __LINE__, #expr); \
            tests_failed++; \
            return; \
        } \
    } while (0)

#define ASSERT_EQ_INT(expected, actual) \
    do { \
        long long expected__ = (long long)(expected); \
        long long actual__ = (long long)(actual); \
        if (expected__ != actual__) { \
            fprintf(stderr, "%s:%d: expected %lld, got %lld\n", __FILE__, __LINE__, expected__, actual__); \
            tests_failed++; \
            return; \
        } \
    } while (0)

#define ASSERT_EQ_DOUBLE(expected, actual) \
    do { \
        double expected__ = (double)(expected); \
        double actual__ = (double)(actual); \
        double diff__ = expected__ > actual__ ? expected__ - actual__ : actual__ - expected__; \
        if (diff__ > 0.000001) { \
            fprintf(stderr, "%s:%d: expected %.12g, got %.12g\n", __FILE__, __LINE__, expected__, actual__); \
            tests_failed++; \
            return; \
        } \
    } while (0)

static ASTNode *parse_expression(const char *source) {
    struct Lexer lexer;
    Token_init(&lexer, source);

    Parser parser;
    parser_init(&parser, &lexer);
    return parser_parse_expression(&parser);
}

static void test_lexer_distinguishes_int_and_float(void) {
    struct Lexer lexer;
    Token_init(&lexer, "9007199254740993 12.5");

    struct Token first = lexer_next(&lexer);
    ASSERT_EQ_INT(TOKEN_TYPE_NUMBER, first.type);
    ASSERT_TRUE(!first.is_float);
    ASSERT_EQ_INT(9007199254740993LL, first.int_value);

    struct Token second = lexer_next(&lexer);
    ASSERT_EQ_INT(TOKEN_TYPE_NUMBER, second.type);
    ASSERT_TRUE(second.is_float);
    ASSERT_EQ_DOUBLE(12.5, second.double_value);
}

static void test_eval_preserves_large_int_precision(void) {
    MKSContext ctx;
    mks_context_init(&ctx, 1024 * 1024);

    ASTNode *expr = parse_expression("9007199254740993");
    RuntimeValue value = eval(expr, NULL);

    ASSERT_EQ_INT(VAL_INT, value.type);
    ASSERT_EQ_INT(9007199254740993LL, value.data.int_value);

    delete_ast_node(expr);
    mks_context_dispose(&ctx);
}

static void test_eval_float_arithmetic(void) {
    MKSContext ctx;
    mks_context_init(&ctx, 1024 * 1024);

    ASTNode *expr = parse_expression("1.5 + 2");
    RuntimeValue value = eval(expr, NULL);

    ASSERT_EQ_INT(VAL_FLOAT, value.type);
    ASSERT_EQ_DOUBLE(3.5, value.data.float_value);

    delete_ast_node(expr);
    mks_context_dispose(&ctx);
}

static void test_gc_root_scope_restores_stack(void) {
    MKSContext ctx;
    mks_context_init(&ctx, 1024 * 1024);

    RuntimeValue value = make_string("still rooted");
    int before = mks_gc.roots_count;

    {
        MKS_GC_ROOTS(scope);
        MKS_GC_ROOT(&value);
        ASSERT_EQ_INT(before + 1, mks_gc.roots_count);
        gc_collect(NULL, NULL);
        ASSERT_EQ_INT(VAL_STRING, value.type);
        ASSERT_TRUE(value.data.managed_string != NULL);
        ASSERT_TRUE(strcmp(value.data.managed_string->data, "still rooted") == 0);
        MKS_GC_ROOTS_END(scope);
    }

    ASSERT_EQ_INT(before, mks_gc.roots_count);
    mks_context_dispose(&ctx);
}

static MKSContext *expected_native_ctx = NULL;

static RuntimeValue native_ctx_probe(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    (void)args;
    (void)arg_count;
    return make_int(ctx == expected_native_ctx ? 1 : 0);
}

static void test_native_function_receives_context(void) {
    MKSContext ctx;
    mks_context_init(&ctx, 1024 * 1024);
    expected_native_ctx = &ctx;

    Environment *env = mks_create_global_env(&ctx);
    module_bind_native(make_object(env), "ctx_probe", native_ctx_probe);

    ASTNode *expr = parse_expression("ctx_probe()");
    RuntimeValue value = eval(expr, env);

    ASSERT_EQ_INT(VAL_INT, value.type);
    ASSERT_EQ_INT(1, value.data.int_value);

    delete_ast_node(expr);
    gc_pop_env();
    mks_context_dispose(&ctx);
    expected_native_ctx = NULL;
}

static void test_runner_returns_status_on_syntax_error(void) {
    MKSContext ctx;
    mks_context_init(&ctx, 1024 * 1024);

    int status = mks_run_source(&ctx, "<unit-syntax-error>", "var x =: ;", 0, 0);
    ASSERT_EQ_INT(1, status);

    mks_context_dispose(&ctx);
}

static void test_runner_can_run_source_after_error_in_new_context(void) {
    MKSContext bad_ctx;
    mks_context_init(&bad_ctx, 1024 * 1024);
    ASSERT_EQ_INT(1, mks_run_source(&bad_ctx, "<bad>", "var x =: ;", 0, 0));
    mks_context_dispose(&bad_ctx);

    MKSContext good_ctx;
    mks_context_init(&good_ctx, 1024 * 1024);
    ASSERT_EQ_INT(0, mks_run_source(&good_ctx, "<good>", "var x =: 1 + 2;", 0, 0));
    mks_context_dispose(&good_ctx);
}

static void write_test_file(const char *path, const char *source) {
    FILE *f = fopen(path, "w");
    if (f == NULL) {
        fprintf(stderr, "failed to open %s for writing\n", path);
        tests_failed++;
        return;
    }
    fputs(source, f);
    fclose(f);
}

static void test_module_namespace_export_access(void) {
    mkdir("/tmp/mks_unit_modules", 0777);
    write_test_file("/tmp/mks_unit_modules/lib_export.mks",
                    "var hidden =: 7;\n"
                    "export fnc value() -> return hidden; <-\n");

    MKSContext ctx;
    mks_context_init(&ctx, 1024 * 1024);
    ASSERT_EQ_INT(0, mks_run_source(&ctx,
                                    "/tmp/mks_unit_modules/main_export.mks",
                                    "using \"lib_export.mks\" as lib;\n"
                                    "expect(lib.value() ?= 7);\n",
                                    0,
                                    0));
    mks_context_dispose(&ctx);
}

static void test_module_skips_top_level_executable_statement(void) {
    mkdir("/tmp/mks_unit_modules", 0777);
    write_test_file("/tmp/mks_unit_modules/lib_top_level.mks",
                    "Writeln(\"should not load\");\n"
                    "export fnc value() -> return 1; <-\n");

    MKSContext ctx;
    mks_context_init(&ctx, 1024 * 1024);
    ASSERT_EQ_INT(0, mks_run_source(&ctx,
                                    "/tmp/mks_unit_modules/main_top_level.mks",
                                    "using \"lib_top_level.mks\" as lib;\n"
                                    "expect(lib.value() ?= 1);\n",
                                    0,
                                    0));
    mks_context_dispose(&ctx);
}

static void test_module_private_symbol_is_not_exported(void) {
    mkdir("/tmp/mks_unit_modules", 0777);
    write_test_file("/tmp/mks_unit_modules/lib_private.mks",
                    "var hidden =: 42;\n"
                    "export fnc visible() -> return hidden; <-\n");

    MKSContext ctx;
    mks_context_init(&ctx, 1024 * 1024);
    ASSERT_EQ_INT(1, mks_run_source(&ctx,
                                    "/tmp/mks_unit_modules/main_private.mks",
                                    "using \"lib_private.mks\" as lib;\n"
                                    "lib.hidden;\n",
                                    0,
                                    0));
    mks_context_dispose(&ctx);
}

static void test_module_cycle_returns_loading_namespace(void) {
    mkdir("/tmp/mks_unit_modules", 0777);
    write_test_file("/tmp/mks_unit_modules/cycle_a.mks",
                    "using \"cycle_b.mks\" as b;\n"
                    "export var a_value =: 1;\n");
    write_test_file("/tmp/mks_unit_modules/cycle_b.mks",
                    "using \"cycle_a.mks\" as a;\n"
                    "export var b_value =: 2;\n");

    MKSContext ctx;
    mks_context_init(&ctx, 1024 * 1024);
    ASSERT_EQ_INT(0, mks_run_source(&ctx,
                                    "/tmp/mks_unit_modules/main_cycle.mks",
                                    "using \"cycle_a.mks\" as a;\n"
                                    "expect(a.a_value ?= 1);\n",
                                    0,
                                    0));
    mks_context_dispose(&ctx);
}

static void test_pointer_mutates_variable(void) {
    MKSContext ctx;
    mks_context_init(&ctx, 1024 * 1024);
    ASSERT_EQ_INT(0, mks_run_source(&ctx,
                                    "<pointer-var>",
                                    "var x =: 10;\n"
                                    "var p =: &x;\n"
                                    "expect(*p ?= 10);\n"
                                    "*p =: 20;\n"
                                    "expect(x ?= 20);\n",
                                    0,
                                    0));
    mks_context_dispose(&ctx);
}

static void test_pointer_allows_function_to_mutate_caller_variable(void) {
    MKSContext ctx;
    mks_context_init(&ctx, 1024 * 1024);
    ASSERT_EQ_INT(0, mks_run_source(&ctx,
                                    "<pointer-function>",
                                    "fnc inc(p) ->\n"
                                    "    *p =: *p + 1;\n"
                                    "<-\n"
                                    "var x =: 1;\n"
                                    "inc(&x);\n"
                                    "expect(x ?= 2);\n",
                                    0,
                                    0));
    mks_context_dispose(&ctx);
}

static void test_pointer_swap(void) {
    MKSContext ctx;
    mks_context_init(&ctx, 1024 * 1024);
    ASSERT_EQ_INT(0, mks_run_source(&ctx,
                                    "<pointer-swap>",
                                    "fnc swap(a, b) ->\n"
                                    "    var t =: *a;\n"
                                    "    *a =: *b;\n"
                                    "    *b =: t;\n"
                                    "<-\n"
                                    "var x =: 1;\n"
                                    "var y =: 2;\n"
                                    "swap(&x, &y);\n"
                                    "expect(x ?= 2);\n"
                                    "expect(y ?= 1);\n",
                                    0,
                                    0));
    mks_context_dispose(&ctx);
}

static void test_pointer_mutates_array_element(void) {
    MKSContext ctx;
    mks_context_init(&ctx, 1024 * 1024);
    ASSERT_EQ_INT(0, mks_run_source(&ctx,
                                    "<pointer-array>",
                                    "var xs =: [1, 2, 3];\n"
                                    "var p =: &xs[1];\n"
                                    "*p =: 9;\n"
                                    "expect(xs[1] ?= 9);\n",
                                    0,
                                    0));
    mks_context_dispose(&ctx);
}

static void test_pointer_mutates_object_field(void) {
    MKSContext ctx;
    mks_context_init(&ctx, 1024 * 1024);
    ASSERT_EQ_INT(0, mks_run_source(&ctx,
                                    "<pointer-object>",
                                    "var obj =: Object();\n"
                                    "obj.value =: 5;\n"
                                    "var p =: &obj.value;\n"
                                    "*p =: 8;\n"
                                    "expect(obj.value ?= 8);\n",
                                    0,
                                    0));
    mks_context_dispose(&ctx);
}

static void test_pointer_rejects_temporary_address(void) {
    MKSContext ctx;
    mks_context_init(&ctx, 1024 * 1024);
    ASSERT_EQ_INT(1, mks_run_source(&ctx,
                                    "<pointer-temporary>",
                                    "var p =: &(1 + 2);\n",
                                    0,
                                    0));
    mks_context_dispose(&ctx);
}

static void test_pointer_rejects_non_pointer_deref(void) {
    MKSContext ctx;
    mks_context_init(&ctx, 1024 * 1024);
    ASSERT_EQ_INT(1, mks_run_source(&ctx,
                                    "<pointer-non-pointer>",
                                    "var x =: 1;\n"
                                    "Writeln(*x);\n",
                                    0,
                                    0));
    mks_context_dispose(&ctx);
}

static void run_test(const char *name, TestFn fn) {
    int before = tests_failed;
    fn();
    if (tests_failed == before) {
        printf("[UNIT PASS] %s\n", name);
    } else {
        printf("[UNIT FAIL] %s\n", name);
    }
}

int main(void) {
    run_test("lexer distinguishes int and float", test_lexer_distinguishes_int_and_float);
    run_test("eval preserves large int precision", test_eval_preserves_large_int_precision);
    run_test("eval float arithmetic", test_eval_float_arithmetic);
    run_test("gc root scope restores stack", test_gc_root_scope_restores_stack);
    run_test("native function receives context", test_native_function_receives_context);
    run_test("runner returns status on syntax error", test_runner_returns_status_on_syntax_error);
    run_test("runner can run source after error in new context", test_runner_can_run_source_after_error_in_new_context);
    run_test("module namespace export access", test_module_namespace_export_access);
    run_test("module skips top-level executable statement", test_module_skips_top_level_executable_statement);
    run_test("module private symbol is not exported", test_module_private_symbol_is_not_exported);
    run_test("module cycle returns loading namespace", test_module_cycle_returns_loading_namespace);
    run_test("pointer mutates variable", test_pointer_mutates_variable);
    run_test("pointer allows function to mutate caller variable", test_pointer_allows_function_to_mutate_caller_variable);
    run_test("pointer swap", test_pointer_swap);
    run_test("pointer mutates array element", test_pointer_mutates_array_element);
    run_test("pointer mutates object field", test_pointer_mutates_object_field);
    run_test("pointer rejects temporary address", test_pointer_rejects_temporary_address);
    run_test("pointer rejects non-pointer deref", test_pointer_rejects_non_pointer_deref);

    if (tests_failed != 0) {
        fprintf(stderr, "%d unit test(s) failed\n", tests_failed);
        return 1;
    }

    printf("[UNIT] all tests passed\n");
    return 0;
}
