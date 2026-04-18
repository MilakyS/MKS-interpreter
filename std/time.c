#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L
#include "time.h"
#include "../Runtime/module.h"
#include "../Runtime/errors.h"
#include "../Utils/hash.h"
#include <time.h>
#include <stdint.h>
#include <stdlib.h>
#include <math.h>
#include <unistd.h>

static double epoch_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

static double monotonic_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

static RuntimeValue n_now(const RuntimeValue *args, int arg_count) {
    (void)args; (void)arg_count;
    time_t t = time(NULL);
    char buf[64];
    struct tm tm;
    localtime_r(&t, &tm);
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
    return make_string(buf);
}

static RuntimeValue n_timestamp(const RuntimeValue *args, int arg_count) {
    (void)args; (void)arg_count;
    return make_int((double)time(NULL));
}

static RuntimeValue n_millis(const RuntimeValue *args, int arg_count) {
    (void)args; (void)arg_count;
    return make_int(epoch_ms());
}

static RuntimeValue n_monotonic(const RuntimeValue *args, int arg_count) {
    (void)args; (void)arg_count;
    return make_int(monotonic_ms());
}

static RuntimeValue n_sleep(const RuntimeValue *args, int arg_count) {
    if (arg_count != 1) runtime_error("sleep expects 1 arg (ms)");
    int64_t ms = (int64_t)args[0].data.float_value;
    if (ms < 0) ms = 0;
    struct timespec ts = { (time_t)(ms / 1000), (long)((ms % 1000) * 1000000L) };
    nanosleep(&ts, NULL);
    return make_null();
}

static RuntimeValue n_sleep_s(const RuntimeValue *args, int arg_count) {
    if (arg_count != 1) runtime_error("sleep_s expects 1 arg (seconds)");
    double s = args[0].data.float_value;
    if (s < 0) s = 0;
    struct timespec ts = { (time_t)s, (long)((s - floor(s)) * 1e9) };
    nanosleep(&ts, NULL);
    return make_null();
}

static RuntimeValue n_format(const RuntimeValue *args, int arg_count) {
    if (arg_count != 2) runtime_error("format expects (timestamp_sec, format)");
    if (args[1].type != VAL_STRING) runtime_error("format: fmt must be string");
    time_t t = (time_t)args[0].data.float_value;
    const char *fmt = args[1].data.managed_string->data;
    char buf[128];
    struct tm tm;
    localtime_r(&t, &tm);
    if (strftime(buf, sizeof(buf), fmt, &tm) == 0) return make_string("");
    return make_string(buf);
}

static RuntimeValue n_parse(const RuntimeValue *args, int arg_count) {
    if (arg_count != 2) runtime_error("parse expects (text, format)");
    if (args[0].type != VAL_STRING || args[1].type != VAL_STRING) runtime_error("parse: both args must be string");
    const char *text = args[0].data.managed_string->data;
    const char *fmt = args[1].data.managed_string->data;
    struct tm tm = {0};
    char *ret = strptime(text, fmt, &tm);
    if (ret == NULL) return make_null();
    time_t t = timegm(&tm);
    return make_int((double)t);
}

static void bind(RuntimeValue exports, const char *name, NativeFn fn) {
    module_bind_native(exports, name, fn);
}

void std_init_time(RuntimeValue exports, Environment *module_env) {
    (void)module_env;
    bind(exports, "now", n_now);
    bind(exports, "timestamp", n_timestamp);
    bind(exports, "millis", n_millis);
    bind(exports, "monotonic", n_monotonic);
    bind(exports, "sleep", n_sleep);
    bind(exports, "sleep_s", n_sleep_s);
    bind(exports, "format", n_format);
    bind(exports, "parse", n_parse);
}
