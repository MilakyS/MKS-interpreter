#define _XOPEN_SOURCE 700
#include "tty.h"
#include "../Runtime/module.h"
#include "../Runtime/errors.h"
#include "../Runtime/context.h"
#include "../Utils/hash.h"

#include <termios.h>
#include <unistd.h>
#include <sys/select.h>
#include <string.h>

static struct termios orig_termios;
static int raw_enabled = 0;

static void set_raw_mode(int enable) {
    if (!isatty(STDIN_FILENO)) {
        /* Non-tty stdin; ignore */
        return;
    }
    if (enable && !raw_enabled) {
        struct termios t;
        if (tcgetattr(STDIN_FILENO, &orig_termios) != 0) {
            return;
        }
        t = orig_termios;
        t.c_lflag &= (unsigned int)~(ICANON | ECHO);
        t.c_cc[VMIN] = 0;
        t.c_cc[VTIME] = 0;
        if (tcsetattr(STDIN_FILENO, TCSANOW, &t) != 0) {
            return;
        }
        raw_enabled = 1;
    } else if (!enable && raw_enabled) {
        tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios);
        raw_enabled = 0;
    }
}

static RuntimeValue n_set_raw(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    (void)ctx;
    int enable = 1;
    if (arg_count == 1) {
        enable = (int)runtime_value_as_int(args[0]);
    }
    set_raw_mode(enable != 0);
    return make_null();
}

static RuntimeValue n_readch(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    (void)ctx; (void)args; (void)arg_count;
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(STDIN_FILENO, &rfds);
    struct timeval tv = {0, 0};
    int rv = select(STDIN_FILENO + 1, &rfds, NULL, NULL, &tv);
    if (rv <= 0) return make_null();
    char c;
    ssize_t n = read(STDIN_FILENO, &c, 1);
    if (n <= 0) return make_null();
    return make_string_len(&c, 1);
}

static void bind(RuntimeValue exports, const char *name, NativeFn fn) {
    module_bind_native(exports, name, fn);
}

void std_init_tty(RuntimeValue exports, Environment *module_env) {
    (void)module_env;
    bind(exports, "set_raw", n_set_raw);
    bind(exports, "setraw", n_set_raw);
    bind(exports, "readch", n_readch);
}
