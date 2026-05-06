#include "watch.h"
#include "../Eval/eval.h"
#include "../Runtime/errors.h"
#include "../Runtime/context.h"
#include "../Runtime/module.h"
#include "../Runtime/functions.h"
#include "../Utils/hash.h"
#include "../GC/gc.h"
#include "../Lexer/lexer.h"
#include <string.h>
#include <stdlib.h>

typedef struct WatchHandler {
    char *name;
    unsigned int hash;
    const ASTNode *body;
    Environment *env;
    RuntimeValue callable;
    struct WatchHandler *next;
} WatchHandler;

static WatchHandler **watch_head_slot(void) {
    return (WatchHandler **)&mks_context_current()->watch_head;
}

#define watch_head (*watch_head_slot())

int watch_has_any(void) {
    return watch_head != NULL;
}

static char *watch_strdup(const char *s) {
    if (s == NULL) return NULL;
    size_t len = strlen(s);
    char *out = malloc(len + 1);
    if (!out) return NULL;
    memcpy(out, s, len + 1);
    return out;
}

void watch_register(const char *name, unsigned int hash) {
    WatchHandler *wh = malloc(sizeof(WatchHandler));
    if (!wh) return;
    wh->name = watch_strdup(name);
    wh->hash = hash;
    wh->body = NULL;
    wh->env = NULL;
    wh->callable.type = VAL_NULL;
    wh->next = watch_head;
    watch_head = wh;
}

void watch_register_handler(const char *name, unsigned int hash, const ASTNode *body, Environment *env) {
    WatchHandler *wh = malloc(sizeof(WatchHandler));
    if (!wh) return;
    wh->name = watch_strdup(name);
    wh->hash = hash;
    wh->body = body;
    wh->env = env;
    wh->callable.type = VAL_NULL;
    wh->next = watch_head;
    watch_head = wh;
    gc_pin_env(env);
}

void watch_register_callable(const char *name, unsigned int hash, RuntimeValue callable) {
    WatchHandler *wh = malloc(sizeof(WatchHandler));
    if (!wh) return;
    wh->name = watch_strdup(name);
    wh->hash = hash;
    wh->body = NULL;
    wh->env = NULL;
    wh->callable = callable;
    wh->next = watch_head;
    watch_head = wh;
    gc_pin_root_if_needed(&wh->callable);
}

static void invoke_callable(RuntimeValue callable, const RuntimeValue *value, Environment *env) {
    if (callable.type != VAL_NATIVE_FUNC && callable.type != VAL_FUNC && callable.type != VAL_BLUEPRINT) {
        runtime_error("watch: handler is not callable");
    }

    RuntimeValue arg = value ? *value : make_null();
    RuntimeValue res;

    if (callable.type == VAL_NATIVE_FUNC) {
        res = callable.data.native.fn(mks_context_current(), &arg, 1);
    } else if (callable.type == VAL_FUNC) {
        const ASTNode *decl = callable.data.func.node;
        if (decl->data.func_decl.param_count != 1) runtime_error("watch: handler expects 1 param");
        Environment *local_env = env_create_child(callable.data.func.closure_env);
        gc_push_env(local_env);
        env_set(local_env, decl->data.func_decl.params[0], arg);
        res = eval(decl->data.func_decl.body, local_env);
        gc_pop_env();
    } else { /* VAL_BLUEPRINT */
        RuntimeValue args[1] = { arg };
        res = eval_blueprint_construct(callable, args, 1, env);
    }
    (void)res;
}

void watch_trigger(const char *name, unsigned int hash, Environment *env, const RuntimeValue *value) {
    if (watch_head == NULL) {
        return;
    }

    WatchHandler *cur = watch_head;
    while (cur) {
        if (cur->hash == hash && strcmp(cur->name, name) == 0) {
            if (cur->body != NULL) {
                eval(cur->body, cur->env ? cur->env : env);
            } else if (cur->callable.type != VAL_NULL) {
                invoke_callable(cur->callable, value, env);
            }
        }
        cur = cur->next;
    }
}

static const ASTNode *watch_unwrap_single_block(const ASTNode *node) {
    if (node != NULL && node->type == AST_BLOCK && node->data.block.count == 1) {
        return node->data.block.items[0];
    }
    return node;
}

static int watch_match_mod_eq_filter(const ASTNode *body,
                                     const char *name,
                                     unsigned int hash,
                                     int64_t *mod_out,
                                     int64_t *eq_out) {
    const ASTNode *stmt = watch_unwrap_single_block(body);
    if (stmt == NULL || stmt->type != AST_IF_BLOCK || stmt->data.if_block.else_body != NULL) {
        return 0;
    }

    const ASTNode *condition = stmt->data.if_block.condition;
    if (condition == NULL || condition->type != AST_BINOP || condition->data.binop.op != TOKEN_EQ) {
        return 0;
    }

    const ASTNode *left = condition->data.binop.left;
    const ASTNode *right = condition->data.binop.right;
    if (left == NULL || right == NULL || right->type != AST_NUMBER || right->data.number.kind != NUMBER_INT) {
        return 0;
    }
    if (left->type != AST_BINOP || left->data.binop.op != TOKEN_MOD) {
        return 0;
    }

    const ASTNode *mod_left = left->data.binop.left;
    const ASTNode *mod_right = left->data.binop.right;
    if (mod_left == NULL || mod_left->type != AST_IDENTIFIER ||
        mod_right == NULL || mod_right->type != AST_NUMBER ||
        mod_right->data.number.kind != NUMBER_INT) {
        return 0;
    }
    if (mod_left->data.identifier.id_hash != hash || strcmp(mod_left->data.identifier.name, name) != 0) {
        return 0;
    }
    if (mod_right->data.number.int_value == 0) {
        return 0;
    }

    *mod_out = mod_right->data.number.int_value;
    *eq_out = right->data.number.int_value;
    return 1;
}

int watch_trigger_int_add_range(const char *name,
                                unsigned int hash,
                                Environment *env,
                                RuntimeValue *slot,
                                int64_t start,
                                int64_t step,
                                int64_t count) {
    if (watch_head == NULL) {
        return 1;
    }
    if (slot == NULL || slot->type != VAL_INT || step <= 0 || count < 0) {
        return 0;
    }

    for (WatchHandler *cur = watch_head; cur != NULL; cur = cur->next) {
        if (cur->hash != hash || strcmp(cur->name, name) != 0) {
            continue;
        }
        if (cur->body == NULL) {
            if (cur->callable.type != VAL_NULL) {
                return 0;
            }
            continue;
        }
        int64_t mod = 0;
        int64_t eq = 0;
        if (!watch_match_mod_eq_filter(cur->body, name, hash, &mod, &eq)) {
            return 0;
        }
    }

    for (WatchHandler *cur = watch_head; cur != NULL; cur = cur->next) {
        if (cur->hash != hash || strcmp(cur->name, name) != 0 || cur->body == NULL) {
            continue;
        }

        int64_t mod = 0;
        int64_t eq = 0;
        (void)watch_match_mod_eq_filter(cur->body, name, hash, &mod, &eq);
        for (int64_t i = 1; i <= count; i++) {
            const int64_t current = start + step * i;
            if (current % mod == eq) {
                slot->data.int_value = current;
                eval(cur->body, cur->env ? cur->env : env);
            }
        }
    }
    return 1;
}

void watch_clear_all(void) {
    WatchHandler *cur = watch_head;
    while (cur) {
        WatchHandler *next = cur->next;
        if (cur->env != NULL) {
            gc_unpin_env(cur->env);
        }
        gc_unpin_root_if_needed(&cur->callable);
        free(cur->name);
        free(cur);
        cur = next;
    }
    watch_head = NULL;
}


static RuntimeValue n_on(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    (void)ctx;
    if (arg_count != 2) runtime_error("watch.on expects (name, handler)");
    if (args[0].type != VAL_STRING) runtime_error("watch.on: name must be string");
    RuntimeValue handler = args[1];
    unsigned int h = get_hash(args[0].data.managed_string->data);
    watch_register_callable(args[0].data.managed_string->data, h, handler);
    return make_null();
}

static RuntimeValue n_mark(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    (void)ctx;
    if (arg_count != 1) runtime_error("watch.mark expects 1 string arg");
    if (args[0].type != VAL_STRING) runtime_error("watch.mark: name must be string");
    unsigned int h = get_hash(args[0].data.managed_string->data);
    watch_register(args[0].data.managed_string->data, h);
    return make_null();
}

static RuntimeValue n_clear(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    (void)ctx;
    (void)args; (void)arg_count;
    watch_clear_all();
    return make_null();
}

static RuntimeValue n_trigger(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    (void)ctx;
    if (arg_count < 1 || arg_count > 2) runtime_error("watch.trigger expects (name [, value])");
    if (args[0].type != VAL_STRING) runtime_error("watch.trigger: name must be string");
    unsigned int h = get_hash(args[0].data.managed_string->data);
    const RuntimeValue *val = (arg_count == 2) ? &args[1] : NULL;
    watch_trigger(args[0].data.managed_string->data, h, NULL, val);
    return make_null();
}

static void bind(RuntimeValue exports, const char *name, NativeFn fn) {
    module_bind_native(exports, name, fn);
}

void std_init_watch(RuntimeValue exports, Environment *module_env) {
    (void)module_env;
    bind(exports, "on", n_on);
    bind(exports, "mark", n_mark);
    bind(exports, "clear", n_clear);
    bind(exports, "trigger", n_trigger);
}
