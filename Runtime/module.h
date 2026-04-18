#ifndef MKS_RUNTIME_MODULE_H
#define MKS_RUNTIME_MODULE_H

#include "../env/env.h"
#include "value.h"
#include <stdbool.h>

/* Native module initializer. Must fill the provided exports object. */
typedef void (*NativeModuleInit)(RuntimeValue exports, Environment *module_env);

typedef enum {
    MODULE_KIND_NATIVE,
    MODULE_KIND_FILE
} ModuleKind;

typedef struct ModuleDescriptor {
    const char *id;             /* normalized module id, e.g. "std.math" */
    ModuleKind kind;
    NativeModuleInit native_init; /* for native */
    const char *file_path;        /* relative path for file modules */
} ModuleDescriptor;

/*
 * Initialize module system. Must be called once after global environment is
 * created so that module scopes can inherit builtins from it.
 */
void module_system_init(Environment *global_env);

/* Register a C/native module that can be loaded via `using "name";`. */
void module_register_native(const char *name, NativeModuleInit init_fn);

/* Access builtin descriptors (for tests/introspection) */
const ModuleDescriptor *module_builtin_descriptors(void);

/* Helper: bind a native function into exports/object. */
void module_bind_native(RuntimeValue exports, const char *name, NativeFn fn);

/* Load a module (native or .mks) and return its exports object. */
RuntimeValue module_import(const char *spec, const char *alias, bool is_legacy_path, bool star_import, Environment *requester_env);

/* Free all loaded modules and associated AST/programs. */
void module_free_all(void);

/* Helper to derive default alias from specifier (basename without extension). */
void module_guess_alias(const char *spec, char *out, size_t out_size);

#endif /* MKS_RUNTIME_MODULE_H */
