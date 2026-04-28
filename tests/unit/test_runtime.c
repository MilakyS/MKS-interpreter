#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <stdint.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../../Eval/eval.h"
#include "../../GC/gc.h"
#include "../../Lexer/lexer.h"
#include "../../Parser/parser.h"
#include "../../Runtime/context.h"
#include "../../Runtime/module.h"
#include "../../Runtime/operators.h"
#include "../../Runtime/runner.h"
#include "../../Runtime/value.h"
#include "../../Utils/hash.h"

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

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

static ASTNode *parse_program_source(const char *source) {
    struct Lexer lexer;
    Token_init(&lexer, source);

    Parser parser;
    parser_init(&parser, &lexer);
    return parser_parse_program(&parser);
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

static int native_string_counter = 0;

static RuntimeValue native_make_gc_string(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    (void)ctx;
    (void)args;
    (void)arg_count;

    native_string_counter++;
    return native_string_counter == 1 ? make_string("first") : make_string("second");
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

static void write_u32_le_test(FILE *f, uint32_t value) {
    unsigned char b[4] = {
        (unsigned char)(value & 0xff),
        (unsigned char)((value >> 8) & 0xff),
        (unsigned char)((value >> 16) & 0xff),
        (unsigned char)((value >> 24) & 0xff)
    };
    fwrite(b, 1, sizeof(b), f);
}

static void write_u64_le_test(FILE *f, uint64_t value) {
    unsigned char b[8] = {
        (unsigned char)(value & 0xff),
        (unsigned char)((value >> 8) & 0xff),
        (unsigned char)((value >> 16) & 0xff),
        (unsigned char)((value >> 24) & 0xff),
        (unsigned char)((value >> 32) & 0xff),
        (unsigned char)((value >> 40) & 0xff),
        (unsigned char)((value >> 48) & 0xff),
        (unsigned char)((value >> 56) & 0xff)
    };
    fwrite(b, 1, sizeof(b), f);
}

static void write_mkspkg_entry(FILE *f, const char *path, const char *source) {
    size_t path_len = strlen(path);
    size_t source_len = strlen(source);
    write_u32_le_test(f, (uint32_t)path_len);
    write_u64_le_test(f, (uint64_t)source_len);
    fwrite(path, 1, path_len, f);
    fwrite(source, 1, source_len, f);
}

static void write_mkspkg_file(const char *path) {
    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        fprintf(stderr, "failed to open %s for writing\n", path);
        tests_failed++;
        return;
    }
    fwrite("MKSPKG1\n", 1, 8, f);
    write_mkspkg_entry(f,
                       "package.pkg",
                       "package \"packed.file\";\n"
                       "version \"1.0.0\";\n"
                       "lib \"src/lib.mks\";\n");
    write_mkspkg_entry(f,
                       "src/lib.mks",
                       "using \"./sub/value\" as value;\n"
                       "export fnc root() -> return value.item(); <-\n");
    write_mkspkg_entry(f,
                       "src/sub/value.mks",
                       "export fnc item() -> return \"packed-file\"; <-\n");
    write_u32_le_test(f, 0);
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

static void test_module_exported_closure_survives_gc_after_import(void) {
    mkdir("/tmp/mks_unit_modules", 0777);
    write_test_file("/tmp/mks_unit_modules/lib_gc_closure.mks",
                    "var hidden =: 7;\n"
                    "export fnc value() -> return hidden; <-\n");

    MKSContext ctx;
    mks_context_init(&ctx, 1024 * 1024);
    mks_context_set_current(&ctx);

    Environment *env = mks_create_global_env(&ctx);
    RuntimeValue exports = module_import("/tmp/mks_unit_modules/lib_gc_closure.mks", "lib", true, false, env);
    ASSERT_EQ_INT(VAL_MODULE, exports.type);

    gc_collect(env, env);

    ASTNode *expr = parse_expression("lib.value()");
    RuntimeValue value = eval(expr, env);
    ASSERT_EQ_INT(VAL_INT, value.type);
    ASSERT_EQ_INT(7, value.data.int_value);

    delete_ast_node(expr);
    mks_context_dispose(&ctx);
}

static void test_module_reimport_reuses_cached_namespace_object(void) {
    mkdir("/tmp/mks_unit_modules", 0777);
    write_test_file("/tmp/mks_unit_modules/lib_cached.mks",
                    "export var answer =: 42;\n");

    MKSContext ctx;
    mks_context_init(&ctx, 1024 * 1024);
    mks_context_set_current(&ctx);

    Environment *env = mks_create_global_env(&ctx);
    RuntimeValue first = module_import("/tmp/mks_unit_modules/lib_cached.mks", "one", true, false, env);
    RuntimeValue second = module_import("/tmp/mks_unit_modules/lib_cached.mks", "two", true, false, env);

    ASSERT_EQ_INT(VAL_MODULE, first.type);
    ASSERT_EQ_INT(VAL_MODULE, second.type);
    ASSERT_TRUE(first.data.obj_env == second.data.obj_env);
    ASSERT_TRUE(runtime_value_equals(first, second));

    mks_context_dispose(&ctx);
}

static void test_package_pkg_current_and_module_imports(void) {
    mkdir("/tmp/mks_unit_pkg", 0777);
    mkdir("/tmp/mks_unit_pkg/src", 0777);
    mkdir("/tmp/mks_unit_pkg/src/util", 0777);
    mkdir("/tmp/mks_unit_pkg/mks_modules", 0777);
    mkdir("/tmp/mks_unit_pkg/mks_modules/cool.lib", 0777);
    mkdir("/tmp/mks_unit_pkg/mks_modules/cool.lib/src", 0777);
    mkdir("/tmp/mks_unit_pkg/mks_modules/cool.lib/src/http", 0777);

    write_test_file("/tmp/mks_unit_pkg/package.pkg",
                    "package \"demo.app\";\n"
                    "version \"0.1.0\";\n"
                    "main \"src/main.mks\";\n"
                    "lib \"src/lib.mks\";\n");
    write_test_file("/tmp/mks_unit_pkg/src/lib.mks",
                    "export fnc root() -> return 10; <-\n");
    write_test_file("/tmp/mks_unit_pkg/src/util/strings.mks",
                    "export fnc label() -> return \"util\"; <-\n");
    write_test_file("/tmp/mks_unit_pkg/mks_modules/cool.lib/package.pkg",
                    "package \"cool.lib\";\n"
                    "version \"0.2.0\";\n"
                    "lib \"src/lib.mks\";\n");
    write_test_file("/tmp/mks_unit_pkg/mks_modules/cool.lib/src/lib.mks",
                    "export fnc value() -> return 20; <-\n");
    write_test_file("/tmp/mks_unit_pkg/mks_modules/cool.lib/src/http/client.mks",
                    "export fnc get() -> return \"client\"; <-\n");

    MKSContext ctx;
    mks_context_init(&ctx, 1024 * 1024);
    ASSERT_EQ_INT(0, mks_run_source(&ctx,
                                    "/tmp/mks_unit_pkg/src/main.mks",
                                    "using demo.app as app;\n"
                                    "using demo.app.util.strings as strings;\n"
                                    "using cool.lib as cool;\n"
                                    "using cool.lib.http.client as client;\n"
                                    "expect(app.root() ?= 10);\n"
                                    "expect(strings.label() ?= \"util\");\n"
                                    "expect(cool.value() ?= 20);\n"
                                    "expect(client.get() ?= \"client\");\n",
                                    0,
                                    0));
    mks_context_dispose(&ctx);
}

static void test_package_pkg_relative_current_file_imports(void) {
    char original_cwd[PATH_MAX];
    ASSERT_TRUE(getcwd(original_cwd, sizeof(original_cwd)) != NULL);

    mkdir("/tmp/mks_unit_pkg_relative", 0777);
    mkdir("/tmp/mks_unit_pkg_relative/src", 0777);
    mkdir("/tmp/mks_unit_pkg_relative/examples", 0777);

    write_test_file("/tmp/mks_unit_pkg_relative/package.pkg",
                    "package \"demo.rel\";\n"
                    "version \"0.1.0\";\n"
                    "lib \"src/lib.mks\";\n");
    write_test_file("/tmp/mks_unit_pkg_relative/src/lib.mks",
                    "export fnc value() -> return 9; <-\n");
    write_test_file("/tmp/mks_unit_pkg_relative/examples/demo.mks",
                    "using demo.rel as app;\n"
                    "expect(app.value() ?= 9);\n");

    ASSERT_EQ_INT(0, chdir("/tmp/mks_unit_pkg_relative/examples"));

    MKSContext ctx;
    mks_context_init(&ctx, 1024 * 1024);
    int status = mks_run_file(&ctx, "demo.mks", 0, 0);
    mks_context_dispose(&ctx);

    ASSERT_EQ_INT(0, chdir(original_cwd));
    ASSERT_EQ_INT(0, status);
}

static void test_mkspkg_artifact_imports(void) {
    mkdir("/tmp/mks_unit_pkg_artifact", 0777);
    mkdir("/tmp/mks_unit_pkg_artifact/src", 0777);
    mkdir("/tmp/mks_unit_pkg_artifact/mks_modules", 0777);
    mkdir("/tmp/mks_unit_pkg_artifact/mks_modules/packed.lib.mkspkg", 0777);
    mkdir("/tmp/mks_unit_pkg_artifact/mks_modules/packed.lib.mkspkg/src", 0777);
    mkdir("/tmp/mks_unit_pkg_artifact/mks_modules/packed.lib.mkspkg/src/tools", 0777);

    write_test_file("/tmp/mks_unit_pkg_artifact/package.pkg",
                    "package \"consumer.app\";\n"
                    "version \"0.1.0\";\n"
                    "lib \"src/lib.mks\";\n");
    write_test_file("/tmp/mks_unit_pkg_artifact/src/lib.mks",
                    "export fnc value() -> return 1; <-\n");
    write_test_file("/tmp/mks_unit_pkg_artifact/mks_modules/packed.lib.mkspkg/package.pkg",
                    "package \"packed.lib\";\n"
                    "version \"1.0.0\";\n"
                    "lib \"src/lib.mks\";\n");
    write_test_file("/tmp/mks_unit_pkg_artifact/mks_modules/packed.lib.mkspkg/src/lib.mks",
                    "export fnc root() -> return 30; <-\n");
    write_test_file("/tmp/mks_unit_pkg_artifact/mks_modules/packed.lib.mkspkg/src/tools/name.mks",
                    "export fnc label() -> return \"artifact\"; <-\n");

    MKSContext ctx;
    mks_context_init(&ctx, 1024 * 1024);
    ASSERT_EQ_INT(0, mks_run_source(&ctx,
                                    "/tmp/mks_unit_pkg_artifact/src/main.mks",
                                    "using packed.lib as packed;\n"
                                    "using packed.lib.tools.name as name;\n"
                                    "expect(packed.root() ?= 30);\n"
                                    "expect(name.label() ?= \"artifact\");\n",
                                    0,
                                    0));
    mks_context_dispose(&ctx);
}

static void test_mkspkg_imports_without_package_manifest(void) {
    mkdir("/tmp/mks_unit_pkg_script", 0777);
    mkdir("/tmp/mks_unit_pkg_script/mks_modules", 0777);
    mkdir("/tmp/mks_unit_pkg_script/mks_modules/script.lib.mkspkg", 0777);
    mkdir("/tmp/mks_unit_pkg_script/mks_modules/script.lib.mkspkg/src", 0777);

    write_test_file("/tmp/mks_unit_pkg_script/mks_modules/script.lib.mkspkg/package.pkg",
                    "package \"script.lib\";\n"
                    "version \"1.0.0\";\n"
                    "lib \"src/lib.mks\";\n");
    write_test_file("/tmp/mks_unit_pkg_script/mks_modules/script.lib.mkspkg/src/lib.mks",
                    "export fnc value() -> return 44; <-\n");

    MKSContext ctx;
    mks_context_init(&ctx, 1024 * 1024);
    ASSERT_EQ_INT(0, mks_run_source(&ctx,
                                    "/tmp/mks_unit_pkg_script/main.mks",
                                    "using script.lib as lib;\n"
                                    "expect(lib.value() ?= 44);\n",
                                    0,
                                    0));
    mks_context_dispose(&ctx);
}

static void test_mkspkg_packed_file_imports(void) {
    mkdir("/tmp/mks_unit_pkg_file", 0777);
    mkdir("/tmp/mks_unit_pkg_file/mks_modules", 0777);
    write_mkspkg_file("/tmp/mks_unit_pkg_file/mks_modules/packed.file.mkspkg");

    MKSContext ctx;
    mks_context_init(&ctx, 1024 * 1024);
    ASSERT_EQ_INT(0, mks_run_source(&ctx,
                                    "/tmp/mks_unit_pkg_file/main.mks",
                                    "using packed.file as packed;\n"
                                    "using packed.file.sub.value as value;\n"
                                    "expect(packed.root() ?= \"packed-file\");\n"
                                    "expect(value.item() ?= \"packed-file\");\n",
                                    0,
                                    0));
    mks_context_dispose(&ctx);
}

static void test_mkspkg_flat_file_imports(void) {
    mkdir("/tmp/mks_unit_pkg_flat", 0777);
    write_mkspkg_file("/tmp/mks_unit_pkg_flat/packed.file.mkspkg");

    MKSContext ctx;
    mks_context_init(&ctx, 1024 * 1024);
    ASSERT_EQ_INT(0, mks_run_source(&ctx,
                                    "/tmp/mks_unit_pkg_flat/main.mks",
                                    "using packed.file as packed;\n"
                                    "expect(packed.root() ?= \"packed-file\");\n",
                                    0,
                                    0));
    mks_context_dispose(&ctx);
}

static void test_mkspkg_packed_file_transitive_imports(void) {
    mkdir("/tmp/mks_unit_pkg_transitive", 0777);
    mkdir("/tmp/mks_unit_pkg_transitive/mks_modules", 0777);

    FILE *dep = fopen("/tmp/mks_unit_pkg_transitive/mks_modules/dep.lib.mkspkg", "wb");
    ASSERT_TRUE(dep != NULL);
    fwrite("MKSPKG1\n", 1, 8, dep);
    write_mkspkg_entry(dep,
                       "package.pkg",
                       "package \"dep.lib\";\n"
                       "version \"1.0.0\";\n"
                       "lib \"src/lib.mks\";\n");
    write_mkspkg_entry(dep,
                       "src/lib.mks",
                       "export fnc value() -> return \"dep-ok\"; <-\n");
    write_u32_le_test(dep, 0);
    fclose(dep);

    FILE *uses = fopen("/tmp/mks_unit_pkg_transitive/mks_modules/uses.dep.mkspkg", "wb");
    ASSERT_TRUE(uses != NULL);
    fwrite("MKSPKG1\n", 1, 8, uses);
    write_mkspkg_entry(uses,
                       "package.pkg",
                       "package \"uses.dep\";\n"
                       "version \"1.0.0\";\n"
                       "lib \"src/lib.mks\";\n");
    write_mkspkg_entry(uses,
                       "src/lib.mks",
                       "using dep.lib as dep;\n"
                       "export fnc value() -> return dep.value(); <-\n");
    write_u32_le_test(uses, 0);
    fclose(uses);

    MKSContext ctx;
    mks_context_init(&ctx, 1024 * 1024);
    ASSERT_EQ_INT(0, mks_run_source(&ctx,
                                    "/tmp/mks_unit_pkg_transitive/main.mks",
                                    "using uses.dep as lib;\n"
                                    "expect(lib.value() ?= \"dep-ok\");\n",
                                    0,
                                    0));
    mks_context_dispose(&ctx);
}

static void test_mkspkg_flat_file_transitive_imports(void) {
    mkdir("/tmp/mks_unit_pkg_flat_transitive", 0777);

    FILE *dep = fopen("/tmp/mks_unit_pkg_flat_transitive/dep.lib.mkspkg", "wb");
    ASSERT_TRUE(dep != NULL);
    fwrite("MKSPKG1\n", 1, 8, dep);
    write_mkspkg_entry(dep,
                       "package.pkg",
                       "package \"dep.lib\";\n"
                       "version \"1.0.0\";\n"
                       "lib \"src/lib.mks\";\n");
    write_mkspkg_entry(dep,
                       "src/lib.mks",
                       "export fnc value() -> return \"flat-dep-ok\"; <-\n");
    write_u32_le_test(dep, 0);
    fclose(dep);

    FILE *uses = fopen("/tmp/mks_unit_pkg_flat_transitive/uses.dep.mkspkg", "wb");
    ASSERT_TRUE(uses != NULL);
    fwrite("MKSPKG1\n", 1, 8, uses);
    write_mkspkg_entry(uses,
                       "package.pkg",
                       "package \"uses.dep\";\n"
                       "version \"1.0.0\";\n"
                       "lib \"src/lib.mks\";\n");
    write_mkspkg_entry(uses,
                       "src/lib.mks",
                       "using dep.lib as dep;\n"
                       "export fnc value() -> return dep.value(); <-\n");
    write_u32_le_test(uses, 0);
    fclose(uses);

    MKSContext ctx;
    mks_context_init(&ctx, 1024 * 1024);
    ASSERT_EQ_INT(0, mks_run_source(&ctx,
                                    "/tmp/mks_unit_pkg_flat_transitive/main.mks",
                                    "using uses.dep as lib;\n"
                                    "expect(lib.value() ?= \"flat-dep-ok\");\n",
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

static void test_scalar_argument_rebinding_does_not_change_caller(void) {
    MKSContext ctx;
    mks_context_init(&ctx, 1024 * 1024);
    ASSERT_EQ_INT(0, mks_run_source(&ctx,
                                    "<scalar-rebind>",
                                    "fnc set_local(x) ->\n"
                                    "    x =: 99;\n"
                                    "<-\n"
                                    "var x =: 1;\n"
                                    "set_local(x);\n"
                                    "expect(x ?= 1);\n",
                                    0,
                                    0));
    mks_context_dispose(&ctx);
}

static void test_array_argument_mutation_is_visible_to_caller(void) {
    MKSContext ctx;
    mks_context_init(&ctx, 1024 * 1024);
    ASSERT_EQ_INT(0, mks_run_source(&ctx,
                                    "<array-alias>",
                                    "fnc touch(xs) ->\n"
                                    "    xs[0] =: 7;\n"
                                    "<-\n"
                                    "var xs =: [1, 2, 3];\n"
                                    "touch(xs);\n"
                                    "expect(xs[0] ?= 7);\n",
                                    0,
                                    0));
    mks_context_dispose(&ctx);
}

static void test_parameter_rebinding_does_not_replace_caller_array(void) {
    MKSContext ctx;
    mks_context_init(&ctx, 1024 * 1024);
    ASSERT_EQ_INT(0, mks_run_source(&ctx,
                                    "<array-rebind>",
                                    "fnc replace_local(xs) ->\n"
                                    "    xs =: [9, 9];\n"
                                    "<-\n"
                                    "var xs =: [1, 2, 3];\n"
                                    "replace_local(xs);\n"
                                    "expect(xs[0] ?= 1);\n"
                                    "expect(xs.len() ?= 3);\n",
                                    0,
                                    0));
    mks_context_dispose(&ctx);
}

static void test_object_method_arg_mismatch_returns_runner_error(void) {
    MKSContext ctx;
    mks_context_init(&ctx, 1024 * 1024);
    ASSERT_EQ_INT(1, mks_run_source(&ctx,
                                    "<method-arg-mismatch>",
                                    "entity Box() ->\n"
                                    "    method take(x) -> return x; <-\n"
                                    "<-\n"
                                    "var b =: Box();\n"
                                    "b.take();\n",
                                    0,
                                    0));
    mks_context_dispose(&ctx);
}

static void test_null_surface_literal_and_comparisons(void) {
    MKSContext ctx;
    mks_context_init(&ctx, 1024 * 1024);
    ASSERT_EQ_INT(0, mks_run_source(&ctx,
                                    "<null-surface>",
                                    "var x =: null;\n"
                                    "expect(x ?= null);\n"
                                    "expect(x !? 0);\n"
                                    "expect(String(x) ?= \"null\");\n"
                                    "expect(Int(x) ?= 0);\n",
                                    0,
                                    0));
    mks_context_dispose(&ctx);
}

static void test_bool_surface_literals_and_conversions(void) {
    MKSContext ctx;
    mks_context_init(&ctx, 1024 * 1024);
    ASSERT_EQ_INT(0, mks_run_source(&ctx,
                                    "<bool-surface>",
                                    "expect(true ?= true);\n"
                                    "expect(false !? true);\n"
                                    "expect(String(true) ?= \"true\");\n"
                                    "expect(String(false) ?= \"false\");\n"
                                    "expect(Int(true) ?= 1);\n"
                                    "expect(Int(false) ?= 0);\n",
                                    0,
                                    0));
    mks_context_dispose(&ctx);
}

static void test_null_truthiness_in_control_flow(void) {
    MKSContext ctx;
    mks_context_init(&ctx, 1024 * 1024);
    ASSERT_EQ_INT(0, mks_run_source(&ctx,
                                    "<null-truthiness>",
                                    "var x =: 0;\n"
                                    "if (null) -> x =: 1; <- else -> x =: 2; <-\n"
                                    "expect(x ?= 2);\n"
                                    "expect((null || 1) ?= true);\n"
                                    "expect((null && 1) ?= false);\n",
                                    0,
                                    0));
    mks_context_dispose(&ctx);
}

static void test_entity_uses_null_fields_and_returns(void) {
    MKSContext ctx;
    mks_context_init(&ctx, 1024 * 1024);
    ASSERT_EQ_INT(0, mks_run_source(&ctx,
                                    "<entity-null>",
                                    "entity User(name, nick) ->\n"
                                    "    init ->\n"
                                    "        self.name =: name;\n"
                                    "        self.nick =: nick;\n"
                                    "    <-\n"
                                    "    method has_nick() ->\n"
                                    "        if (self.nick ?= null) -> return 0; <-\n"
                                    "        return 1;\n"
                                    "    <-\n"
                                    "    method nick_or_name() ->\n"
                                    "        if (self.nick ?= null) -> return self.name; <-\n"
                                    "        return self.nick;\n"
                                    "    <-\n"
                                    "<-\n"
                                    "var u =: User(\"Ann\", null);\n"
                                    "expect(u.nick ?= null);\n"
                                    "expect(u.has_nick() ?= 0);\n"
                                    "expect(u.nick_or_name() ?= \"Ann\");\n",
                                    0,
                                    0));
    mks_context_dispose(&ctx);
}

static void test_switch_matches_number_string_and_default(void) {
    MKSContext ctx;
    mks_context_init(&ctx, 1024 * 1024);
    ASSERT_EQ_INT(0, mks_run_source(&ctx,
                                    "<switch-basic>",
                                    "var a =: 0;\n"
                                    "switch (2) ->\n"
                                    "    case 1 -> a =: 10; <-\n"
                                    "    case 2 -> a =: 20; <-\n"
                                    "    default -> a =: 30; <-\n"
                                    "<-\n"
                                    "expect(a ?= 20);\n"
                                    "var b =: 0;\n"
                                    "switch (\"x\") ->\n"
                                    "    case \"y\" -> b =: 1; <-\n"
                                    "    default -> b =: 2; <-\n"
                                    "<-\n"
                                    "expect(b ?= 2);\n",
                                    0,
                                    0));
    mks_context_dispose(&ctx);
}

static void test_switch_matches_null_case(void) {
    MKSContext ctx;
    mks_context_init(&ctx, 1024 * 1024);
    ASSERT_EQ_INT(0, mks_run_source(&ctx,
                                    "<switch-null>",
                                    "var x =: 0;\n"
                                    "switch (null) ->\n"
                                    "    case 1 -> x =: 1; <-\n"
                                    "    case null -> x =: 2; <-\n"
                                    "    default -> x =: 3; <-\n"
                                    "<-\n"
                                    "expect(x ?= 2);\n",
                                    0,
                                    0));
    mks_context_dispose(&ctx);
}

static void test_else_if_selects_first_matching_branch(void) {
    MKSContext ctx;
    mks_context_init(&ctx, 1024 * 1024);
    ASSERT_EQ_INT(0, mks_run_source(&ctx,
                                    "<else-if>",
                                    "var out =: 0;\n"
                                    "var x =: 2;\n"
                                    "if (x ?= 1) -> out =: 1; <- else if (x ?= 2) -> out =: 2; <- else if (x ?= 3) -> out =: 3; <- else -> out =: 4; <-\n"
                                    "expect(out ?= 2);\n",
                                    0,
                                    0));
    mks_context_dispose(&ctx);
}

static void test_watch_triggers_on_direct_variable_update(void) {
    MKSContext ctx;
    mks_context_init(&ctx, 1024 * 1024);
    ASSERT_EQ_INT(0, mks_run_source(&ctx,
                                    "<watch-direct-update>",
                                    "var marker =: 0;\n"
                                    "var x =: 0;\n"
                                    "watch x;\n"
                                    "on change x -> marker =: x; <-\n"
                                    "x =: 5;\n"
                                    "expect(marker ?= 5);\n",
                                    0,
                                    0));
    mks_context_dispose(&ctx);
}

static void test_watch_does_not_observe_pointer_write_as_stable_behavior(void) {
    MKSContext ctx;
    mks_context_init(&ctx, 1024 * 1024);
    ASSERT_EQ_INT(0, mks_run_source(&ctx,
                                    "<watch-pointer-boundary>",
                                    "var marker =: 0;\n"
                                    "var x =: 1;\n"
                                    "watch x;\n"
                                    "on change x -> marker =: 1; <-\n"
                                    "var p =: &x;\n"
                                    "*p =: 9;\n"
                                    "expect(marker ?= 0);\n"
                                    "expect(x ?= 9);\n",
                                    0,
                                    0));
    mks_context_dispose(&ctx);
}

static void test_extend_number_method_is_context_global_after_registration(void) {
    MKSContext ctx;
    mks_context_init(&ctx, 1024 * 1024);
    ASSERT_EQ_INT(0, mks_run_source(&ctx,
                                    "<extend-number>",
                                    "extend number ->\n"
                                    "    method twice() -> return self + self; <-\n"
                                    "<-\n"
                                    "expect(4.twice() ?= 8);\n",
                                    0,
                                    0));
    mks_context_dispose(&ctx);
}

static void test_extend_duplicate_method_name_is_rejected(void) {
    MKSContext ctx;
    mks_context_init(&ctx, 1024 * 1024);
    ASSERT_EQ_INT(1, mks_run_source(&ctx,
                                    "<extend-duplicate>",
                                    "extend array ->\n"
                                    "    method pick() -> return 1; <-\n"
                                    "<-\n"
                                    "extend array ->\n"
                                    "    method pick() -> return 2; <-\n"
                                    "<-\n",
                                    0,
                                    0));
    mks_context_dispose(&ctx);
}

static void test_read_rejects_legacy_numeric_mode(void) {
    MKSContext ctx;
    mks_context_init(&ctx, 1024 * 1024);
    ASSERT_EQ_INT(1, mks_run_source(&ctx,
                                    "<read-legacy-mode>",
                                    "Read(0);\n",
                                    0,
                                    0));
    mks_context_dispose(&ctx);
}

static void test_readline_and_readword_builtins_exist(void) {
    MKSContext ctx;
    mks_context_init(&ctx, 1024 * 1024);
    ASSERT_EQ_INT(0, mks_run_source(&ctx,
                                    "<read-builtins>",
                                    "ReadLine;\n"
                                    "ReadWord;\n",
                                    0,
                                    0));
    mks_context_dispose(&ctx);
}

static void test_gc_array_literal_keeps_elements_during_construction(void) {
    MKSContext ctx;
    mks_context_init(&ctx, 1);
    native_string_counter = 0;

    Environment *env = mks_create_global_env(&ctx);
    module_bind_native(make_object(env), "mk_gc_string", native_make_gc_string);

    ASTNode *expr = parse_expression("[mk_gc_string(), mk_gc_string()]");
    RuntimeValue value = eval(expr, env);

    ASSERT_EQ_INT(VAL_ARRAY, value.type);
    ASSERT_TRUE(value.data.managed_array != NULL);
    ASSERT_EQ_INT(2, value.data.managed_array->count);
    ASSERT_EQ_INT(VAL_STRING, value.data.managed_array->elements[0].type);
    ASSERT_EQ_INT(VAL_STRING, value.data.managed_array->elements[1].type);
    ASSERT_TRUE(strcmp(value.data.managed_array->elements[0].data.managed_string->data, "first") == 0);
    ASSERT_TRUE(strcmp(value.data.managed_array->elements[1].data.managed_string->data, "second") == 0);

    delete_ast_node(expr);
    gc_pop_env();
    mks_context_dispose(&ctx);
}

static void test_watch_keeps_closure_alive_across_gc(void) {
    MKSContext ctx;
    mks_context_init(&ctx, 1024 * 1024);
    Environment *env = mks_create_global_env(&ctx);

    ASTNode *setup = parse_program_source(
        "var marker =: 0;\n"
        "var x =: 0;\n"
        "watch x;\n"
        "fnc setup() ->\n"
        "    var captured =: 7;\n"
        "    on change x -> marker =: captured; <-\n"
        "<-\n"
        "setup();\n"
    );
    eval(setup, env);
    gc_collect(env, env);

    ASTNode *trigger = parse_program_source("x =: 1;\n");
    eval(trigger, env);

    RuntimeValue marker = env_get_fast(env, "marker", get_hash("marker"));
    ASSERT_EQ_INT(VAL_INT, marker.type);
    ASSERT_EQ_INT(7, marker.data.int_value);

    delete_ast_node(trigger);
    delete_ast_node(setup);
    gc_pop_env();
    mks_context_dispose(&ctx);
}

static void test_extension_keeps_closure_alive_across_gc(void) {
    MKSContext ctx;
    mks_context_init(&ctx, 1024 * 1024);
    Environment *env = mks_create_global_env(&ctx);

    ASTNode *setup = parse_program_source(
        "fnc install() ->\n"
        "    var captured =: 9;\n"
        "    extend array ->\n"
        "        method cap() -> return captured; <-\n"
        "    <-\n"
        "<-\n"
        "install();\n"
    );
    eval(setup, env);
    gc_collect(env, env);

    ASTNode *expr = parse_expression("[1].cap()");
    RuntimeValue value = eval(expr, env);
    ASSERT_EQ_INT(VAL_INT, value.type);
    ASSERT_EQ_INT(9, value.data.int_value);

    delete_ast_node(expr);
    delete_ast_node(setup);
    gc_pop_env();
    mks_context_dispose(&ctx);
}

static void test_std_json_invalid_input_fails(void) {
    MKSContext ctx;
    mks_context_init(&ctx, 1024 * 1024);
    ASSERT_EQ_INT(1, mks_run_source(&ctx,
                                    "<json-invalid>",
                                    "using std.json as json;\n"
                                    "json.parse(\"{]\");\n",
                                    0,
                                    0));
    mks_context_dispose(&ctx);
}

static void test_std_process_exit_returns_requested_status(void) {
    MKSContext ctx;
    mks_context_init(&ctx, 1024 * 1024);
    char *argv[] = { "mks", "script.mks", NULL };
    mks_context_set_cli_args(&ctx, 2, argv);
    ASSERT_EQ_INT(7, mks_run_source(&ctx,
                                    "<process-exit>",
                                    "using std.process as process;\n"
                                    "process.exit(7);\n",
                                    0,
                                    0));
    mks_context_dispose(&ctx);
}

static void test_std_path_basics(void) {
    MKSContext ctx;
    mks_context_init(&ctx, 1024 * 1024);
    ASSERT_EQ_INT(0, mks_run_source(&ctx,
                                    "<path-basics>",
                                    "using std.path as path;\n"
                                    "expect(path.join(\"a\", \"b\", \"c\") ?= \"a/b/c\");\n"
                                    "expect(path.base(\"dir/sub/file.txt\") ?= \"file.txt\");\n"
                                    "expect(path.dir(\"dir/sub/file.txt\") ?= \"dir/sub\");\n"
                                    "expect(path.ext(\"dir/sub/file.txt\") ?= \".txt\");\n"
                                    "expect(path.name(\"dir/sub/file.txt\") ?= \"file\");\n"
                                    "expect(path.norm(\"dir//sub\\\\file.txt\") ?= \"dir/sub/file.txt\");\n",
                                    0,
                                    0));
    mks_context_dispose(&ctx);
}

static void test_std_tty_safe_text_helpers(void) {
    MKSContext ctx;
    mks_context_init(&ctx, 1024 * 1024);
    ASSERT_EQ_INT(0, mks_run_source(&ctx,
                                    "<tty-safe-text>",
                                    "using std.tty as tty;\n"
                                    "var s =: tty.size_or(80, 24);\n"
                                    "expect(s.w > 0);\n"
                                    "expect(s.h > 0);\n"
                                    "tty.text(1, 1, 10, \"Привет world\", 0, 1);\n"
                                    "tty.text_wrap(1, 2, 12, 3, \"Русский текст and English text\", 1);\n"
                                    "var edit =: tty.edit_text(\"ab\", 1, \"X\");\n"
                                    "expect(edit.value ?= \"aXb\");\n"
                                    "expect(edit.cursor ?= 2);\n"
                                    "edit =: tty.edit_text(edit.value, edit.cursor, \"backspace\");\n"
                                    "expect(edit.value ?= \"ab\");\n"
                                    "expect(edit.cursor ?= 1);\n"
                                    "tty.flush();\n",
                                    0,
                                    0));
    mks_context_dispose(&ctx);
}

static void test_builtin_std_file_module_ignores_cwd_shadow(void) {
    char original_cwd[PATH_MAX];
    ASSERT_TRUE(getcwd(original_cwd, sizeof(original_cwd)) != NULL);

    mkdir("/tmp/mks_unit_std_shadow", 0777);
    mkdir("/tmp/mks_unit_std_shadow/std", 0777);
    write_test_file("/tmp/mks_unit_std_shadow/std/string.mks",
                    "export fnc upper(s) -> return \"shadow\"; <-\n");

    ASSERT_EQ_INT(0, chdir("/tmp/mks_unit_std_shadow"));

    MKSContext ctx;
    mks_context_init(&ctx, 1024 * 1024);
    int status = mks_run_source(&ctx,
                                "/tmp/mks_unit_std_shadow/main.mks",
                                "using std.string as string;\n"
                                "expect(string.upper(\"ok\") ?= \"OK\");\n",
                                0,
                                0);
    mks_context_dispose(&ctx);

    ASSERT_EQ_INT(0, chdir(original_cwd));
    ASSERT_EQ_INT(0, status);
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
    run_test("module exported closure survives gc after import", test_module_exported_closure_survives_gc_after_import);
    run_test("module reimport reuses cached namespace object", test_module_reimport_reuses_cached_namespace_object);
    run_test("package.pkg current and external module imports", test_package_pkg_current_and_module_imports);
    run_test("package.pkg relative current file imports", test_package_pkg_relative_current_file_imports);
    run_test(".mkspkg artifact imports", test_mkspkg_artifact_imports);
    run_test(".mkspkg imports without package manifest", test_mkspkg_imports_without_package_manifest);
    run_test(".mkspkg packed file imports", test_mkspkg_packed_file_imports);
    run_test(".mkspkg flat file imports", test_mkspkg_flat_file_imports);
    run_test(".mkspkg packed file transitive imports", test_mkspkg_packed_file_transitive_imports);
    run_test(".mkspkg flat file transitive imports", test_mkspkg_flat_file_transitive_imports);
    run_test("pointer mutates variable", test_pointer_mutates_variable);
    run_test("pointer allows function to mutate caller variable", test_pointer_allows_function_to_mutate_caller_variable);
    run_test("pointer swap", test_pointer_swap);
    run_test("pointer mutates array element", test_pointer_mutates_array_element);
    run_test("pointer mutates object field", test_pointer_mutates_object_field);
    run_test("pointer rejects temporary address", test_pointer_rejects_temporary_address);
    run_test("pointer rejects non-pointer deref", test_pointer_rejects_non_pointer_deref);
    run_test("scalar argument rebinding does not change caller", test_scalar_argument_rebinding_does_not_change_caller);
    run_test("array argument mutation is visible to caller", test_array_argument_mutation_is_visible_to_caller);
    run_test("parameter rebinding does not replace caller array", test_parameter_rebinding_does_not_replace_caller_array);
    run_test("object method arg mismatch returns runner error", test_object_method_arg_mismatch_returns_runner_error);
    run_test("null surface literal and comparisons", test_null_surface_literal_and_comparisons);
    run_test("bool surface literals and conversions", test_bool_surface_literals_and_conversions);
    run_test("null truthiness in control flow", test_null_truthiness_in_control_flow);
    run_test("entity uses null fields and returns", test_entity_uses_null_fields_and_returns);
    run_test("else if selects first matching branch", test_else_if_selects_first_matching_branch);
    run_test("switch matches number string and default", test_switch_matches_number_string_and_default);
    run_test("switch matches null case", test_switch_matches_null_case);
    run_test("watch triggers on direct variable update", test_watch_triggers_on_direct_variable_update);
    run_test("watch does not observe pointer write as stable behavior", test_watch_does_not_observe_pointer_write_as_stable_behavior);
    run_test("extend number method is context global after registration", test_extend_number_method_is_context_global_after_registration);
    run_test("extend duplicate method name is rejected", test_extend_duplicate_method_name_is_rejected);
    run_test("read rejects legacy numeric mode", test_read_rejects_legacy_numeric_mode);
    run_test("readline and readword builtins exist", test_readline_and_readword_builtins_exist);
    run_test("gc array literal keeps elements during construction", test_gc_array_literal_keeps_elements_during_construction);
    run_test("watch keeps closure alive across gc", test_watch_keeps_closure_alive_across_gc);
    run_test("extension keeps closure alive across gc", test_extension_keeps_closure_alive_across_gc);
    run_test("std json invalid input fails", test_std_json_invalid_input_fails);
    run_test("std path basics", test_std_path_basics);
    run_test("std tty safe text helpers", test_std_tty_safe_text_helpers);
    run_test("builtin std file module ignores cwd shadow", test_builtin_std_file_module_ignores_cwd_shadow);
    run_test("std process exit returns requested status", test_std_process_exit_returns_requested_status);

    if (tests_failed != 0) {
        fprintf(stderr, "%d unit test(s) failed\n", tests_failed);
        return 1;
    }

    printf("[UNIT] all tests passed\n");
    return 0;
}
