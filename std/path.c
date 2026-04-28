#define _POSIX_C_SOURCE 200809L

#include "path.h"

#include "../Runtime/context.h"
#include "../Runtime/errors.h"
#include "../Runtime/module.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static int is_sep(char c) {
    return c == '/' || c == '\\';
}

static const char *expect_string_arg(const RuntimeValue *args, int index, const char *fn_name) {
    if (args[index].type != VAL_STRING ||
        args[index].data.managed_string == NULL ||
        args[index].data.managed_string->data == NULL) {
        runtime_error("%s expects string arguments", fn_name);
    }
    return args[index].data.managed_string->data;
}

static char *dup_slice(const char *src, size_t start, size_t len) {
    char *out = (char *)malloc(len + 1);
    if (out == NULL) {
        runtime_error("path: out of memory");
    }
    memcpy(out, src + start, len);
    out[len] = '\0';
    return out;
}

static size_t trim_trailing_seps(const char *path, size_t len) {
    while (len > 1 && is_sep(path[len - 1])) {
        len--;
    }
    return len;
}

static RuntimeValue n_basename(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    (void)ctx;
    if (arg_count != 1) {
        runtime_error("path.basename expects 1 argument");
    }

    const char *path = expect_string_arg(args, 0, "path.basename");
    size_t len = strlen(path);
    if (len == 0) {
        return make_string("");
    }

    len = trim_trailing_seps(path, len);
    size_t start = len;
    while (start > 0 && !is_sep(path[start - 1])) {
        start--;
    }

    return make_string_owned(dup_slice(path, start, len - start), len - start);
}

static RuntimeValue n_dirname(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    (void)ctx;
    if (arg_count != 1) {
        runtime_error("path.dirname expects 1 argument");
    }

    const char *path = expect_string_arg(args, 0, "path.dirname");
    size_t len = strlen(path);
    if (len == 0) {
        return make_string(".");
    }

    len = trim_trailing_seps(path, len);
    while (len > 0 && !is_sep(path[len - 1])) {
        len--;
    }

    while (len > 1 && is_sep(path[len - 1])) {
        len--;
    }

    if (len == 0) {
        return make_string(".");
    }

    return make_string_owned(dup_slice(path, 0, len), len);
}

static RuntimeValue n_extname(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    (void)ctx;
    if (arg_count != 1) {
        runtime_error("path.extname expects 1 argument");
    }

    const char *path = expect_string_arg(args, 0, "path.extname");
    size_t len = strlen(path);
    if (len == 0) {
        return make_string("");
    }

    len = trim_trailing_seps(path, len);
    size_t start = len;
    while (start > 0 && !is_sep(path[start - 1])) {
        start--;
    }

    size_t dot = len;
    while (dot > start) {
        if (path[dot - 1] == '.') {
            dot--;
            break;
        }
        dot--;
    }

    if (dot == len || dot == start) {
        return make_string("");
    }

    return make_string_owned(dup_slice(path, dot, len - dot), len - dot);
}

static RuntimeValue n_stem(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    (void)ctx;
    if (arg_count != 1) {
        runtime_error("path.stem expects 1 argument");
    }

    const char *path = expect_string_arg(args, 0, "path.stem");
    size_t len = strlen(path);
    if (len == 0) {
        return make_string("");
    }

    len = trim_trailing_seps(path, len);
    size_t start = len;
    while (start > 0 && !is_sep(path[start - 1])) {
        start--;
    }

    size_t end = len;
    size_t dot = len;
    while (dot > start) {
        if (path[dot - 1] == '.') {
            dot--;
            break;
        }
        dot--;
    }

    if (dot != len && dot != start) {
        end = dot;
    }

    return make_string_owned(dup_slice(path, start, end - start), end - start);
}

static RuntimeValue n_join(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    (void)ctx;
    if (arg_count < 1) {
        runtime_error("path.join expects at least 1 argument");
    }

    size_t total = 0;
    for (int i = 0; i < arg_count; i++) {
        const char *part = expect_string_arg(args, i, "path.join");
        total += strlen(part) + 1;
    }

    char *buf = (char *)malloc(total + 1);
    if (buf == NULL) {
        runtime_error("path.join: out of memory");
    }

    size_t out = 0;
    buf[0] = '\0';
    for (int i = 0; i < arg_count; i++) {
        const char *part = args[i].data.managed_string->data;
        size_t len = args[i].data.managed_string->len;
        if (len == 0) {
            continue;
        }

        size_t start = 0;
        while (start < len && is_sep(part[start])) {
            start++;
        }

        size_t end = len;
        while (end > start && is_sep(part[end - 1])) {
            end--;
        }

        if (out == 0) {
            if (start == len) {
                buf[out++] = '/';
                continue;
            }
            memcpy(buf + out, part, end);
            out += end;
            continue;
        }

        if (out > 0 && !is_sep(buf[out - 1])) {
            buf[out++] = '/';
        }

        if (start < end) {
            memcpy(buf + out, part + start, end - start);
            out += (end - start);
        }
    }

    if (out == 0) {
        buf[out++] = '.';
    }
    buf[out] = '\0';
    return make_string_owned(buf, out);
}

static RuntimeValue n_normalize(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    (void)ctx;
    if (arg_count != 1) {
        runtime_error("path.normalize expects 1 argument");
    }

    const char *path = expect_string_arg(args, 0, "path.normalize");
    size_t len = strlen(path);
    if (len == 0) {
        return make_string(".");
    }

    char *buf = (char *)malloc(len + 2);
    if (buf == NULL) {
        runtime_error("path.normalize: out of memory");
    }

    size_t out = 0;
    int last_was_sep = 0;
    for (size_t i = 0; i < len; i++) {
        char c = path[i];
        if (is_sep(c)) {
            if (!last_was_sep) {
                buf[out++] = '/';
                last_was_sep = 1;
            }
            continue;
        }
        buf[out++] = c;
        last_was_sep = 0;
    }

    while (out > 1 && buf[out - 1] == '/') {
        out--;
    }

    if (out == 0) {
        buf[out++] = '.';
    }

    buf[out] = '\0';
    return make_string_owned(buf, out);
}

static void bind(RuntimeValue exports, const char *name, NativeFn fn) {
    module_bind_native(exports, name, fn);
}

void std_init_path(RuntimeValue exports, Environment *module_env) {
    (void)module_env;
    bind(exports, "join", n_join);
    bind(exports, "base", n_basename);
    bind(exports, "dir", n_dirname);
    bind(exports, "ext", n_extname);
    bind(exports, "name", n_stem);
    bind(exports, "norm", n_normalize);
}
