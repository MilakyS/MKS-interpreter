#include "math.h"
#include "../Runtime/module.h"
#include "../Runtime/errors.h"
#include "../Runtime/context.h"
#include "../Utils/hash.h"
#include <math.h>
#include <stdbool.h>

static RuntimeValue n_math1(double (*fn)(double), const RuntimeValue *args, int arg_count, const char *name) {
    if (arg_count != 1) runtime_error("%s expects 1 arg", name);
    return make_float(fn(runtime_value_as_double(args[0])));
}

static RuntimeValue n_math2(double (*fn)(double, double), const RuntimeValue *args, int arg_count, const char *name) {
    if (arg_count != 2) runtime_error("%s expects 2 args", name);
    return make_float(fn(runtime_value_as_double(args[0]), runtime_value_as_double(args[1])));
}

static RuntimeValue n_sqrt(MKSContext *ctx, const RuntimeValue *args, int arg_count) { (void)ctx; return n_math1(sqrt, args, arg_count, "sqrt"); }
static RuntimeValue n_sin (MKSContext *ctx, const RuntimeValue *args, int arg_count) { (void)ctx; return n_math1(sin,  args, arg_count, "sin"); }
static RuntimeValue n_cos (MKSContext *ctx, const RuntimeValue *args, int arg_count) { (void)ctx; return n_math1(cos,  args, arg_count, "cos"); }
static RuntimeValue n_tan (MKSContext *ctx, const RuntimeValue *args, int arg_count) { (void)ctx; return n_math1(tan,  args, arg_count, "tan"); }
static RuntimeValue n_asin(MKSContext *ctx, const RuntimeValue *args, int arg_count) { (void)ctx; return n_math1(asin, args, arg_count, "asin"); }
static RuntimeValue n_acos(MKSContext *ctx, const RuntimeValue *args, int arg_count) { (void)ctx; return n_math1(acos, args, arg_count, "acos"); }
static RuntimeValue n_atan(MKSContext *ctx, const RuntimeValue *args, int arg_count) { (void)ctx; return n_math1(atan, args, arg_count, "atan"); }
static RuntimeValue n_atan2(MKSContext *ctx, const RuntimeValue *args, int arg_count) { (void)ctx; return n_math2(atan2, args, arg_count, "atan2"); }
static RuntimeValue n_floor(MKSContext *ctx, const RuntimeValue *args, int arg_count) { (void)ctx; return n_math1(floor, args, arg_count, "floor"); }
static RuntimeValue n_ceil (MKSContext *ctx, const RuntimeValue *args, int arg_count) { (void)ctx; return n_math1(ceil,  args, arg_count, "ceil"); }
static RuntimeValue n_round(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    (void)ctx;
    if (arg_count != 1) runtime_error("round expects 1 arg");
    return make_int(llround(runtime_value_as_double(args[0])));
}

static RuntimeValue n_log(MKSContext *ctx, const RuntimeValue *args, int arg_count) { (void)ctx; return n_math1(log, args, arg_count, "log"); }
static RuntimeValue n_log10(MKSContext *ctx, const RuntimeValue *args, int arg_count) { (void)ctx; return n_math1(log10, args, arg_count, "log10"); }
static RuntimeValue n_exp(MKSContext *ctx, const RuntimeValue *args, int arg_count) { (void)ctx; return n_math1(exp, args, arg_count, "exp"); }
static RuntimeValue n_hypot(MKSContext *ctx, const RuntimeValue *args, int arg_count) { (void)ctx; return n_math2(hypot, args, arg_count, "hypot"); }

static RuntimeValue n_pow(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    (void)ctx;
    if (arg_count != 2) runtime_error("pow expects 2 args");
    return make_float(pow(runtime_value_as_double(args[0]), runtime_value_as_double(args[1])));
}

static RuntimeValue n_abs(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    (void)ctx;
    if (arg_count != 1) runtime_error("abs expects 1 arg");
    return make_number_from_double(fabs(runtime_value_as_double(args[0])));
}

static RuntimeValue n_min(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    (void)ctx;
    if (arg_count != 2) runtime_error("min expects 2 args");
    double a = runtime_value_as_double(args[0]);
    double b = runtime_value_as_double(args[1]);
    return make_number_from_double(a < b ? a : b);
}

static RuntimeValue n_max(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    (void)ctx;
    if (arg_count != 2) runtime_error("max expects 2 args");
    double a = runtime_value_as_double(args[0]);
    double b = runtime_value_as_double(args[1]);
    return make_number_from_double(a > b ? a : b);
}

static RuntimeValue n_square(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    (void)ctx;
    if (arg_count != 1) runtime_error("square expects 1 arg");
    double a = runtime_value_as_double(args[0]);
    return make_number_from_double(a * a);
}

static RuntimeValue n_clamp(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    (void)ctx;
    if (arg_count != 3) runtime_error("clamp expects (x, lo, hi)");
    double x = runtime_value_as_double(args[0]);
    double lo = runtime_value_as_double(args[1]);
    double hi = runtime_value_as_double(args[2]);
    if (lo > hi) runtime_error("clamp: lo > hi");
    if (x < lo) x = lo;
    if (x > hi) x = hi;
    return make_number_from_double(x);
}

static RuntimeValue n_deg2rad(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    (void)ctx;
    if (arg_count != 1) runtime_error("deg2rad expects 1 arg");
    return make_float(runtime_value_as_double(args[0]) * (MKS_PI / 180.0));
}

static RuntimeValue n_rad2deg(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    (void)ctx;
    if (arg_count != 1) runtime_error("rad2deg expects 1 arg");
    return make_float(runtime_value_as_double(args[0]) * (180.0 / MKS_PI));
}

static RuntimeValue n_const_pi(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    (void)ctx;
    (void)args; (void)arg_count;
    return make_float(MKS_PI);
}

static RuntimeValue n_const_tau(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    (void)ctx;
    (void)args; (void)arg_count;
    return make_float(MKS_TAU);
}

static RuntimeValue n_const_e(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    (void)ctx;
    (void)args; (void)arg_count;
    return make_float(MKS_E);
}

static void bind(RuntimeValue exports, const char *name, NativeFn fn) {
    RuntimeValue v;
    v.type = VAL_NATIVE_FUNC;
    v.original_type = VAL_NATIVE_FUNC;
    v.data.native.fn = fn;
    v.data.native.ctx = NULL;
    env_set_fast(exports.data.obj_env, name, get_hash(name), v);
}

void std_init_math(RuntimeValue exports, Environment *module_env) {
    (void)module_env;
    bind(exports, "sqrt", n_sqrt);
    bind(exports, "pow", n_pow);
    bind(exports, "sin", n_sin);
    bind(exports, "cos", n_cos);
    bind(exports, "tan", n_tan);
    bind(exports, "asin", n_asin);
    bind(exports, "acos", n_acos);
    bind(exports, "atan", n_atan);
    bind(exports, "atan2", n_atan2);
    bind(exports, "floor", n_floor);
    bind(exports, "ceil", n_ceil);
    bind(exports, "round", n_round);
    bind(exports, "abs", n_abs);
    bind(exports, "min", n_min);
    bind(exports, "max", n_max);
    bind(exports, "square", n_square);
    bind(exports, "clamp", n_clamp);
    bind(exports, "log", n_log);
    bind(exports, "log10", n_log10);
    bind(exports, "exp", n_exp);
    bind(exports, "hypot", n_hypot);
    bind(exports, "deg2rad", n_deg2rad);
    bind(exports, "rad2deg", n_rad2deg);
    bind(exports, "pi", n_const_pi);
    bind(exports, "tau", n_const_tau);
    bind(exports, "e", n_const_e);
}
