#include "extension.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "errors.h"
#include "context.h"
#include "../Eval/eval.h"
#include "../GC/gc.h"

typedef struct ExtMethod {
    unsigned int hash;
    char *name;
    ASTNode *func_node;
    Environment *closure_env;
} ExtMethod;

typedef struct ExtTable {
    ExtMethod *items;
    int count;
    int cap;
} ExtTable;

static ExtTable *extension_tables(void) {
    MKSContext *ctx = mks_context_current();
    if (ctx->extension_tables == NULL) {
        ctx->extension_tables = calloc(3, sizeof(ExtTable));
        if (ctx->extension_tables == NULL) {
            runtime_error("Out of memory allocating extension tables");
        }
    }
    return (ExtTable *)ctx->extension_tables;
}

static inline RuntimeValue unwrap(RuntimeValue v) {
    if (v.type == VAL_RETURN) {
        v.type = v.original_type;
    }
    return v;
}

static void ext_push(ExtTable *t, ExtMethod m) {
    if (t->count >= t->cap) {
        int new_cap = (t->cap == 0) ? 8 : t->cap * 2;
        ExtMethod *tmp = realloc(t->items, sizeof(ExtMethod) * (size_t)new_cap);
        if (!tmp) {
            fprintf(stderr, "[MKS] Out of memory in extension table\n");
            exit(1);
        }
        t->items = tmp;
        t->cap = new_cap;
    }
    t->items[t->count++] = m;
}

static int ext_find_conflict(const ExtTable *t, unsigned int hash, const char *name) {
    for (int i = 0; i < t->count; i++) {
        if (t->items[i].hash == hash && strcmp(t->items[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

void extension_free_all(void) {
    ExtTable *ext_tables = (ExtTable *)mks_context_current()->extension_tables;
    if (ext_tables == NULL) {
        return;
    }

    for (int i = 0; i < 3; i++) {
        ExtTable *t = &ext_tables[i];
        for (int j = 0; j < t->count; j++) {
            gc_unpin_env(t->items[j].closure_env);
            free(t->items[j].name);
        }
        free(t->items);
        t->items = NULL;
        t->count = 0;
        t->cap = 0;
    }
    free(ext_tables);
    mks_context_current()->extension_tables = NULL;
}

void register_extension(const ASTNode *node, Environment *env) {
    if (node->type != AST_EXTEND) return;
    int tgt = node->data.extend.target_type;
    if (tgt < 0 || tgt > 2) return;
    ExtTable *tab = &extension_tables()[tgt];

    for (int i = 0; i < node->data.extend.method_count; i++) {
        ASTNode *m = node->data.extend.methods[i];
        if (ext_find_conflict(tab, m->data.func_decl.name_hash, m->data.func_decl.name) >= 0) {
            runtime_error("Duplicate extension method '%s' for target family", m->data.func_decl.name);
        }
        ExtMethod em;
        em.hash = m->data.func_decl.name_hash;
        em.name = strdup(m->data.func_decl.name);
        if (em.name == NULL) {
            runtime_error("Out of memory copying extension method name");
        }
        em.func_node = m;
        em.closure_env = env;
        gc_pin_env(env);
        ext_push(tab, em);
    }
}

RuntimeValue dispatch_extension(enum ValueType vtype, unsigned int hash, const char *name, RuntimeValue target, RuntimeValue *args, int arg_count, Environment *env) {
    (void)env;
    int idx = -1;
    if (vtype == VAL_ARRAY) idx = EXT_ARRAY;
    else if (vtype == VAL_STRING) idx = EXT_STRING;
    else if (vtype == VAL_INT || vtype == VAL_FLOAT) idx = EXT_NUMBER;
    else return make_null();

    ExtTable *tab = &extension_tables()[idx];
    for (int i = 0; i < tab->count; i++) {
        if (tab->items[i].hash == hash && strcmp(tab->items[i].name, name) == 0) {
            const ASTNode *decl = tab->items[i].func_node;
            const int param_count = decl->data.func_decl.param_count;
            if (arg_count != param_count) {
                runtime_error("Method '%s' expects %d args, got %d", name, param_count, arg_count);
            }

            Environment *local_env = env_create_child(tab->items[i].closure_env);
            gc_push_env(local_env);
            env_set(local_env, "self", target);
            for (int j = 0; j < param_count; j++) {
                env_set(local_env, decl->data.func_decl.params[j], args[j]);
            }
            const RuntimeValue res = unwrap(eval(decl->data.func_decl.body, local_env));
            gc_pop_env();
            return res;
        }
    }
    return make_null();
}
