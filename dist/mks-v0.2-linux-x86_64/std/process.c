#define _POSIX_C_SOURCE 200809L

#include "process.h"

#include "../GC/gc.h"
#include "../Runtime/context.h"
#include "../Runtime/errors.h"
#include "../Runtime/module.h"

#include <limits.h>
#include <stdlib.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

static void process_array_push(RuntimeValue arr, RuntimeValue value) {
    ManagedArray *a = arr.data.managed_array;
    if (a->count >= a->capacity) {
        int new_cap = a->capacity > 0 ? a->capacity * 2 : 4;
        RuntimeValue *new_items =
            (RuntimeValue *)realloc(a->elements, sizeof(RuntimeValue) * (size_t)new_cap);
        if (new_items == NULL) {
            runtime_error("process.args: out of memory growing array");
        }
        a->elements = new_items;
        a->capacity = new_cap;
    }
    a->elements[a->count++] = value;
}

static RuntimeValue n_args(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    (void)args;
    if (arg_count != 0) {
        runtime_error("process.args expects 0 arguments");
    }

    RuntimeValue arr = make_array(ctx->cli_argc > 0 ? ctx->cli_argc : 4);
    gc_push_root(&arr);

    for (int i = 0; i < ctx->cli_argc; i++) {
        RuntimeValue item = make_string(ctx->cli_argv[i] != NULL ? ctx->cli_argv[i] : "");
        process_array_push(arr, item);
    }

    gc_pop_root();
    return arr;
}

static RuntimeValue n_cwd(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    (void)ctx;
    (void)args;
    if (arg_count != 0) {
        runtime_error("process.cwd expects 0 arguments");
    }

    char buf[PATH_MAX];
    if (getcwd(buf, sizeof(buf)) == NULL) {
        runtime_error("process.cwd: getcwd failed");
    }
    return make_string(buf);
}

static RuntimeValue n_exit(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    (void)ctx;
    int code = 0;
    if (arg_count > 1) {
        runtime_error("process.exit expects at most 1 argument");
    }
    if (arg_count == 1) {
        code = (int)runtime_value_as_int(args[0]);
    }
    mks_context_abort(code);
    return make_null();
}

static void bind(RuntimeValue exports, const char *name, NativeFn fn) {
    module_bind_native(exports, name, fn);
}

void std_init_process(RuntimeValue exports, Environment *module_env) {
    (void)module_env;
    bind(exports, "args", n_args);
    bind(exports, "cwd", n_cwd);
    bind(exports, "exit", n_exit);
}
