#ifndef MKS_MODULE_API_H
#define MKS_MODULE_API_H

/* Public API for authoring native MKS modules. */

#include "Runtime/value.h"
#include "Runtime/context.h"
#include "env/env.h"
#include "Runtime/module.h"
#include "Runtime/errors.h"

/* ABI versioning: modules must export mks_module_abi_version() returning this value. */
#define MKS_MODULE_ABI_VERSION 1

/* Convenience macro for defining the module init entrypoint. */
#define MKS_MODULE_INIT(fn_name) void fn_name(RuntimeValue exports, Environment *module_env)

/* Macro for modules to declare ABI version compliance. */
#define MKS_MODULE_DECLARE_ABI \
    int mks_module_abi_version(void) { return MKS_MODULE_ABI_VERSION; }

/* Bind a native function into exports. */
#define MKS_EXPORT_NATIVE(exports_obj, name, c_fn) module_bind_native(exports_obj, name, c_fn)

/* Optional auto-registration (compile-time linked modules). */
#if defined(__GNUC__) || defined(__clang__)
#define MKS_MODULE_REGISTER(ID, INIT_FN) \
    __attribute__((constructor)) static void mks__auto_reg_##INIT_FN(void) { module_register_native(ID, INIT_FN); }
#else
#define MKS_MODULE_REGISTER(ID, INIT_FN) \
    static void mks__auto_reg_##INIT_FN(void); \
    static int mks__auto_reg_flag_##INIT_FN = (mks__auto_reg_##INIT_FN(), 0); \
    static void mks__auto_reg_##INIT_FN(void) { module_register_native(ID, INIT_FN); }
#endif

/* Typical signature for native functions. */
/* typedef RuntimeValue (*NativeFn)(MKSContext *ctx, const RuntimeValue *args, int arg_count); */

#endif /* MKS_MODULE_API_H */
