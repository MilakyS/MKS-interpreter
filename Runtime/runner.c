#define _POSIX_C_SOURCE 200809L

#include "runner.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../Eval/eval.h"
#include "../GC/gc.h"
#include "../Lexer/lexer.h"
#include "../Parser/parser.h"
#include "../Utils/file.h"
#include "../Utils/hash.h"
#include "../env/env.h"
#include "errors.h"
#include "functions.h"
#include "module.h"
#include "profiler.h"

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} StringBuffer;

static const char *value_type_name(RuntimeValue value) {
    if (value.type == VAL_RETURN) {
        value.type = value.original_type;
    }

    switch (value.type) {
        case VAL_INT:
        case VAL_FLOAT: return "number";
        case VAL_STRING: return "string";
        case VAL_ARRAY: return "array";
        case VAL_POINTER: return "pointer";
        case VAL_FUNC: return "function";
        case VAL_NATIVE_FUNC: return "native function";
        case VAL_RETURN: return "return";
        case VAL_BREAK: return "break";
        case VAL_CONTINUE: return "continue";
        case VAL_OBJECT: return "object";
        case VAL_MODULE: return "module";
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
            snprintf(num_buf, sizeof(num_buf), "%lld", (long long)value.data.int_value);
            sb_append(sb, num_buf);
            break;

        case VAL_FLOAT:
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

        case VAL_POINTER: sb_append(sb, "<Pointer>"); break;

        case VAL_OBJECT: sb_append(sb, "<Object>"); break;
        case VAL_MODULE: sb_append(sb, "<Module>"); break;
        case VAL_FUNC: sb_append(sb, "<Function>"); break;
        case VAL_NATIVE_FUNC: sb_append(sb, "<Native Function>"); break;
        case VAL_BLUEPRINT: sb_append(sb, "<Blueprint>"); break;
        case VAL_BREAK: sb_append(sb, "<Break>"); break;
        case VAL_CONTINUE: sb_append(sb, "<Continue>"); break;
        case VAL_NULL: sb_append(sb, "null"); break;
        case VAL_RETURN: break;
    }
}

static RuntimeValue native_String(MKSContext *ctx, const RuntimeValue *args, const int arg_count) {
    (void)ctx;
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

static RuntimeValue native_Int(MKSContext *ctx, const RuntimeValue *args, const int arg_count) {
    (void)ctx;
    if (arg_count != 1) {
        runtime_error("Int expects 1 argument, got %d", arg_count);
    }

    RuntimeValue value = args[0];
    if (value.type == VAL_RETURN) {
        value.type = value.original_type;
    }

    if (runtime_value_is_number(value)) {
        return value;
    }

    if (value.type == VAL_NULL) {
        return make_int(0);
    }

    if (value.type != VAL_STRING) {
        runtime_error("Int cannot convert %s; expected number, string, or null",
                      value_type_name(value));
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

    return make_number_from_double(parsed);
}

static RuntimeValue native_Read(MKSContext *ctx, const RuntimeValue *args, const int arg_count) {
    (void)ctx;
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
        runtime_value_is_number(args[0]) &&
        runtime_value_as_double(args[0]) == 0) {
        char word[256] = {0};
        sscanf(buffer, "%255s", word);
        return make_string(word);
    }

    return make_string(buffer);
}

static RuntimeValue native_Expect(MKSContext *ctx, const RuntimeValue *args, const int arg_count) {
    (void)ctx;
    if (arg_count < 1) {
        runtime_error("Expect needs at least 1 argument");
    }
    RuntimeValue c = args[0];
    int truthy = runtime_value_is_number(c) ? (runtime_value_as_double(c) != 0) : 0;
    if (!truthy) {
        runtime_error("Expectation failed");
    }
    return make_int(1);
}

static RuntimeValue native_Object(MKSContext *ctx, const RuntimeValue *args, const int arg_count) {
    (void)ctx;
    (void)args;
    (void)arg_count;

    Environment *obj_env = (Environment *)gc_alloc(sizeof(Environment), GC_OBJ_ENV);
    env_init(obj_env);
    obj_env->parent = NULL;

    return make_object(obj_env);
}

void mks_register_builtins(MKSContext *ctx, Environment *env) {
    MKSContext *previous = mks_context_current();
    mks_context_set_current(ctx);

    module_bind_native(make_object(env), "Read", native_Read);
    module_bind_native(make_object(env), "Object", native_Object);
    module_bind_native(make_object(env), "Int", native_Int);
    module_bind_native(make_object(env), "String", native_String);
    module_bind_native(make_object(env), "expect", native_Expect);

    mks_context_set_current(previous);
}

Environment *mks_create_global_env(MKSContext *ctx) {
    MKSContext *previous = mks_context_current();
    mks_context_set_current(ctx);

    Environment *env = (Environment *)gc_alloc(sizeof(Environment), GC_OBJ_ENV);
    env_init(env);
    gc_push_env(env);
    module_system_init(env);
    mks_register_builtins(ctx, env);

    mks_context_set_current(previous);
    return env;
}

static void call_entry_main(Environment *env) {
    RuntimeValue main_val;
    if (!env_try_get(env, "main", get_hash("main"), &main_val)) {
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
        main_val.data.native.fn(mks_context_current(), NULL, 0);
        return;
    }

    return;
}

int mks_run_source(MKSContext *ctx, const char *name, const char *source, const int call_main, const int profile) {
    MKSContext *previous = mks_context_current();
    mks_context_set_current(ctx);

    const int was_active = ctx->error_active;
    if (setjmp(ctx->error_jmp) != 0) {
        ctx->error_active = was_active;
        mks_context_set_current(previous);
        return ctx->error_status != 0 ? ctx->error_status : 1;
    }
    ctx->error_active = 1;
    ctx->error_status = 0;

    runtime_set_file(name);
    runtime_set_source(source);

    struct Lexer lexer;
    Token_init(&lexer, source);

    Parser parser;
    parser_init(&parser, &lexer);

    Environment *env = mks_create_global_env(ctx);
    ASTNode *program = parser_parse_program(&parser);

    if (profile) {
        profiler_enable();
    }
    if (program != NULL) {
        eval(program, env);
        if (call_main) {
            call_entry_main(env);
        }
    }
    if (profile) {
        profiler_report();
    }

    gc_collect(env, env);
    gc_pop_env();
    delete_ast_node(program);

    ctx->error_active = was_active;
    mks_context_set_current(previous);
    return 0;
}

int mks_run_file(MKSContext *ctx, const char *path, const int call_main, const int profile) {
    char *source = mks_read_file(path);
    if (source == NULL) {
        return 1;
    }

    int status = mks_run_source(ctx, path, source, call_main, profile);
    free(source);
    return status;
}
