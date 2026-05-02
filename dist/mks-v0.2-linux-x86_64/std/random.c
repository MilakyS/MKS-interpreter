#include "random.h"
#include "../Runtime/module.h"
#include "../Runtime/errors.h"
#include "../Runtime/context.h"
#include "../Utils/hash.h"
#include <stdint.h>

static uint64_t random_next_u64(MKSContext *ctx) {
    uint64_t x = ctx != NULL ? ctx->random_state : mks_context_current()->random_state;
    if (x == 0) {
        x = 0x9e3779b97f4a7c15ULL;
    }
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    if (ctx != NULL) {
        ctx->random_state = x;
    } else {
        mks_context_current()->random_state = x;
    }
    return x * 0x2545f4914f6cdd1dULL;
}

static RuntimeValue n_rand(MKSContext *ctx, const RuntimeValue *args, const int arg_count) {
    (void)args; (void)arg_count;
    double r = (double)(random_next_u64(ctx) >> 11) * (1.0 / 9007199254740992.0);
    return make_float(r);
}

static RuntimeValue n_randint(MKSContext *ctx, const RuntimeValue *args, const int arg_count) {
    if (arg_count != 2) runtime_error("randint expects 2 args (min, max)");
    int min = (int)runtime_value_as_int(args[0]);
    int max = (int)runtime_value_as_int(args[1]);
    if (max < min) runtime_error("randint: max < min");
    int v = min + (int)(random_next_u64(ctx) % (uint64_t)(max - min + 1));
    return make_int(v);
}

static void bind(RuntimeValue exports, const char *name, NativeFn fn) {
    RuntimeValue val;
    val.type = VAL_NATIVE_FUNC;
    val.data.native.fn = fn;
    val.data.native.ctx = NULL;
    val.original_type = VAL_NATIVE_FUNC;
    env_set_fast(exports.data.obj_env, name, get_hash(name), val);
}

void std_init_random(RuntimeValue exports, Environment *module_env) {
    (void)module_env;
    bind(exports, "rand", n_rand);
    bind(exports, "randint", n_randint);
}
