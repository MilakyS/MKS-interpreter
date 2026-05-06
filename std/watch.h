#ifndef MKS_STD_WATCH_H
#define MKS_STD_WATCH_H

#include "../Parser/AST.h"
#include "../env/env.h"
#include "../Runtime/value.h"

/* Register plain watch mark (used by `watch x;`). */
void watch_register(const char *name, unsigned int hash);

/* Register AST handler for `on change x -> ... <-`. */
void watch_register_handler(const char *name, unsigned int hash, const ASTNode *body, Environment *env);

/* Register callable handler from std/watch module API. */
void watch_register_callable(const char *name, unsigned int hash, RuntimeValue callable);

int watch_has_any(void);

/* Trigger handlers; value may be NULL when unknown. */
void watch_trigger(const char *name, unsigned int hash, Environment *env, const RuntimeValue *value);

/* Optimized trigger for monotonic integer bulk updates. Returns 1 when handled. */
int watch_trigger_int_add_range(const char *name,
                                unsigned int hash,
                                Environment *env,
                                RuntimeValue *slot,
                                int64_t start,
                                int64_t step,
                                int64_t count);

void watch_clear_all(void);

/* Native std module entry point. */
void std_init_watch(RuntimeValue exports, Environment *module_env);

#endif
