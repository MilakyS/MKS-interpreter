#include "math.h"

#include "../Runtime/context.h"
#include "../Runtime/errors.h"
#include "../Runtime/module.h"

#include <math.h>

typedef double (*MathUnaryFn)(double);
typedef double (*MathBinaryFn)(double, double);

typedef struct MathBinding {
    const char *name;
    NativeFn fn;
} MathBinding;

static void expect_argc(const char *name, int got, int want) {
    if (got != want) {
        runtime_error("math.%s expects %d argument%s, got %d",
                      name,
                      want,
                      want == 1 ? "" : "s",
                      got);
    }
}

static RuntimeValue call_unary(const char *name,
                               MathUnaryFn fn,
                               const RuntimeValue *args,
                               int arg_count) {
    expect_argc(name, arg_count, 1);
    return make_float(fn(runtime_value_as_double(args[0])));
}

static RuntimeValue call_binary(const char *name,
                                MathBinaryFn fn,
                                const RuntimeValue *args,
                                int arg_count) {
    expect_argc(name, arg_count, 2);
    return make_float(fn(runtime_value_as_double(args[0]),
                         runtime_value_as_double(args[1])));
}

#define MATH_UNARY(NAME, C_FN)                                                \
    static RuntimeValue n_##NAME(MKSContext *ctx,                             \
                                 const RuntimeValue *args,                    \
                                 int arg_count) {                             \
        (void)ctx;                                                            \
        return call_unary(#NAME, C_FN, args, arg_count);                      \
    }

#define MATH_BINARY(NAME, C_FN)                                               \
    static RuntimeValue n_##NAME(MKSContext *ctx,                             \
                                 const RuntimeValue *args,                    \
                                 int arg_count) {                             \
        (void)ctx;                                                            \
        return call_binary(#NAME, C_FN, args, arg_count);                     \
    }

MATH_UNARY(sqrt, sqrt)
MATH_UNARY(sin, sin)
MATH_UNARY(cos, cos)
MATH_UNARY(tan, tan)
MATH_UNARY(asin, asin)
MATH_UNARY(acos, acos)
MATH_UNARY(atan, atan)
MATH_UNARY(floor, floor)
MATH_UNARY(ceil, ceil)
MATH_UNARY(log, log)
MATH_UNARY(log10, log10)
MATH_UNARY(exp, exp)
MATH_UNARY(trunc, trunc)

MATH_BINARY(pow, pow)
MATH_BINARY(atan2, atan2)
MATH_BINARY(hypot, hypot)
MATH_BINARY(fmod, fmod)

static RuntimeValue n_round(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    (void)ctx;
    expect_argc("round", arg_count, 1);
    return make_int(llround(runtime_value_as_double(args[0])));
}

static RuntimeValue n_abs(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    (void)ctx;
    expect_argc("abs", arg_count, 1);
    return make_number_from_double(fabs(runtime_value_as_double(args[0])));
}

static RuntimeValue n_min(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    (void)ctx;
    expect_argc("min", arg_count, 2);

    double a = runtime_value_as_double(args[0]);
    double b = runtime_value_as_double(args[1]);
    return make_number_from_double(a < b ? a : b);
}

static RuntimeValue n_max(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    (void)ctx;
    expect_argc("max", arg_count, 2);

    double a = runtime_value_as_double(args[0]);
    double b = runtime_value_as_double(args[1]);
    return make_number_from_double(a > b ? a : b);
}

static RuntimeValue n_square(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    (void)ctx;
    expect_argc("square", arg_count, 1);

    double a = runtime_value_as_double(args[0]);
    return make_number_from_double(a * a);
}

static RuntimeValue n_clamp(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    (void)ctx;
    expect_argc("clamp", arg_count, 3);

    double x = runtime_value_as_double(args[0]);
    double lo = runtime_value_as_double(args[1]);
    double hi = runtime_value_as_double(args[2]);

    if (lo > hi) {
        runtime_error("math.clamp expects low <= high");
    }

    if (x < lo) {
        x = lo;
    }
    if (x > hi) {
        x = hi;
    }

    return make_number_from_double(x);
}

static RuntimeValue n_deg2rad(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    (void)ctx;
    expect_argc("deg2rad", arg_count, 1);
    return make_float(runtime_value_as_double(args[0]) * (MKS_PI / 180.0));
}

static RuntimeValue n_rad2deg(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    (void)ctx;
    expect_argc("rad2deg", arg_count, 1);
    return make_float(runtime_value_as_double(args[0]) * (180.0 / MKS_PI));
}

static RuntimeValue n_isnan(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    (void)ctx;
    expect_argc("isnan", arg_count, 1);
    return make_bool(isnan(runtime_value_as_double(args[0])));
}

static RuntimeValue n_isinf(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    (void)ctx;
    expect_argc("isinf", arg_count, 1);
    return make_bool(isinf(runtime_value_as_double(args[0])));
}

static RuntimeValue math_const(const char *name, double value, int arg_count) {
    expect_argc(name, arg_count, 0);
    return make_float(value);
}

static RuntimeValue n_const_pi(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    (void)ctx;
    (void)args;
    return math_const("pi", MKS_PI, arg_count);
}

static RuntimeValue n_const_tau(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    (void)ctx;
    (void)args;
    return math_const("tau", MKS_TAU, arg_count);
}

static RuntimeValue n_const_e(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    (void)ctx;
    (void)args;
    return math_const("e", MKS_E, arg_count);
}

static void bind_many(RuntimeValue exports, const MathBinding *bindings) {
    for (const MathBinding *binding = bindings; binding->name != NULL; binding++) {
        module_bind_native(exports, binding->name, binding->fn);
    }
}

void std_init_math(RuntimeValue exports, Environment *module_env) {
    (void)module_env;

    static const MathBinding bindings[] = {
        { "sqrt", n_sqrt },
        { "pow", n_pow },
        { "sin", n_sin },
        { "cos", n_cos },
        { "tan", n_tan },
        { "asin", n_asin },
        { "acos", n_acos },
        { "atan", n_atan },
        { "atan2", n_atan2 },
        { "floor", n_floor },
        { "ceil", n_ceil },
        { "round", n_round },
        { "trunc", n_trunc },
        { "abs", n_abs },
        { "min", n_min },
        { "max", n_max },
        { "square", n_square },
        { "clamp", n_clamp },
        { "log", n_log },
        { "log10", n_log10 },
        { "exp", n_exp },
        { "hypot", n_hypot },
        { "fmod", n_fmod },
        { "deg2rad", n_deg2rad },
        { "rad2deg", n_rad2deg },
        { "isnan", n_isnan },
        { "isinf", n_isinf },
        { "pi", n_const_pi },
        { "tau", n_const_tau },
        { "e", n_const_e },

        /* Convenience aliases; existing names above remain canonical. */
        { "sqr", n_square },
        { "ln", n_log },
        { "mod", n_fmod },
        { "rad", n_deg2rad },
        { "deg", n_rad2deg },
        { "fabs", n_abs },
        { "fmin", n_min },
        { "fmax", n_max },
        { "PI", n_const_pi },
        { "TAU", n_const_tau },
        { "E", n_const_e },
        { NULL, NULL }
    };

    bind_many(exports, bindings);
}
