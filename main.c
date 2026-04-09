#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>
#include <time.h>

#include "Lexer/lexer.h"
#include "Parser/parser.h"
#include "Eval/eval.h"
#include "GC/gc.h"
#include "env/env.h"
#include "Utils/hash.h"
#include "Runtime/output.h"
#include "Runtime/module.h"
#include "Runtime/errors.h"
#include "Utils/file.h"
#include "Runtime/extension.h"
#include "Runtime/watch.h"
#include "Runtime/profiler.h"

bool debug_mode = false;

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

#define UNUSED(x) (void)(x)

static RuntimeValue native_Rand(const RuntimeValue *args, const int arg_count) {
    UNUSED(args);
    UNUSED(arg_count);
    double r = (double)rand() / (double)RAND_MAX;
    return make_int(r);
}

static RuntimeValue native_RandInt(const RuntimeValue *args, const int arg_count) {
    if (arg_count != 2) runtime_error("randint expects 2 args (min, max)");
    int min = (int)args[0].data.float_value;
    int max = (int)args[1].data.float_value;
    if (max < min) runtime_error("randint: max < min");
    int v = min + rand() % (max - min + 1);
    return make_int(v);
}

static RuntimeValue native_Math1(double (*fn)(double), const RuntimeValue *args, int arg_count, const char *name) {
    if (arg_count != 1) runtime_error("%s expects 1 arg", name);
    return make_int(fn(args[0].data.float_value));
}

static RuntimeValue native_Sqrt(const RuntimeValue *args, int arg_count) { return native_Math1(sqrt, args, arg_count, "sqrt"); }
static RuntimeValue native_Sin (const RuntimeValue *args, int arg_count) { return native_Math1(sin,  args, arg_count, "sin"); }
static RuntimeValue native_Cos (const RuntimeValue *args, int arg_count) { return native_Math1(cos,  args, arg_count, "cos"); }
static RuntimeValue native_Floor(const RuntimeValue *args, int arg_count) { return native_Math1(floor, args, arg_count, "floor"); }
static RuntimeValue native_Ceil (const RuntimeValue *args, int arg_count) { return native_Math1(ceil,  args, arg_count, "ceil"); }
static RuntimeValue native_Round(const RuntimeValue *args, int arg_count) {
    if (arg_count != 1) runtime_error("round expects 1 arg");
    return make_int(llround(args[0].data.float_value));
}

static RuntimeValue native_Pow(const RuntimeValue *args, int arg_count) {
    if (arg_count != 2) runtime_error("pow expects 2 args");
    return make_int(pow(args[0].data.float_value, args[1].data.float_value));
}

static RuntimeValue native_Sleep(const RuntimeValue *args, int arg_count) {
    if (arg_count != 1) runtime_error("sleep expects 1 arg (ms)");
    int ms = (int)args[0].data.float_value;
    if (ms < 0) ms = 0;
    struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
    return make_null();
}

static RuntimeValue native_Now(const RuntimeValue *args, int arg_count) {
    UNUSED(args); UNUSED(arg_count);
    time_t t = time(NULL);
    char buf[64];
    struct tm tm;
    localtime_r(&t, &tm);
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
    return make_string(buf);
}

static RuntimeValue native_Timestamp(const RuntimeValue *args, int arg_count) {
    UNUSED(args); UNUSED(arg_count);
    return make_int((double)time(NULL));
}
static RuntimeValue native_Expect(const RuntimeValue *args, const int arg_count) {
    if (arg_count < 1) {
        runtime_error("Expect needs at least 1 argument");
    }
    RuntimeValue c = args[0];
    int truthy = (c.type == VAL_INT) ? (c.data.float_value != 0) : 0;
    if (!truthy) {
        runtime_error("Expectation failed");
    }
    return make_int(1);
}

static RuntimeValue native_Object(const RuntimeValue *args, const int arg_count) {
    (void)args;
    (void)arg_count;

    Environment *obj_env = (Environment *)gc_alloc(sizeof(Environment), GC_OBJ_ENV);
    env_init(obj_env);
    obj_env->parent = NULL;

    return make_object(obj_env);
}

static RuntimeValue native_DefMethod(const RuntimeValue *args, const int arg_count) {
    if (arg_count != 3) {
        runtime_error("DefMethod expects (object, name, func)");
    }

    RuntimeValue obj = args[0];
    RuntimeValue name = args[1];
    RuntimeValue fn = args[2];

    if (obj.type != VAL_OBJECT || name.type != VAL_STRING || fn.type != VAL_FUNC) {
        runtime_error("DefMethod types: object, string, function");
    }

    const char *s = name.data.managed_string->data;
    unsigned int h = get_hash(s);
    env_set_fast(obj.data.obj_env, s, h, fn);
    return fn;
}

static void env_register_native(Environment *env, const char *name, NativeFn fn) {
    RuntimeValue val;
    val.type = VAL_NATIVE_FUNC;
    val.data.native.fn = fn;
    val.data.native.ctx = NULL;
    val.original_type = VAL_NATIVE_FUNC;

    env_set_fast(env, name, get_hash(name), val);
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

    gc_init(1024 * 1024);
    srand((unsigned int)time(NULL));
    const char *gc_debug = getenv("MKS_GC_DEBUG");
    if (gc_debug != NULL && strcmp(gc_debug, "1") == 0) {
        gc_set_debug(1);
        fprintf(stderr, "[MKS] GC debug enabled\n");
    }

    module_init();

    int argi = 1;
    int profile_flag = 0;

    if (strcmp(argv[argi], "--profile") == 0) {
        profile_flag = 1;
        argi++;
        if (argc <= argi) {
            fprintf(stderr, "Usage: %s --profile <file.mks>\n", argv[0]);
            return 1;
        }
    }

    if (strcmp(argv[argi], "--version") == 0) {
        printf("mks %s\n", "0.1.0");
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
        return 0;
    }

    if (strcmp(argv[argi], "--repl") == 0) {
        char line[2048];
        Environment *env = (Environment *)gc_alloc(sizeof(Environment), GC_OBJ_ENV);
        env_init(env);
        gc_push_env(env);
    env_register_native(env, "Read", native_Read);
    runtime_set_file("<repl>");
        printf(":mks> type code, Ctrl+D to exit\n");
        while (fgets(line, sizeof(line), stdin)) {
            struct Lexer lexer;
            Token_init(&lexer, line);
            Parser parser;
            parser_init(&parser, &lexer);
            ASTNode *program = parser_parse_program(&parser);
            if (program != NULL) {
                eval(program, env);
            }
            delete_ast_node(program);
        }
        gc_collect(env, env);
        gc_pop_env();
        return 0;
    }

    char *source = mks_read_file(argv[argi]);
    if (!source) {
        return 1;
    }

    runtime_set_file(argv[argi]);

    struct Lexer lexer;
    Token_init(&lexer, source);

    Parser parser;
    parser_init(&parser, &lexer);

    Environment *env = (Environment *)gc_alloc(sizeof(Environment), GC_OBJ_ENV);
    env_init(env);
    gc_push_env(env);

    env_register_native(env, "Read", native_Read);
    env_register_native(env, "Object", native_Object);
    env_register_native(env, "DefMethod", native_DefMethod);
    env_register_native(env, "expect", native_Expect);
    env_register_native(env, "rand", native_Rand);
    env_register_native(env, "randint", native_RandInt);
    env_register_native(env, "sqrt", native_Sqrt);
    env_register_native(env, "pow", native_Pow);
    env_register_native(env, "sin", native_Sin);
    env_register_native(env, "cos", native_Cos);
    env_register_native(env, "floor", native_Floor);
    env_register_native(env, "ceil", native_Ceil);
    env_register_native(env, "round", native_Round);
    env_register_native(env, "sleep", native_Sleep);
    env_register_native(env, "now", native_Now);
    env_register_native(env, "timestamp", native_Timestamp);
        env_register_native(env, "rand", native_Rand);
        env_register_native(env, "randint", native_RandInt);
        env_register_native(env, "sqrt", native_Sqrt);
        env_register_native(env, "pow", native_Pow);
        env_register_native(env, "sin", native_Sin);
        env_register_native(env, "cos", native_Cos);
        env_register_native(env, "floor", native_Floor);
        env_register_native(env, "ceil", native_Ceil);
        env_register_native(env, "round", native_Round);
        env_register_native(env, "sleep", native_Sleep);
        env_register_native(env, "now", native_Now);
        env_register_native(env, "timestamp", native_Timestamp);

    ASTNode *program = parser_parse_program(&parser);
    if (profile_flag) profiler_enable();
    if (program != NULL) {
        eval(program, env);
    }
    if (profile_flag) profiler_report();

    gc_collect(env, env);

    gc_pop_env();
    delete_ast_node(program);
    free(source);

    module_free_all();
    extension_free_all();
    watch_clear_all();

    return 0;
}
