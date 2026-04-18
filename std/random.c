#include "random.h"
#include "../Runtime/module.h"
#include "../Runtime/errors.h"
#include "../Utils/hash.h"
#include <stdlib.h>
#include <time.h>

static RuntimeValue n_rand(const RuntimeValue *args, const int arg_count) {
    (void)args; (void)arg_count;
    double r = (double)rand() / (double)RAND_MAX;
    return make_int(r);
}

static RuntimeValue n_randint(const RuntimeValue *args, const int arg_count) {
    if (arg_count != 2) runtime_error("randint expects 2 args (min, max)");
    int min = (int)args[0].data.float_value;
    int max = (int)args[1].data.float_value;
    if (max < min) runtime_error("randint: max < min");
    int v = min + rand() % (max - min + 1);
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
