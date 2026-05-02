#include "io.h"

#include <stdio.h>
#include <string.h>

#include "../Runtime/errors.h"
#include "../Runtime/context.h"
#include "../Runtime/module.h"

static const char *io_string_arg(RuntimeValue value, const char *where) {
    if (value.type == VAL_RETURN) {
        value.type = value.original_type;
    }

    if (value.type != VAL_STRING ||
        value.data.managed_string == NULL ||
        value.data.managed_string->data == NULL) {
        runtime_error("%s expects a string", where);
    }

    return value.data.managed_string->data;
}

static FILE *io_stream_from_value(RuntimeValue value, const char *where) {
    const char *name = io_string_arg(value, where);

    if (strcmp(name, "stdout") == 0) {
        return stdout;
    }
    if (strcmp(name, "stderr") == 0) {
        return stderr;
    }
    if (strcmp(name, "stdin") == 0) {
        runtime_error("%s cannot write to stdin", where);
    }

    runtime_error("%s got unknown stream '%s'", where, name);
    return stdout;
}

static void io_fprint_value(FILE *stream, RuntimeValue value) {
    if (value.type == VAL_RETURN) {
        value.type = value.original_type;
    }

    switch (value.type) {
        case VAL_INT:
            fprintf(stream, "%lld", (long long)value.data.int_value);
            break;
        case VAL_FLOAT:
            fprintf(stream, "%g", value.data.float_value);
            break;
        case VAL_BOOL:
            fputs(value.data.bool_value ? "true" : "false", stream);
            break;
        case VAL_STRING:
            if (value.data.managed_string != NULL &&
                value.data.managed_string->data != NULL) {
                fputs(value.data.managed_string->data, stream);
            }
            break;
        case VAL_ARRAY:
            fputc('[', stream);
            if (value.data.managed_array != NULL) {
                for (int i = 0; i < value.data.managed_array->count; i++) {
                    if (i > 0) {
                        fputs(", ", stream);
                    }
                    io_fprint_value(stream, value.data.managed_array->elements[i]);
                }
            }
            fputc(']', stream);
            break;
        case VAL_POINTER:
            fputs("<Pointer>", stream);
            break;
        case VAL_OBJECT:
            fputs("<Object>", stream);
            break;
        case VAL_MODULE:
            fputs("<Module>", stream);
            break;
        case VAL_FUNC:
            fputs("<Function>", stream);
            break;
        case VAL_NATIVE_FUNC:
            fputs("<Native Function>", stream);
            break;
        case VAL_BLUEPRINT:
            fputs("<Blueprint>", stream);
            break;
        case VAL_BREAK:
            fputs("<Break>", stream);
            break;
        case VAL_CONTINUE:
            fputs("<Continue>", stream);
            break;
        case VAL_NULL:
            fputs("null", stream);
            break;
        case VAL_STRING_BUILDER:
            fputs("<StringBuilder>", stream);
            break;
        case VAL_RETURN:
            break;
    }
}

static void io_write_values(FILE *stream, const RuntimeValue *args, int arg_count, int newline) {
    for (int i = 1; i < arg_count; i++) {
        io_fprint_value(stream, args[i]);
        if (i < arg_count - 1) {
            fputc(' ', stream);
        }
    }

    if (newline) {
        fputc('\n', stream);
    }
}

static RuntimeValue n_write(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    (void)ctx;

    if (arg_count < 1) {
        runtime_error("io.write expects a stream and optional values");
    }

    FILE *stream = io_stream_from_value(args[0], "io.write");
    io_write_values(stream, args, arg_count, 0);
    return make_null();
}

static RuntimeValue n_writeln(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    (void)ctx;

    if (arg_count < 1) {
        runtime_error("io.writeln expects a stream and optional values");
    }

    FILE *stream = io_stream_from_value(args[0], "io.writeln");
    io_write_values(stream, args, arg_count, 1);
    return make_null();
}

static RuntimeValue n_flush(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    (void)ctx;

    if (arg_count != 1) {
        runtime_error("io.flush expects 1 argument: stream");
    }

    FILE *stream = io_stream_from_value(args[0], "io.flush");
    fflush(stream);
    return make_null();
}

static void io_write_prompt(const RuntimeValue *args, int arg_count, const char *where) {
    if (arg_count == 0) {
        return;
    }

    const char *prompt = io_string_arg(args[0], where);
    fputs(prompt, stdout);
    fflush(stdout);
}

static RuntimeValue io_read_line_value(void) {
    char buffer[8192];
    buffer[0] = '\0';

    if (fgets(buffer, sizeof(buffer), stdin) == NULL) {
        return make_string("");
    }

    size_t len = strlen(buffer);
    while (len > 0 && (buffer[len - 1] == '\n' || buffer[len - 1] == '\r')) {
        buffer[len - 1] = '\0';
        len--;
    }

    return make_string(buffer);
}

static RuntimeValue n_read_line(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    (void)ctx;

    if (arg_count > 1) {
        runtime_error("io.read_line expects at most 1 argument");
    }

    io_write_prompt(args, arg_count, "io.read_line");
    return io_read_line_value();
}

static RuntimeValue n_read_word(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    (void)ctx;

    if (arg_count > 1) {
        runtime_error("io.read_word expects at most 1 argument");
    }

    io_write_prompt(args, arg_count, "io.read_word");

    RuntimeValue line = io_read_line_value();
    if (line.type != VAL_STRING ||
        line.data.managed_string == NULL ||
        line.data.managed_string->data == NULL) {
        return make_string("");
    }

    char word[256] = {0};
    sscanf(line.data.managed_string->data, "%255s", word);
    return make_string(word);
}

static void bind(RuntimeValue exports, const char *name, NativeFn fn) {
    module_bind_native(exports, name, fn);
}

void std_init_io(RuntimeValue exports, Environment *module_env) {
    (void)module_env;

    bind(exports, "write", n_write);
    bind(exports, "writeln", n_writeln);
    bind(exports, "flush", n_flush);
    bind(exports, "read_line", n_read_line);
    bind(exports, "read_word", n_read_word);
    bind(exports, "fputs", n_write);
    bind(exports, "puts", n_writeln);
    bind(exports, "fflush", n_flush);
    bind(exports, "fgets", n_read_line);

    env_set(exports.data.obj_env, "stdin", make_string("stdin"));
    env_set(exports.data.obj_env, "stdout", make_string("stdout"));
    env_set(exports.data.obj_env, "stderr", make_string("stderr"));
}
