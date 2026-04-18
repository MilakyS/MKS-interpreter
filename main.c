#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <errno.h>
#include <ctype.h>

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
#include "std/watch.h"
#include "Runtime/profiler.h"
#include "Runtime/functions.h"

bool debug_mode = false;

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} StringBuffer;

static const char *main_value_type_name(RuntimeValue value) {
    if (value.type == VAL_RETURN) {
        value.type = value.original_type;
    }

    switch (value.type) {
        case VAL_INT: return "number";
        case VAL_STRING: return "string";
        case VAL_ARRAY: return "array";
        case VAL_FUNC: return "function";
        case VAL_NATIVE_FUNC: return "native function";
        case VAL_RETURN: return "return";
        case VAL_BREAK: return "break";
        case VAL_CONTINUE: return "continue";
        case VAL_OBJECT: return "object";
        case VAL_BLUEPRINT: return "blueprint";
        case VAL_NULL: return "null";
    }
    return "unknown";
}

static void sb_reserve(StringBuffer *sb, size_t extra) {
    if (sb->len + extra + 1 <= sb->cap) {
        return;
    }

    size_t new_cap = sb->cap == 0 ? 64 : sb->cap;
    while (new_cap < sb->len + extra + 1) {
        new_cap *= 2;
    }

    char *new_data = (char *)realloc(sb->data, new_cap);
    if (new_data == NULL) {
        runtime_error("Out of memory while converting value to string");
    }

    sb->data = new_data;
    sb->cap = new_cap;
}

static void sb_append_len(StringBuffer *sb, const char *text, size_t len) {
    if (text == NULL) {
        text = "";
        len = 0;
    }

    sb_reserve(sb, len);
    memcpy(sb->data + sb->len, text, len);
    sb->len += len;
    sb->data[sb->len] = '\0';
}

static void sb_append(StringBuffer *sb, const char *text) {
    sb_append_len(sb, text, text != NULL ? strlen(text) : 0);
}

static void append_value_string(StringBuffer *sb, RuntimeValue value) {
    if (value.type == VAL_RETURN) {
        value.type = value.original_type;
    }

    char num_buf[64];
    switch (value.type) {
        case VAL_INT:
            snprintf(num_buf, sizeof(num_buf), "%g", value.data.float_value);
            sb_append(sb, num_buf);
            break;

        case VAL_STRING:
            if (value.data.managed_string != NULL &&
                value.data.managed_string->data != NULL) {
                sb_append_len(sb,
                              value.data.managed_string->data,
                              value.data.managed_string->len);
            }
            break;

        case VAL_ARRAY:
            sb_append(sb, "[");
            if (value.data.managed_array != NULL) {
                for (int i = 0; i < value.data.managed_array->count; i++) {
                    if (i > 0) {
                        sb_append(sb, ", ");
                    }
                    append_value_string(sb, value.data.managed_array->elements[i]);
                }
            }
            sb_append(sb, "]");
            break;

        case VAL_OBJECT:
            sb_append(sb, "<Object>");
            break;

        case VAL_FUNC:
            sb_append(sb, "<Function>");
            break;

        case VAL_NATIVE_FUNC:
            sb_append(sb, "<Native Function>");
            break;

        case VAL_BLUEPRINT:
            sb_append(sb, "<Blueprint>");
            break;

        case VAL_BREAK:
            sb_append(sb, "<Break>");
            break;

        case VAL_CONTINUE:
            sb_append(sb, "<Continue>");
            break;

        case VAL_NULL:
            sb_append(sb, "null");
            break;

        case VAL_RETURN:
            break;
    }
}

static RuntimeValue native_String(const RuntimeValue *args, const int arg_count) {
    if (arg_count != 1) {
        runtime_error("String expects 1 argument, got %d", arg_count);
    }

    StringBuffer sb = {0};
    append_value_string(&sb, args[0]);
    if (sb.data == NULL) {
        return make_string("");
    }

    return make_string_owned(sb.data, sb.len);
}

static RuntimeValue native_Int(const RuntimeValue *args, const int arg_count) {
    if (arg_count != 1) {
        runtime_error("Int expects 1 argument, got %d", arg_count);
    }

    RuntimeValue value = args[0];
    if (value.type == VAL_RETURN) {
        value.type = value.original_type;
    }

    if (value.type == VAL_INT) {
        return value;
    }

    if (value.type == VAL_NULL) {
        return make_int(0);
    }

    if (value.type != VAL_STRING) {
        runtime_error("Int cannot convert %s; expected number, string, or null",
                      main_value_type_name(value));
    }

    const char *text = value.data.managed_string != NULL
        ? value.data.managed_string->data
        : "";
    char *end = NULL;
    errno = 0;
    const double parsed = strtod(text, &end);

    if (text == end) {
        runtime_error("Int cannot parse '%s' as a number", text);
    }

    while (end != NULL && *end != '\0' && isspace((unsigned char)*end)) {
        end++;
    }

    if (errno == ERANGE || end == NULL || *end != '\0') {
        runtime_error("Int cannot parse '%s' as a number", text);
    }

    return make_int(parsed);
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
static void env_register_native(Environment *env, const char *name, NativeFn fn) {
    RuntimeValue val;
    val.type = VAL_NATIVE_FUNC;
    val.data.native.fn = fn;
    val.data.native.ctx = NULL;
    val.original_type = VAL_NATIVE_FUNC;

    env_set_fast(env, name, get_hash(name), val);
}

static void call_entry_main(Environment *env) {
    RuntimeValue main_val;
    if (!env_try_get(env, "main", get_hash("main"), &main_val)) {
        /* Нет точки входа — значит, скрипт запускается как раньше: только верхний уровень. */
        return;
    }

    if (main_val.type == VAL_FUNC) {
        const ASTNode *decl = main_val.data.func.node;
        if (decl->data.func_decl.param_count != 0) {
            runtime_error("main must take 0 arguments");
        }
        Environment *local_env = env_create_child(main_val.data.func.closure_env);
        gc_push_env(local_env);
        eval(decl->data.func_decl.body, local_env);
        gc_pop_env();
        return;
    }

    if (main_val.type == VAL_NATIVE_FUNC) {
        main_val.data.native.fn(NULL, 0);
        return;
    }

    runtime_error("main must be function or native function");
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
        module_system_init(env);
        env_register_native(env, "Read", native_Read);
        env_register_native(env, "Object", native_Object);
        env_register_native(env, "Int", native_Int);
        env_register_native(env, "String", native_String);
        env_register_native(env, "expect", native_Expect);
        runtime_set_file("<repl>");
        printf(":mks> type code, Ctrl+D to exit\n");
        while (fgets(line, sizeof(line), stdin)) {
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
    runtime_set_source(source);

    struct Lexer lexer;
    Token_init(&lexer, source);

    Parser parser;
    parser_init(&parser, &lexer);

    Environment *env = (Environment *)gc_alloc(sizeof(Environment), GC_OBJ_ENV);
    env_init(env);
    gc_push_env(env);
    module_system_init(env);

    env_register_native(env, "Read", native_Read);
    env_register_native(env, "Object", native_Object);
    env_register_native(env, "Int", native_Int);
    env_register_native(env, "String", native_String);
    env_register_native(env, "expect", native_Expect);

    ASTNode *program = parser_parse_program(&parser);
    if (profile_flag) profiler_enable();
    if (program != NULL) {
        eval(program, env);
        call_entry_main(env);
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
