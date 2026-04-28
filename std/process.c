#define _POSIX_C_SOURCE 200809L

#include "process.h"
#include <stdio.h>
#include "../GC/gc.h"
#include "../Runtime/context.h"
#include "../Runtime/errors.h"
#include "../Runtime/module.h"
#include "../Utils/hash.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <sys/select.h>

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

static RuntimeValue process_make_result_object(int code, const char *out, const char *err) {
    Environment *obj_env = env_create_child(NULL);
    gc_push_env(obj_env);

    RuntimeValue obj = make_object(obj_env);
    gc_push_root(&obj);

    env_set_fast(obj_env, "code", get_hash("code"), make_int(code));
    env_set_fast(obj_env, "out", get_hash("out"), make_string(out != NULL ? out : ""));
    env_set_fast(obj_env, "err", get_hash("err"), make_string(err != NULL ? err : ""));

    gc_pop_root();
    gc_pop_env();

    return obj;
}
typedef struct {
    char *data;
    size_t len;
    size_t cap;
} ProcessBuffer;

static void process_buffer_append(ProcessBuffer *b, const char *data, size_t len) {
    if (len == 0) return;

    if (b->len + len + 1 > b->cap) {
        size_t new_cap = b->cap == 0 ? 4096 : b->cap;

        while (new_cap < b->len + len + 1) {
            new_cap *= 2;
        }

        char *new_data = (char *)realloc(b->data, new_cap);
        if (new_data == NULL) {
            free(b->data);
            runtime_error("process.capture: out of memory");
        }

        b->data = new_data;
        b->cap = new_cap;
    }

    memcpy(b->data + b->len, data, len);
    b->len += len;
    b->data[b->len] = '\0';
}

static const char *process_as_string(RuntimeValue value, const char *where) {
    if (value.type != VAL_STRING) {
        runtime_error("%s expects string arguments", where);
    }

    return value.data.managed_string->data;
}

static char **process_build_argv(const char *cmd, RuntimeValue args_array, const char *where) {
    if (args_array.type != VAL_ARRAY) {
        runtime_error("%s expects second argument to be array", where);
    }

    ManagedArray *arr = args_array.data.managed_array;
    int argc = arr->count;

    char **argv = (char **)calloc((size_t)argc + 2, sizeof(char *));
    if (argv == NULL) {
        runtime_error("%s: out of memory allocating argv", where);
    }

    argv[0] = (char *)cmd;

    for (int i = 0; i < argc; i++) {
        argv[i + 1] = (char *)process_as_string(arr->elements[i], where);
    }

    argv[argc + 1] = NULL;
    return argv;
}

static int process_wait_status(int status) {
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }

    if (WIFSIGNALED(status)) {
        return 128 + WTERMSIG(status);
    }

    return 1;
}

static int process_wait_pid(pid_t pid, int *status, const char *where) {
    while (waitpid(pid, status, 0) < 0) {
        if (errno == EINTR) {
            continue;
        }
        runtime_error("%s: waitpid failed", where);
    }
    return 1;
}

static RuntimeValue n_capture(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    (void)ctx;

    if (arg_count != 2) {
        runtime_error("process.capture expects 2 arguments: command, args");
    }

    const char *cmd = process_as_string(args[0], "process.capture");
    char **argv = process_build_argv(cmd, args[1], "process.capture");

    int out_pipe[2];
    int err_pipe[2];

    if (pipe(out_pipe) != 0) {
        free(argv);
        runtime_error("process.capture: stdout pipe failed");
    }

    if (pipe(err_pipe) != 0) {
        close(out_pipe[0]);
        close(out_pipe[1]);
        free(argv);
        runtime_error("process.capture: stderr pipe failed");
    }

    pid_t pid = fork();

    if (pid < 0) {
        close(out_pipe[0]);
        close(out_pipe[1]);
        close(err_pipe[0]);
        close(err_pipe[1]);
        free(argv);
        runtime_error("process.capture: fork failed");
    }

    if (pid == 0) {
        close(out_pipe[0]);
        close(err_pipe[0]);

        dup2(out_pipe[1], STDOUT_FILENO);
        dup2(err_pipe[1], STDERR_FILENO);

        close(out_pipe[1]);
        close(err_pipe[1]);

        execvp(cmd, argv);

        fprintf(stderr, "process.capture: execvp failed for '%s': %s\n", cmd, strerror(errno));
        _exit(127);
    }

    close(out_pipe[1]);
    close(err_pipe[1]);
    free(argv);

    ProcessBuffer out = {0};
    ProcessBuffer err = {0};

    int out_open = 1;
    int err_open = 1;

    while (out_open || err_open) {
        fd_set rfds;
        FD_ZERO(&rfds);

        int max_fd = -1;

        if (out_open) {
            FD_SET(out_pipe[0], &rfds);
            if (out_pipe[0] > max_fd) max_fd = out_pipe[0];
        }

        if (err_open) {
            FD_SET(err_pipe[0], &rfds);
            if (err_pipe[0] > max_fd) max_fd = err_pipe[0];
        }

        int rv = select(max_fd + 1, &rfds, NULL, NULL, NULL);
        if (rv < 0) {
            if (errno == EINTR) {
                continue;
            }

            free(out.data);
            free(err.data);
            close(out_pipe[0]);
            close(err_pipe[0]);
            runtime_error("process.capture: select failed");
        }

        char buf[4096];

        if (out_open && FD_ISSET(out_pipe[0], &rfds)) {
            ssize_t n = read(out_pipe[0], buf, sizeof(buf));
            if (n > 0) {
                process_buffer_append(&out, buf, (size_t)n);
            } else {
                close(out_pipe[0]);
                out_open = 0;
            }
        }

        if (err_open && FD_ISSET(err_pipe[0], &rfds)) {
            ssize_t n = read(err_pipe[0], buf, sizeof(buf));
            if (n > 0) {
                process_buffer_append(&err, buf, (size_t)n);
            } else {
                close(err_pipe[0]);
                err_open = 0;
            }
        }
    }

    int status = 0;

    process_wait_pid(pid, &status, "process.capture");

    RuntimeValue result = process_make_result_object(
        process_wait_status(status),
        out.data,
        err.data
    );

    free(out.data);
    free(err.data);

    return result;
}
static RuntimeValue n_capture_shell(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    (void)ctx;

    if (arg_count != 1) {
        runtime_error("process.capture_shell expects 1 argument: command");
    }
    process_as_string(args[0], "process.capture_shell");

    RuntimeValue shell_args = make_array(2);
    gc_push_root(&shell_args);

    process_array_push(shell_args, make_string("-c"));
    process_array_push(shell_args, args[0]);

    RuntimeValue cmd = make_string("/bin/sh");
    gc_push_root(&cmd);

    RuntimeValue call_args[2] = { cmd, shell_args };
    RuntimeValue result = n_capture(ctx, call_args, 2);

    gc_pop_root();
    gc_pop_root();

    return result;
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

static RuntimeValue n_exec(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    (void)ctx;

    if (arg_count != 2) {
        runtime_error("process.exec expects 2 arguments: command, args");
    }

    const char *cmd = process_as_string(args[0], "process.exec");
    char **argv = process_build_argv(cmd, args[1], "process.exec");

    pid_t pid = fork();

    if (pid < 0) {
        free(argv);
        runtime_error("process.exec: fork failed");
    }

    if (pid == 0) {
        execvp(cmd, argv);

        fprintf(stderr, "process.exec: execvp failed for '%s': %s\n", cmd, strerror(errno));
        _exit(127);
    }

    free(argv);

    int status = 0;

    process_wait_pid(pid, &status, "process.exec");

    return make_int(process_wait_status(status));
}

static RuntimeValue n_shell(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    (void)ctx;

    if (arg_count != 1) {
        runtime_error("process.shell expects 1 argument: command");
    }

    const char *command = process_as_string(args[0], "process.shell");

    pid_t pid = fork();

    if (pid < 0) {
        runtime_error("process.shell: fork failed");
    }

    if (pid == 0) {
        execl("/bin/sh", "sh", "-c", command, (char *)NULL);

        fprintf(stderr, "process.shell: execl failed: %s\n", strerror(errno));
        _exit(127);
    }

    int status = 0;

    process_wait_pid(pid, &status, "process.shell");

    return make_int(process_wait_status(status));
}

static void bind(RuntimeValue exports, const char *name, NativeFn fn) {
    module_bind_native(exports, name, fn);
}

void std_init_process(RuntimeValue exports, Environment *module_env) {
    (void)module_env;

    bind(exports, "args", n_args);
    bind(exports, "capture", n_capture);
    bind(exports, "capture_shell", n_capture_shell);
    bind(exports, "argv", n_args);
    bind(exports, "cwd", n_cwd);
    bind(exports, "exit", n_exit);
    bind(exports, "exec", n_exec);
    bind(exports, "shell", n_shell);
    bind(exports, "system", n_shell);
}
