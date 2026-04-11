#include "extension.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "errors.h"
#include "../Eval/eval.h"
#include "../Utils/hash.h"
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

static ExtTable ext_tables[3];

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

void extension_free_all(void) {
    for (int i = 0; i < 3; i++) {
        ExtTable *t = &ext_tables[i];
        for (int j = 0; j < t->count; j++) {
            free(t->items[j].name);
        }
        free(t->items);
        t->items = NULL;
        t->count = 0;
        t->cap = 0;
    }
}

void register_extension(const ASTNode *node, Environment *env) {
    if (node->type != AST_EXTEND) return;
    int tgt = node->data.extend.target_type;
    if (tgt < 0 || tgt > 2) return;
    ExtTable *tab = &ext_tables[tgt];

    for (int i = 0; i < node->data.extend.method_count; i++) {
        ASTNode *m = node->data.extend.methods[i];
        ExtMethod em;
        em.hash = m->data.func_decl.name_hash;
        em.name = strdup(m->data.func_decl.name);
        em.func_node = m;
        em.closure_env = env;
        ext_push(tab, em);
    }
}

RuntimeValue dispatch_extension(enum ValueType vtype, unsigned int hash, const char *name, RuntimeValue target, RuntimeValue *args, int arg_count, Environment *env) {
    (void)env;
    int idx = -1;
    if (vtype == VAL_ARRAY) idx = EXT_ARRAY;
    else if (vtype == VAL_STRING) idx = EXT_STRING;
    else if (vtype == VAL_INT) idx = EXT_NUMBER;
    else return make_null();

    ExtTable *tab = &ext_tables[idx];
    for (int i = 0; i < tab->count; i++) {
        if (tab->items[i].hash == hash && strcmp(tab->items[i].name, name) == 0) {
            const ASTNode *decl = tab->items[i].func_node;
            const int param_count = decl->data.func_decl.param_count;
            if (arg_count != param_count) {
                runtime_error("Method '%s' expects %d args, got %d", name, param_count, arg_count);
            }

            Environment *local_env = env_create_child(tab->items[i].closure_env);
            gc_push_env(local_env);
            static unsigned int self_hash = 0;
            if (self_hash == 0) self_hash = get_hash("self");
            env_set_fast(local_env, "self", self_hash, target);
            for (int j = 0; j < param_count; j++) {
                env_set_fast(local_env, decl->data.func_decl.params[j], decl->data.func_decl.param_hashes[j], args[j]);
            }
            const RuntimeValue res = unwrap(eval(decl->data.func_decl.body, local_env));
            gc_pop_env();
            return res;
        }
    }
    return make_null();
}
