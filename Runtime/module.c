#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "module.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <libgen.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#include "errors.h"
#include "../Lexer/lexer.h"
#include "../Parser/parser.h"
#include "../Eval/eval.h"
#include "../GC/gc.h"
#include "../Utils/file.h"
#include "../Utils/hash.h"
#include "../std/registry.h"

typedef struct NativeModule {
    char *name;
    NativeModuleInit init_fn;
    struct NativeModule *next;
} NativeModule;

typedef struct LoadedModule {
    char *key; /* abs path for .mks or name for native */
    RuntimeValue exports;
    Environment *env;
    struct LoadedModule *next;
} LoadedModule;

typedef struct ProgramNode {
    ASTNode *program;
    struct ProgramNode *next;
} ProgramNode;

static NativeModule *native_registry = NULL; /* dynamic registrations */
static LoadedModule *loaded_modules = NULL;
static ProgramNode *programs = NULL;
static Environment *modules_parent_env = NULL;

static ModuleDescriptor builtin_modules[] = {
    { "std.math",   MODULE_KIND_NATIVE, NULL, NULL },   /* native init assigned in module_system_init */
    { "std.time",   MODULE_KIND_NATIVE, NULL, NULL },
    { "std.random", MODULE_KIND_NATIVE, NULL, NULL },
    { "std.watch",  MODULE_KIND_NATIVE, NULL, NULL },
    { "std.fs",     MODULE_KIND_NATIVE, NULL, NULL },
    { "std.string", MODULE_KIND_FILE,   NULL, "std/string.mks" },
    { "std.array",  MODULE_KIND_FILE,   NULL, "std/array.mks" },
    { "std.bool",   MODULE_KIND_FILE,   NULL, "std/bool.mks" },
    { "std",        MODULE_KIND_FILE,   NULL, "std/std.mks" },
    { NULL, MODULE_KIND_NATIVE, NULL, NULL }
};

static int try_realpath(const char *candidate, char *out);
static RuntimeValue load_script(const char *abs_path);
static LoadedModule *find_loaded(const char *key);
static void remember_loaded(const char *key, RuntimeValue exports, Environment *env);
static int make_module_id_path(const char *module_id, char *out);
static void normalize_spec_to_id(const char *spec, char *out, size_t out_size);
static void import_into_env(Environment *dst, Environment *src);
static int alias_already_bound(Environment *env, const char *alias, RuntimeValue exports);

void module_system_init(Environment *global_env) {
    modules_parent_env = global_env;
    native_registry = NULL;
    loaded_modules = NULL;
    programs = NULL;
    std_registry_init();
}

void module_register_native(const char *name, NativeModuleInit init_fn) {
    NativeModule *n = (NativeModule *)malloc(sizeof(NativeModule));
    if (!n) runtime_error("Out of memory registering native module");
    n->name = strdup(name);
    if (!n->name) runtime_error("Out of memory registering native module name");
    n->init_fn = init_fn;
    n->next = native_registry;
    native_registry = n;
}

const ModuleDescriptor *module_builtin_descriptors(void) {
    return builtin_modules;
}

void module_bind_native(RuntimeValue exports, const char *name, NativeFn fn) {
    RuntimeValue v;
    v.type = VAL_NATIVE_FUNC;
    v.original_type = VAL_NATIVE_FUNC;
    v.data.native.fn = fn;
    v.data.native.ctx = NULL;
    env_set_fast(exports.data.obj_env, name, get_hash(name), v);
}

static LoadedModule *find_loaded(const char *key) {
    for (LoadedModule *m = loaded_modules; m != NULL; m = m->next) {
        if (strcmp(m->key, key) == 0) return m;
    }
    return NULL;
}

static void remember_loaded(const char *key, RuntimeValue exports, Environment *env) {
    LoadedModule *m = (LoadedModule *)malloc(sizeof(LoadedModule));
    if (!m) runtime_error("Out of memory while tracking loaded modules");
    m->key = strdup(key);
    if (!m->key) runtime_error("Out of memory while tracking loaded modules key");
    m->exports = exports;
    m->env = env;
    m->next = loaded_modules;
    loaded_modules = m;

    /* Pin module environment so GC cannot collect it even если alias пропущен/занят. */
    gc_push_env(env);
}

void module_free_all(void) {
    NativeModule *n = native_registry;
    while (n != NULL) {
        NativeModule *next = n->next;
        free(n->name);
        free(n);
        n = next;
    }
    native_registry = NULL;

    LoadedModule *lm = loaded_modules;
    while (lm != NULL) {
        LoadedModule *next = lm->next;
        free(lm->key);
        free(lm);
        lm = next;
    }
    loaded_modules = NULL;

    ProgramNode *p = programs;
    while (p != NULL) {
        ProgramNode *next = p->next;
        delete_ast_node(p->program);
        free(p);
        p = next;
    }
    programs = NULL;
}

static int try_realpath(const char *candidate, char *out) {
    if (candidate == NULL) return 0;
    if (realpath(candidate, out) != NULL) {
        return 1;
    }
    return 0;
}

static int append_ext_if_missing(const char *path, char *buffer, size_t buffer_size) {
    if (!path || buffer_size == 0) return 0;
    const char *dot = strrchr(path, '.');
    int needed = (int)strlen(path) + (dot ? 0 : 4);
    if (needed >= (int)buffer_size) return 0;
    int written;
    if (dot == NULL) {
        written = snprintf(buffer, buffer_size, "%s.mks", path);
    } else {
        written = snprintf(buffer, buffer_size, "%s", path);
    }
    return written > 0 && written < (int)buffer_size;
}

static const char *get_exec_dir(void) {
    static char exec_dir[PATH_MAX] = "";
    static int exec_dir_inited = 0;
    if (exec_dir_inited) return exec_dir;

    char path[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (len <= 0 || len >= (ssize_t)sizeof(path)) {
        exec_dir[0] = '\0';
        exec_dir_inited = 1;
        return exec_dir;
    }
    path[len] = '\0';
    char *dir = dirname(path);
    strncpy(exec_dir, dir, sizeof(exec_dir) - 1);
    exec_dir[sizeof(exec_dir) - 1] = '\0';
    exec_dir_inited = 1;
    return exec_dir;
}

static int make_absolute_path(const char *path, char *out) {
    if (path == NULL) return 0;

    char with_ext[PATH_MAX];
    append_ext_if_missing(path, with_ext, sizeof(with_ext));

    if (try_realpath(path, out) || try_realpath(with_ext, out)) return 1;

    const char *current_file = runtime_current_file();
    if (current_file != NULL) {
        const char *slash = strrchr(current_file, '/');
        if (slash != NULL) {
            size_t dir_len = (size_t)(slash - current_file);
            if (dir_len < PATH_MAX) {
                char tmp[PATH_MAX];
                memcpy(tmp, current_file, dir_len);
                tmp[dir_len] = '\0';
                char tmp_out[PATH_MAX];
                if (snprintf(tmp_out, PATH_MAX, "%s/%s", tmp, path) < PATH_MAX &&
                    try_realpath(tmp_out, out)) return 1;
                if (snprintf(tmp_out, PATH_MAX, "%s/%s", tmp, with_ext) < PATH_MAX &&
                    try_realpath(tmp_out, out)) return 1;
            }
        }
    }

    const char *base = get_exec_dir();
    if (base && base[0]) {
        char tmp_out[PATH_MAX];
        if (snprintf(tmp_out, PATH_MAX, "%s/%s", base, with_ext) < PATH_MAX &&
            try_realpath(tmp_out, out)) return 1;
        char parent[PATH_MAX];
        if (snprintf(parent, PATH_MAX, "%s/..", base) < PATH_MAX) {
            if (snprintf(tmp_out, PATH_MAX, "%s/%s", parent, with_ext) < PATH_MAX &&
                try_realpath(tmp_out, out)) return 1;
        }
    }

    const char *env_std = getenv("MKS_STD_PATH");
    if (env_std && env_std[0]) {
        char tmp_out[PATH_MAX];
        if (snprintf(tmp_out, PATH_MAX, "%s/%s", env_std, with_ext) < PATH_MAX &&
            try_realpath(tmp_out, out)) return 1;
    }

    static const char *system_std_paths[] = {"/usr/local/share/mks", "/usr/share/mks", NULL};
    for (int i = 0; system_std_paths[i] != NULL; i++) {
        char tmp_out[PATH_MAX];
        if (snprintf(tmp_out, PATH_MAX, "%s/%s", system_std_paths[i], with_ext) < PATH_MAX &&
            try_realpath(tmp_out, out)) return 1;
    }
    return 0;
}

static int make_module_id_path(const char *module_id, char *out) {
    if (!module_id || !out) return 0;
    size_t len = strlen(module_id);
    if (len == 0 || len >= PATH_MAX - 4) return 0;
    size_t j = 0;
    for (size_t i = 0; i < len && j + 1 < PATH_MAX; i++) {
        char c = module_id[i];
        out[j++] = (c == '.') ? '/' : c;
    }
    out[j] = '\0';
    char tmp[PATH_MAX];
    if (!append_ext_if_missing(out, tmp, sizeof(tmp))) return 0;
    size_t to_copy = strlen(tmp) + 1;
    if (to_copy > PATH_MAX) to_copy = PATH_MAX;
    memcpy(out, tmp, to_copy);
    out[PATH_MAX - 1] = '\0';
    return 1;
}

static void normalize_spec_to_id(const char *spec, char *out, size_t out_size) {
    size_t len = strlen(spec);
    if (len >= out_size) len = out_size - 1;
    size_t j = 0;
    for (size_t i = 0; i < len && j + 1 < out_size; i++) {
        char c = spec[i];
        if (c == '/' || c == '\\') c = '.';
        out[j++] = c;
    }
    out[j] = '\0';
    char *ext = strrchr(out, '.');
    if (ext && strcmp(ext, ".mks") == 0) {
        *ext = '\0';
    }
}

/* Copy exported bindings into the requester env (legacy semantics). */
static void import_into_env(Environment *dst, Environment *src) {
    if (dst == NULL || src == NULL || src->buckets == NULL) return;

    for (size_t i = 0; i < src->bucket_count; i++) {
        EnvVar *entry = src->buckets[i];
        while (entry != NULL) {
            if (strcmp(entry->name, "exports") != 0) {
                env_set_fast(dst, entry->name, entry->hash, entry->value);
            }
            entry = entry->next;
        }
    }
}

static int alias_already_bound(Environment *env, const char *alias, RuntimeValue exports) {
    RuntimeValue existing;
    if (env_try_get(env, alias, get_hash(alias), &existing)) {
        if (existing.type == VAL_OBJECT && existing.data.obj_env == exports.data.obj_env) {
            return 1; /* same module already bound under this alias */
        }
        return 1; /* different binding exists: keep existing alias, but still allow import */
    }
    return 0;
}

static RuntimeValue load_script(const char *abs_path) {
    char *source = mks_read_file(abs_path);
    if (source == NULL) runtime_error("Could not read file '%s'", abs_path);

    const char *prev_file = runtime_push_file(abs_path);
    const char *prev_source = runtime_push_source(source);

    struct Lexer lexer;
    Token_init(&lexer, source);

    Parser parser;
    parser_init(&parser, &lexer);

    ASTNode *program = parser_parse_program(&parser);

    Environment *module_env = env_create_child(modules_parent_env);
    gc_push_env(module_env);

    RuntimeValue exports = make_object(module_env);
    gc_push_root(&exports);
    env_set_fast(module_env, "exports", get_hash("exports"), exports);

    if (program != NULL) {
        eval(program, module_env);
    }

    ProgramNode *pnode = (ProgramNode *)malloc(sizeof(ProgramNode));
    if (pnode == NULL) {
        runtime_error("Out of memory tracking module program");
    }
    pnode->program = program;
    pnode->next = programs;
    programs = pnode;

    free(source);
    runtime_pop_source(prev_source);
    runtime_pop_file(prev_file);

    gc_pop_root();
    gc_pop_env();

    remember_loaded(abs_path, exports, module_env);
    return exports;
}

static RuntimeValue load_native_with_init(const char *key, NativeModuleInit init_fn) {
    Environment *module_env = env_create_child(modules_parent_env);
    gc_push_env(module_env);

    RuntimeValue exports = make_object(module_env);
    gc_push_root(&exports);
    env_set_fast(module_env, "exports", get_hash("exports"), exports);

    init_fn(exports, module_env);

    gc_pop_root();
    gc_pop_env();

    remember_loaded(key, exports, module_env);
    return exports;
}

RuntimeValue module_import(const char *spec, const char *alias, bool is_legacy_path, bool star_import, Environment *requester_env) {
    if (spec == NULL) runtime_error("Import spec is null");
    if (star_import) {
        runtime_error("Star-import is not supported; import via namespace instead");
    }

    char key[PATH_MAX];
    char normalized_id[PATH_MAX];
    normalize_spec_to_id(spec, normalized_id, sizeof(normalized_id));

    /* 1a. Dynamic native registry (module_register_native) */
    for (NativeModule *n = native_registry; n != NULL; n = n->next) {
        if (strcmp(n->name, spec) == 0 || strcmp(n->name, normalized_id) == 0) {
            LoadedModule *lm = find_loaded(n->name);
            RuntimeValue exports = lm ? lm->exports : load_native_with_init(n->name, n->init_fn);
            import_into_env(requester_env, exports.data.obj_env);
            char alias_buf[PATH_MAX];
            if (alias == NULL) {
                module_guess_alias(spec, alias_buf, sizeof(alias_buf));
                alias = alias_buf;
            }
            if (!alias_already_bound(requester_env, alias, exports)) {
                env_set_fast(requester_env, alias, get_hash(alias), exports);
            }
            return exports;
        }
    }

    /* 1b. Built-in descriptor table */
    const ModuleDescriptor *desc = std_registry_lookup(normalized_id);

    if (desc) {
        if (desc->kind == MODULE_KIND_NATIVE) {
            LoadedModule *lm = find_loaded(desc->id);
            RuntimeValue exports = lm ? lm->exports : load_native_with_init(desc->id, desc->native_init);
            import_into_env(requester_env, exports.data.obj_env);
            char alias_buf[PATH_MAX];
            if (alias == NULL) {
                module_guess_alias(desc->id, alias_buf, sizeof(alias_buf));
                alias = alias_buf;
            }
            if (!alias_already_bound(requester_env, alias, exports)) {
                env_set_fast(requester_env, alias, get_hash(alias), exports);
            }
            return exports;
        } else { /* MODULE_KIND_FILE */
            char abs_path[PATH_MAX];
            if (!make_absolute_path(desc->file_path, abs_path)) {
                runtime_error("Cannot resolve module file '%s' for '%s'", desc->file_path, desc->id);
            }
            LoadedModule *lm = find_loaded(abs_path);
            RuntimeValue exports = lm ? lm->exports : load_script(abs_path);
            import_into_env(requester_env, exports.data.obj_env);
            char alias_buf[PATH_MAX];
            if (alias == NULL) {
                module_guess_alias(desc->id, alias_buf, sizeof(alias_buf));
                alias = alias_buf;
            }
            if (!alias_already_bound(requester_env, alias, exports)) {
                env_set_fast(requester_env, alias, get_hash(alias), exports);
            }
            return exports;
        }
    }

    /* 2. Resolve user script path */
    if (is_legacy_path) {
        if (!make_absolute_path(spec, key)) {
            runtime_error("Cannot resolve path '%s'", spec);
        }
    } else {
        char pathbuf[PATH_MAX];
        if (!make_module_id_path(spec, pathbuf)) {
            runtime_error("Invalid module id '%s'", spec);
        }
        if (!make_absolute_path(pathbuf, key)) {
            runtime_error("Cannot resolve module '%s' (path '%s')", spec, pathbuf);
        }
    }

    LoadedModule *lm = find_loaded(key);
    RuntimeValue exports = lm ? lm->exports : load_script(key);

    import_into_env(requester_env, exports.data.obj_env);

    char alias_buf[PATH_MAX];
    if (alias == NULL) {
        module_guess_alias(spec, alias_buf, sizeof(alias_buf));
        alias = alias_buf;
    }
    if (!alias_already_bound(requester_env, alias, exports)) {
        env_set_fast(requester_env, alias, get_hash(alias), exports);
    }
    return exports;
}

void module_guess_alias(const char *spec, char *out, size_t out_size) {
    const char *slash = strrchr(spec, '/');
    const char *base = slash ? slash + 1 : spec;

    char tmp[PATH_MAX];
    append_ext_if_missing(base, tmp, sizeof(tmp));
    char *ext = strrchr(tmp, '.');
    if (ext) *ext = '\0';
    strncpy(out, tmp, out_size - 1);
    out[out_size - 1] = '\0';
}
