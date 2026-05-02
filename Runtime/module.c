#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "module.h"
#include "module_toml.h"
#include "../mks_module.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <stdint.h>
#include <libgen.h>
#include <unistd.h>
#include <sys/stat.h>
#include <setjmp.h>

#ifdef __unix__
#include <dlfcn.h>
#endif

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#ifndef MKS_INSTALL_BINDIR_REL
#define MKS_INSTALL_BINDIR_REL "bin"
#endif

#ifndef MKS_INSTALL_STDLIB_DIR_REL
#define MKS_INSTALL_STDLIB_DIR_REL "share/mks"
#endif

#ifndef MKS_INSTALL_STDLIB_DIR_ABS
#define MKS_INSTALL_STDLIB_DIR_ABS "/usr/local/share/mks"
#endif

#include "errors.h"
#include "context.h"
#include "../Lexer/lexer.h"
#include "../Parser/parser.h"
#include "../Eval/eval.h"
#include "../GC/gc.h"
#include "../VM/vm.h"
#include "../Utils/file.h"
#include "../Utils/hash.h"
#include "../std/registry.h"

typedef struct NativeModule {
    char *name;
    NativeModuleInit init_fn;
    struct NativeModule *next;
} NativeModule;

#define MODULE_CACHE_SLOTS 128

typedef struct {
    char *key;          /* NULL = empty slot */
    RuntimeValue exports;
    Environment *env;
    int loading;
    int failed;         /* set if module eval panicked — retry on next import */
} ModuleCacheEntry;

typedef struct {
    ModuleCacheEntry *slots;  /* heap-allocated, variable size */
    int capacity;             /* current size (power of 2) */
    int count;                /* occupied slots */
} ModuleCache;

typedef struct LoadedModule {
    char *key;          /* abs path for .mks or name for native */
    RuntimeValue exports;
    Environment *env;
    int loading;
    struct LoadedModule *next;  /* legacy - kept for ABI compat */
} LoadedModule;

typedef struct ProgramNode {
    ASTNode *program;
    struct ProgramNode *next;
} ProgramNode;

typedef struct PackageManifest {
    char name[PATH_MAX];
    char main_path[PATH_MAX];
    char lib_path[PATH_MAX];
    int has_name;
    int has_main;
    int has_lib;
    int is_legacy_toml;
} PackageManifest;

static NativeModule **native_registry_slot(void) {
    return (NativeModule **)&mks_context_current()->module_native_registry;
}

static LoadedModule **loaded_modules_slot(void) {
    return (LoadedModule **)&mks_context_current()->module_loaded_modules;
}

static ProgramNode **programs_slot(void) {
    return (ProgramNode **)&mks_context_current()->module_programs;
}

#define native_registry (*native_registry_slot())
#define loaded_modules (*loaded_modules_slot())
#define programs (*programs_slot())
#define modules_parent_env (mks_context_current()->module_parent_env)

static ModuleDescriptor builtin_modules[] = {
    { "std.math",   MODULE_KIND_NATIVE, NULL, NULL },   /* native init assigned in module_system_init */
    { "std.time",   MODULE_KIND_NATIVE, NULL, NULL },
    { "std.random", MODULE_KIND_NATIVE, NULL, NULL },
    { "std.watch",  MODULE_KIND_NATIVE, NULL, NULL },
    { "std.fs",     MODULE_KIND_NATIVE, NULL, NULL },
    { "std.json",   MODULE_KIND_NATIVE, NULL, NULL },
    { "std.path",   MODULE_KIND_NATIVE, NULL, NULL },
    { "std.process",MODULE_KIND_NATIVE, NULL, NULL },
    { "std.io",     MODULE_KIND_NATIVE, NULL, NULL },
    { "std.string", MODULE_KIND_FILE,   NULL, "std/string.mks" },
    { "std.array",  MODULE_KIND_FILE,   NULL, "std/array.mks" },
    { "std",        MODULE_KIND_FILE,   NULL, "std/std.mks" },
    { NULL, MODULE_KIND_NATIVE, NULL, NULL }
};

static int try_realpath(const char *candidate, char *out);
static char *read_module_source(const char *path);
static RuntimeValue load_script(const char *abs_path);
static LoadedModule *find_loaded(const char *key);
static LoadedModule *remember_loaded(const char *key, RuntimeValue exports, Environment *env, int loading);
static int make_module_id_path(const char *module_id, char *out);
static int make_builtin_module_path(const char *path, char *out);
static void normalize_spec_to_id(const char *spec, char *out, size_t out_size);
static int bind_module_alias(Environment *env, const char *alias, RuntimeValue exports);
static int path_exists(const char *path);
static int path_is_regular_file(const char *path);
static int has_suffix(const char *text, const char *suffix);
static int mkspkg_entry_exists(const char *archive_path, const char *entry_path);
static int split_virtual_path(const char *path, char *archive_out, size_t archive_size, const char **entry_out);
static int resolve_virtual_relative_path(const char *current_file, const char *path, char *out);
static int path_dirname(const char *path, char *out, size_t out_size);
static int find_package_root_for(const char *file_path, char *out, size_t out_size);
static int read_package_manifest(const char *package_root, PackageManifest *manifest);
static int resolve_package_import(const char *spec, char *out, size_t out_size);
static int trim_path_components(char *path, const char *relative_path);
static int try_exec_relative_std_path(const char *exec_dir, const char *module_path, char *out);
static RuntimeValue eval_module_program(const ASTNode *program, Environment *module_env);

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

static ModuleCache *get_module_cache(void) {
    void **cache_ptr = (void **)&mks_context_current()->module_loaded_modules;
    if (*cache_ptr == NULL) {
        ModuleCache *cache = (ModuleCache *)malloc(sizeof(ModuleCache));
        if (!cache) runtime_error("Out of memory for module cache");
        cache->capacity = MODULE_CACHE_SLOTS;
        cache->count = 0;
        cache->slots = (ModuleCacheEntry *)malloc(sizeof(ModuleCacheEntry) * (size_t)cache->capacity);
        if (!cache->slots) {
            free(cache);
            runtime_error("Out of memory for module cache slots");
        }
        memset(cache->slots, 0, sizeof(ModuleCacheEntry) * (size_t)cache->capacity);
        *cache_ptr = cache;
    }
    return (ModuleCache *)*cache_ptr;
}

static LoadedModule *find_loaded(const char *key) {
    ModuleCache *cache = get_module_cache();
    unsigned int h = get_hash(key) & ((unsigned int)cache->capacity - 1);

    for (int i = 0; i < cache->capacity; i++) {
        unsigned int slot = (h + (unsigned int)i) & ((unsigned int)cache->capacity - 1);
        if (cache->slots[slot].key == NULL) {
            return NULL;  /* empty slot, key not found */
        }
        if (strcmp(cache->slots[slot].key, key) == 0) {
            if (cache->slots[slot].failed) {
                return NULL;  /* failed load; treat as not found to retry */
            }
            return (LoadedModule *)&cache->slots[slot];
        }
    }
    return NULL;
}

static void cache_resize(ModuleCache *cache) {
    int new_capacity = cache->capacity * 2;
    ModuleCacheEntry *new_slots = (ModuleCacheEntry *)malloc(sizeof(ModuleCacheEntry) * (size_t)new_capacity);
    if (!new_slots) {
        runtime_error("Out of memory resizing module cache");
    }
    memset(new_slots, 0, sizeof(ModuleCacheEntry) * (size_t)new_capacity);

    /* Rehash all non-null non-failed entries into new table */
    for (int i = 0; i < cache->capacity; i++) {
        if (cache->slots[i].key != NULL && !cache->slots[i].failed) {
            unsigned int h = get_hash(cache->slots[i].key) & ((unsigned int)new_capacity - 1);
            for (int j = 0; j < new_capacity; j++) {
                unsigned int slot = (h + (unsigned int)j) & ((unsigned int)new_capacity - 1);
                if (new_slots[slot].key == NULL) {
                    new_slots[slot] = cache->slots[i];
                    break;
                }
            }
        }
    }

    free(cache->slots);
    cache->slots = new_slots;
    cache->capacity = new_capacity;
}

static LoadedModule *remember_loaded(const char *key, RuntimeValue exports, Environment *env, int loading) {
    ModuleCache *cache = get_module_cache();

    /* Check load factor and resize if necessary (at 70% capacity) */
    if (cache->count >= cache->capacity * 7 / 10) {
        cache_resize(cache);
    }

    unsigned int h = get_hash(key) & ((unsigned int)cache->capacity - 1);

    for (int i = 0; i < cache->capacity; i++) {
        unsigned int slot = (h + (unsigned int)i) & ((unsigned int)cache->capacity - 1);
        if (cache->slots[slot].key == NULL ||
            (strcmp(cache->slots[slot].key, key) == 0 && cache->slots[slot].failed)) {
            /* New empty slot, or reuse a failed slot with same key */
            if (cache->slots[slot].key == NULL) {
                cache->slots[slot].key = strdup(key);
                if (!cache->slots[slot].key) runtime_error("Out of memory for module cache key");
                cache->count++;
            }
            cache->slots[slot].exports = exports;
            cache->slots[slot].env = env;
            cache->slots[slot].loading = loading;
            cache->slots[slot].failed = 0;
            gc_pin_env(env);
            return (LoadedModule *)&cache->slots[slot];
        }
    }

    runtime_error("Module cache overflow (too many loaded modules)");
    return NULL;
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

    /* Free hash table cache */
    void **cache_ptr = (void **)&loaded_modules;
    if (*cache_ptr != NULL) {
        ModuleCache *cache = (ModuleCache *)*cache_ptr;
        for (int i = 0; i < cache->capacity; i++) {
            if (cache->slots[i].key != NULL) {
                if (cache->slots[i].env != NULL) {
                    gc_unpin_env(cache->slots[i].env);
                }
                free(cache->slots[i].key);
            }
        }
        free(cache->slots);
        free(cache);
        *cache_ptr = NULL;
    }

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

static int trim_path_components(char *path, const char *relative_path) {
    if (path == NULL || relative_path == NULL) {
        return 0;
    }

    int components = 0;
    int in_component = 0;
    for (const char *p = relative_path; *p != '\0'; p++) {
        if (*p == '/') {
            in_component = 0;
            continue;
        }
        if (!in_component) {
            components++;
            in_component = 1;
        }
    }

    for (int i = 0; i < components; i++) {
        char parent[PATH_MAX];
        if (!path_dirname(path, parent, sizeof(parent))) {
            return 0;
        }
        if (strcmp(parent, path) == 0) {
            return 0;
        }
        strcpy(path, parent);
    }

    return 1;
}

static int try_exec_relative_std_path(const char *exec_dir, const char *module_path, char *out) {
    if (exec_dir == NULL || exec_dir[0] == '\0' || module_path == NULL || out == NULL) {
        return 0;
    }

    char prefix[PATH_MAX];
    strncpy(prefix, exec_dir, sizeof(prefix) - 1);
    prefix[sizeof(prefix) - 1] = '\0';

    if (!trim_path_components(prefix, MKS_INSTALL_BINDIR_REL)) {
        return 0;
    }

    char candidate[PATH_MAX];
    if (snprintf(candidate, sizeof(candidate), "%s/%s/%s", prefix, MKS_INSTALL_STDLIB_DIR_REL, module_path) >= (int)sizeof(candidate)) {
        return 0;
    }

    return try_realpath(candidate, out);
}

static int make_absolute_path(const char *path, char *out) {
    if (path == NULL) return 0;

    char with_ext[PATH_MAX];
    append_ext_if_missing(path, with_ext, sizeof(with_ext));

    if (try_realpath(path, out) || try_realpath(with_ext, out)) return 1;

    const char *current_file = runtime_current_file();
    if (current_file != NULL) {
        if ((strncmp(path, "./", 2) == 0 || strncmp(path, "../", 3) == 0) &&
            resolve_virtual_relative_path(current_file, path, out)) {
            return 1;
        }

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
        if (try_exec_relative_std_path(base, with_ext, out)) return 1;
    }

    const char *env_std = getenv("MKS_STD_PATH");
    if (env_std && env_std[0]) {
        char tmp_out[PATH_MAX];
        if (snprintf(tmp_out, PATH_MAX, "%s/%s", env_std, with_ext) < PATH_MAX &&
            try_realpath(tmp_out, out)) return 1;
    }

    {
        char tmp_out[PATH_MAX];
        if (snprintf(tmp_out, sizeof(tmp_out), "%s/%s", MKS_INSTALL_STDLIB_DIR_ABS, with_ext) < (int)sizeof(tmp_out) &&
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

static int try_path_or_dir(const char *base, const char *file_rel, char *out) {
    char p[PATH_MAX];
    if (snprintf(p, sizeof(p), "%s/%s", base, file_rel) < (int)sizeof(p)) {
        if (try_realpath(p, out)) return 1;
    }
    /* Also try directory form: if file_rel is "std/X.mks", try "std/X/mod.mks" */
    char mod_rel[PATH_MAX];
    snprintf(mod_rel, sizeof(mod_rel), "%s", file_rel);
    char *dot = strrchr(mod_rel, '.');
    if (dot && strcmp(dot, ".mks") == 0) {
        *dot = '\0';
        if (snprintf(p, sizeof(p), "%s/%s/mod.mks", base, mod_rel) < (int)sizeof(p)) {
            if (try_realpath(p, out)) return 1;
        }
    }
    return 0;
}

static int make_builtin_module_path(const char *path, char *out) {
    if (path == NULL) return 0;

    char with_ext[PATH_MAX];
    if (!append_ext_if_missing(path, with_ext, sizeof(with_ext))) {
        return 0;
    }

    const char *base = get_exec_dir();
    if (base && base[0]) {
        if (try_exec_relative_std_path(base, with_ext, out)) return 1;

        if (try_path_or_dir(base, with_ext, out)) return 1;

        char parent[PATH_MAX];
        if (snprintf(parent, PATH_MAX, "%s/..", base) < PATH_MAX) {
            if (try_path_or_dir(parent, with_ext, out)) return 1;
        }
    }

    const char *env_std = getenv("MKS_STD_PATH");
    if (env_std && env_std[0]) {
        if (try_path_or_dir(env_std, with_ext, out)) return 1;
    }

    if (try_path_or_dir(MKS_INSTALL_STDLIB_DIR_ABS, with_ext, out)) return 1;

    static const char *system_std_paths[] = {"/usr/local/share/mks", "/usr/share/mks", NULL};
    for (int i = 0; system_std_paths[i] != NULL; i++) {
        if (try_path_or_dir(system_std_paths[i], with_ext, out)) return 1;
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

static int bind_module_alias(Environment *env, const char *alias, RuntimeValue exports) {
    RuntimeValue existing;
    if (env_try_get(env, alias, get_hash(alias), &existing)) {
        if ((existing.type == VAL_OBJECT || existing.type == VAL_MODULE) &&
            existing.data.obj_env == exports.data.obj_env) {
            return 1;
        }
        runtime_error("Import alias '%s' is already bound in this scope", alias);
    }

    env_set_fast(env, alias, get_hash(alias), exports);
    return 1;
}

static int path_exists(const char *path) {
    struct stat st;
    return path != NULL && stat(path, &st) == 0;
}

static int path_is_regular_file(const char *path) {
    struct stat st;
    return path != NULL && stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static int path_is_directory(const char *path) {
    struct stat st;
    return path != NULL && stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static int has_suffix(const char *text, const char *suffix) {
    size_t text_len = strlen(text);
    size_t suffix_len = strlen(suffix);
    return text_len >= suffix_len && strcmp(text + text_len - suffix_len, suffix) == 0;
}

static int read_u32_le(FILE *f, uint32_t *out) {
    unsigned char b[4];
    if (fread(b, 1, sizeof(b), f) != sizeof(b)) {
        return 0;
    }
    *out = ((uint32_t)b[0]) |
           ((uint32_t)b[1] << 8) |
           ((uint32_t)b[2] << 16) |
           ((uint32_t)b[3] << 24);
    return 1;
}

static int read_u64_le(FILE *f, uint64_t *out) {
    unsigned char b[8];
    if (fread(b, 1, sizeof(b), f) != sizeof(b)) {
        return 0;
    }
    *out = ((uint64_t)b[0]) |
           ((uint64_t)b[1] << 8) |
           ((uint64_t)b[2] << 16) |
           ((uint64_t)b[3] << 24) |
           ((uint64_t)b[4] << 32) |
           ((uint64_t)b[5] << 40) |
           ((uint64_t)b[6] << 48) |
           ((uint64_t)b[7] << 56);
    return 1;
}

static char *read_mkspkg_entry(const char *archive_path, const char *entry_path) {
    FILE *f = fopen(archive_path, "rb");
    if (f == NULL) {
        return NULL;
    }

    char magic[8];
    if (fread(magic, 1, sizeof(magic), f) != sizeof(magic) ||
        memcmp(magic, "MKSPKG1\n", sizeof(magic)) != 0) {
        fclose(f);
        return NULL;
    }

    while (1) {
        uint32_t path_len = 0;
        uint64_t size = 0;
        if (!read_u32_le(f, &path_len)) {
            break;
        }
        if (path_len == 0) {
            fclose(f);
            return NULL;
        }
        if (path_len >= PATH_MAX || !read_u64_le(f, &size)) {
            break;
        }

        char path[PATH_MAX];
        if (fread(path, 1, path_len, f) != path_len) {
            break;
        }
        path[path_len] = '\0';

        if (strcmp(path, entry_path) == 0) {
            if (size > SIZE_MAX - 1) {
                break;
            }
            char *data = (char *)malloc((size_t)size + 1);
            if (data == NULL) {
                fclose(f);
                runtime_error("Out of memory reading package archive entry");
            }
            if (fread(data, 1, (size_t)size, f) != (size_t)size) {
                free(data);
                break;
            }
            data[size] = '\0';
            fclose(f);
            return data;
        }

        if (size > LONG_MAX || fseek(f, (long)size, SEEK_CUR) != 0) {
            break;
        }
    }

    fclose(f);
    return NULL;
}

static int mkspkg_entry_exists(const char *archive_path, const char *entry_path) {
    char *entry = read_mkspkg_entry(archive_path, entry_path);
    if (entry == NULL) {
        return 0;
    }
    free(entry);
    return 1;
}

static int split_virtual_path(const char *path, char *archive_out, size_t archive_size, const char **entry_out) {
    const char *sep = strstr(path, "::");
    if (sep == NULL) {
        return 0;
    }
    size_t archive_len = (size_t)(sep - path);
    if (archive_len == 0 || archive_len >= archive_size) {
        return 0;
    }
    memcpy(archive_out, path, archive_len);
    archive_out[archive_len] = '\0';
    *entry_out = sep + 2;
    return **entry_out != '\0';
}

static int normalize_virtual_entry_path(const char *base_entry, const char *relative_path, char *out) {
    char with_ext[PATH_MAX];
    if (has_suffix(relative_path, ".mks")) {
        if (snprintf(with_ext, sizeof(with_ext), "%s", relative_path) >= (int)sizeof(with_ext)) {
            return 0;
        }
    } else if (snprintf(with_ext, sizeof(with_ext), "%s.mks", relative_path) >= (int)sizeof(with_ext)) {
        return 0;
    }

    char base_dir[PATH_MAX] = "";
    const char *slash = strrchr(base_entry, '/');
    if (slash != NULL) {
        size_t len = (size_t)(slash - base_entry);
        if (len >= sizeof(base_dir)) {
            return 0;
        }
        memcpy(base_dir, base_entry, len);
        base_dir[len] = '\0';
    }

    char combined[PATH_MAX];
    if (base_dir[0] != '\0') {
        if (snprintf(combined, sizeof(combined), "%s/%s", base_dir, with_ext) >= (int)sizeof(combined)) {
            return 0;
        }
    } else if (snprintf(combined, sizeof(combined), "%s", with_ext) >= (int)sizeof(combined)) {
        return 0;
    }

    char scratch[PATH_MAX];
    strncpy(scratch, combined, sizeof(scratch) - 1);
    scratch[sizeof(scratch) - 1] = '\0';

    char *parts[128];
    int part_count = 0;
    char *saveptr = NULL;
    char *part = strtok_r(scratch, "/", &saveptr);
    while (part != NULL) {
        if (strcmp(part, ".") == 0 || part[0] == '\0') {
            /* skip */
        } else if (strcmp(part, "..") == 0) {
            if (part_count == 0) {
                return 0;
            }
            part_count--;
        } else {
            if (part_count >= (int)(sizeof(parts) / sizeof(parts[0]))) {
                return 0;
            }
            parts[part_count++] = part;
        }
        part = strtok_r(NULL, "/", &saveptr);
    }

    size_t used = 0;
    out[0] = '\0';
    for (int i = 0; i < part_count; i++) {
        size_t len = strlen(parts[i]);
        if (used + len + (i > 0 ? 1 : 0) + 1 > PATH_MAX) {
            return 0;
        }
        if (i > 0) {
            out[used++] = '/';
        }
        memcpy(out + used, parts[i], len);
        used += len;
        out[used] = '\0';
    }

    return used > 0;
}

static int resolve_virtual_relative_path(const char *current_file, const char *path, char *out) {
    char archive_path[PATH_MAX];
    const char *entry_path = NULL;
    if (!split_virtual_path(current_file, archive_path, sizeof(archive_path), &entry_path)) {
        return 0;
    }

    char normalized_entry[PATH_MAX];
    if (!normalize_virtual_entry_path(entry_path, path, normalized_entry)) {
        return 0;
    }
    if (!mkspkg_entry_exists(archive_path, normalized_entry)) {
        return 0;
    }
    return snprintf(out, PATH_MAX, "%s::%s", archive_path, normalized_entry) < PATH_MAX;
}

static char *read_module_source(const char *path) {
    char archive_path[PATH_MAX];
    const char *entry_path = NULL;
    if (split_virtual_path(path, archive_path, sizeof(archive_path), &entry_path)) {
        char *source = read_mkspkg_entry(archive_path, entry_path);
        if (source == NULL) {
            runtime_error("Could not read package archive entry '%s' from '%s'", entry_path, archive_path);
        }
        return source;
    }
    return mks_read_file(path);
}

static int path_dirname(const char *path, char *out, size_t out_size) {
    if (path == NULL || out == NULL || out_size == 0) {
        return 0;
    }

    const char *slash = strrchr(path, '/');
    if (slash == NULL) {
        if (out_size < 2) {
            return 0;
        }
        out[0] = '.';
        out[1] = '\0';
        return 1;
    }

    size_t len = (size_t)(slash - path);
    if (len == 0) {
        len = 1;
    }
    if (len >= out_size) {
        return 0;
    }

    memcpy(out, path, len);
    out[len] = '\0';
    return 1;
}

static int find_package_root_for(const char *file_path, char *out, size_t out_size) {
    char dir[PATH_MAX];
    if (!path_dirname(file_path, dir, sizeof(dir))) {
        return 0;
    }

    while (1) {
        char manifest[PATH_MAX];
        if (snprintf(manifest, sizeof(manifest), "%s/package.pkg", dir) < (int)sizeof(manifest) &&
            path_exists(manifest)) {
            if (strlen(dir) + 1 > out_size) {
                return 0;
            }
            strcpy(out, dir);
            return 1;
        }
        if (snprintf(manifest, sizeof(manifest), "%s/mks.toml", dir) < (int)sizeof(manifest) &&
            path_exists(manifest)) {
            if (strlen(dir) + 1 > out_size) {
                return 0;
            }
            strcpy(out, dir);
            return 1;
        }

        if (strcmp(dir, "/") == 0 || strcmp(dir, ".") == 0) {
            break;
        }

        char parent[PATH_MAX];
        if (!path_dirname(dir, parent, sizeof(parent))) {
            break;
        }
        if (strcmp(parent, dir) == 0) {
            break;
        }
        strcpy(dir, parent);
    }

    return 0;
}

static void package_manifest_init(PackageManifest *manifest) {
    memset(manifest, 0, sizeof(*manifest));
}

static int read_quoted_field(const char *line, const char *field, char *out, size_t out_size) {
    size_t field_len = strlen(field);
    if (strncmp(line, field, field_len) != 0) {
        return 0;
    }

    char next = line[field_len];
    if (next != '\0' && next != ' ' && next != '\t' && next != '=') {
        return 0;
    }

    const char *quote1 = strchr(line + field_len, '"');
    const char *quote2 = quote1 != NULL ? strchr(quote1 + 1, '"') : NULL;
    if (quote1 == NULL || quote2 == NULL || quote2 <= quote1 + 1) {
        return 0;
    }

    size_t len = (size_t)(quote2 - quote1 - 1);
    if (len >= out_size) {
        len = out_size - 1;
    }
    memcpy(out, quote1 + 1, len);
    out[len] = '\0';
    return 1;
}

static void parse_manifest_source(char *source, PackageManifest *manifest, int is_legacy_toml) {
    manifest->is_legacy_toml = is_legacy_toml;

    char *cursor = source;
    while (*cursor != '\0') {
        char *line_end = strchr(cursor, '\n');
        if (line_end != NULL) {
            *line_end = '\0';
        }

        while (*cursor == ' ' || *cursor == '\t') {
            cursor++;
        }

        if (*cursor == '#' || *cursor == '\0') {
            /* ignore comment/empty lines */
        } else if (!is_legacy_toml && read_quoted_field(cursor, "package", manifest->name, sizeof(manifest->name))) {
            manifest->has_name = 1;
        } else if (read_quoted_field(cursor, "name", manifest->name, sizeof(manifest->name))) {
            manifest->has_name = 1;
        } else if (read_quoted_field(cursor, "main", manifest->main_path, sizeof(manifest->main_path))) {
            manifest->has_main = 1;
        } else if (read_quoted_field(cursor, "entry", manifest->main_path, sizeof(manifest->main_path))) {
            manifest->has_main = 1;
        } else if (read_quoted_field(cursor, "lib", manifest->lib_path, sizeof(manifest->lib_path))) {
            manifest->has_lib = 1;
        }

        if (line_end == NULL) {
            break;
        }
        cursor = line_end + 1;
    }
}

static int read_package_manifest(const char *package_root, PackageManifest *manifest) {
    package_manifest_init(manifest);

    if (has_suffix(package_root, ".mkspkg") && path_is_regular_file(package_root)) {
        char *source = read_mkspkg_entry(package_root, "package.pkg");
        if (source != NULL) {
            parse_manifest_source(source, manifest, 0);
            free(source);
            return manifest->has_name;
        }
    }

    char manifest_path[PATH_MAX];
    if (snprintf(manifest_path, sizeof(manifest_path), "%s/package.pkg", package_root) < (int)sizeof(manifest_path) &&
        path_exists(manifest_path)) {
        char *source = mks_read_file(manifest_path);
        if (source != NULL) {
            parse_manifest_source(source, manifest, 0);
            free(source);
            return manifest->has_name;
        }
    }

    if (snprintf(manifest_path, sizeof(manifest_path), "%s/mks.toml", package_root) < (int)sizeof(manifest_path) &&
        path_exists(manifest_path)) {
        char *source = mks_read_file(manifest_path);
        if (source != NULL) {
            parse_manifest_source(source, manifest, 1);
            free(source);
            return manifest->has_name;
        }
    }

    return 0;
}

static int try_package_file(const char *package_root, const char *relative_path, char *out) {
    if (has_suffix(package_root, ".mkspkg") && path_is_regular_file(package_root)) {
        if (!mkspkg_entry_exists(package_root, relative_path)) {
            return 0;
        }
        return snprintf(out, PATH_MAX, "%s::%s", package_root, relative_path) < PATH_MAX;
    }

    char candidate[PATH_MAX];
    if (snprintf(candidate, sizeof(candidate), "%s/%s", package_root, relative_path) >= (int)sizeof(candidate)) {
        return 0;
    }
    return try_realpath(candidate, out);
}

static int resolve_package_root_file(const char *package_root, const PackageManifest *manifest, char *out) {
    if (manifest->has_lib && try_package_file(package_root, manifest->lib_path, out)) {
        return 1;
    }
    if (!manifest->is_legacy_toml && try_package_file(package_root, "src/lib.mks", out)) {
        return 1;
    }
    if (manifest->has_main && try_package_file(package_root, manifest->main_path, out)) {
        return 1;
    }
    return try_package_file(package_root, "src/main.mks", out);
}

static int resolve_package_submodule_file(const char *package_root, const char *suffix, char *out) {
    char module_suffix[PATH_MAX];
    if (!make_module_id_path(suffix, module_suffix)) {
        return 0;
    }

    char relative_path[PATH_MAX];
    if (snprintf(relative_path, sizeof(relative_path), "src/%s", module_suffix) >= (int)sizeof(relative_path)) {
        return 0;
    }
    return try_package_file(package_root, relative_path, out);
}

static int package_consumer_root(const char *package_root, char *out, size_t out_size) {
    char modules_dir[PATH_MAX];
    if (!path_dirname(package_root, modules_dir, sizeof(modules_dir))) {
        return 0;
    }

    const char *base = strrchr(modules_dir, '/');
    base = base != NULL ? base + 1 : modules_dir;
    if (strcmp(base, "mks_modules") != 0) {
        return 0;
    }

    char consumer_root[PATH_MAX];
    if (!path_dirname(modules_dir, consumer_root, sizeof(consumer_root))) {
        return 0;
    }
    if (strlen(consumer_root) + 1 > out_size) {
        return 0;
    }
    strcpy(out, consumer_root);
    return 1;
}

static int try_external_package_from_root(const char *root, const char *package_name, PackageManifest *manifest, char *dep_root) {
    int found_dep = 0;

    if (snprintf(dep_root, PATH_MAX, "%s/%s.mkspkg", root, package_name) < PATH_MAX &&
        read_package_manifest(dep_root, manifest)) {
        if (!manifest->has_name || strcmp(manifest->name, package_name) == 0) {
            found_dep = 1;
        }
    }

    if (!found_dep &&
        snprintf(dep_root, PATH_MAX, "%s/%s", root, package_name) < PATH_MAX &&
        read_package_manifest(dep_root, manifest)) {
        if (!manifest->has_name || strcmp(manifest->name, package_name) == 0) {
            found_dep = 1;
        }
    }

    if (!found_dep &&
        snprintf(dep_root, PATH_MAX, "%s/mks_modules/%s.mkspkg", root, package_name) < PATH_MAX &&
        read_package_manifest(dep_root, manifest)) {
        if (!manifest->has_name || strcmp(manifest->name, package_name) == 0) {
            found_dep = 1;
        }
    }

    if (!found_dep &&
        snprintf(dep_root, PATH_MAX, "%s/mks_modules/%s", root, package_name) < PATH_MAX &&
        read_package_manifest(dep_root, manifest)) {
        if (!manifest->has_name || strcmp(manifest->name, package_name) == 0) {
            found_dep = 1;
        }
    }

    if (!found_dep &&
        snprintf(dep_root, PATH_MAX, "%s/packages/%s", root, package_name) < PATH_MAX &&
        read_package_manifest(dep_root, manifest)) {
        if (!manifest->has_name || strcmp(manifest->name, package_name) == 0) {
            found_dep = 1;
        }
    }

    return found_dep;
}

static int resolve_package_import(const char *spec, char *out, size_t out_size) {
    (void)out_size;

    const char *current_file = runtime_current_file();
    if (current_file == NULL) {
        return 0;
    }

    char current_real[PATH_MAX];
    if (!strstr(current_file, "::") && try_realpath(current_file, current_real)) {
        current_file = current_real;
    }

    char package_root[PATH_MAX];
    const char *virtual_entry = NULL;
    if (split_virtual_path(current_file, package_root, sizeof(package_root), &virtual_entry)) {
        /* archive path is the package root */
    } else if (!find_package_root_for(current_file, package_root, sizeof(package_root))) {
        if (!path_dirname(current_file, package_root, sizeof(package_root))) {
            return 0;
        }
    }

    PackageManifest manifest;
    int has_manifest = read_package_manifest(package_root, &manifest);

    // Try mks.toml if package.pkg not found (0.2 compatibility)
    if (!has_manifest) {
        PackageToml toml;
        if (package_toml_read(package_root, &toml)) {
            strcpy(manifest.name, toml.name);
            if (toml.entry[0] != '\0') {
                strcpy(manifest.lib_path, toml.entry);
                manifest.has_lib = 1;
            } else if (toml.main[0] != '\0') {
                strcpy(manifest.lib_path, toml.main);
                manifest.has_lib = 1;
            }
            manifest.has_name = 1;
            has_manifest = 1;
            package_toml_free(&toml);
        }
    }

    char external_roots[3][PATH_MAX];
    int external_root_count = 0;

    if (virtual_entry != NULL && path_is_regular_file(package_root)) {
        if (path_dirname(package_root, external_roots[external_root_count], PATH_MAX)) {
            external_root_count++;
        }
    } else {
        snprintf(external_roots[external_root_count], PATH_MAX, "%s", package_root);
        external_root_count++;
    }

    char consumer_root[PATH_MAX];
    if (package_consumer_root(package_root, consumer_root, sizeof(consumer_root)) &&
        strcmp(consumer_root, package_root) != 0) {
        int duplicate = 0;
        for (int i = 0; i < external_root_count; i++) {
            if (strcmp(external_roots[i], consumer_root) == 0) {
                duplicate = 1;
                break;
            }
        }
        if (!duplicate && external_root_count < (int)(sizeof(external_roots) / sizeof(external_roots[0]))) {
            snprintf(external_roots[external_root_count], PATH_MAX, "%s", consumer_root);
            external_root_count++;
        }
    }

    const char *suffix = NULL;
    if (has_manifest && manifest.has_name) {
        const size_t pkg_len = strlen(manifest.name);
        if (strcmp(spec, manifest.name) == 0) {
            suffix = "";
        } else if (strncmp(spec, manifest.name, pkg_len) == 0 && spec[pkg_len] == '.') {
            suffix = spec + pkg_len + 1;
        }
    }

    if (suffix != NULL) {
        if (suffix[0] == '\0') {
            return resolve_package_root_file(package_root, &manifest, out);
        }
        return resolve_package_submodule_file(package_root, suffix, out);
    }

    char spec_copy[PATH_MAX];
    size_t spec_len = strlen(spec);
    if (spec_len >= sizeof(spec_copy)) {
        return 0;
    }
    memcpy(spec_copy, spec, spec_len + 1);

    char *segments[64];
    int segment_count = 0;
    char *saveptr = NULL;
    char *part = strtok_r(spec_copy, ".", &saveptr);
    while (part != NULL && segment_count < (int)(sizeof(segments) / sizeof(segments[0]))) {
        segments[segment_count++] = part;
        part = strtok_r(NULL, ".", &saveptr);
    }

    for (int prefix_len = segment_count; prefix_len >= 1; prefix_len--) {
        char package_name[PATH_MAX] = "";
        size_t used = 0;
        for (int i = 0; i < prefix_len; i++) {
            size_t seg_len = strlen(segments[i]);
            if (used + seg_len + 2 >= sizeof(package_name)) {
                used = 0;
                break;
            }
            if (i > 0) {
                package_name[used++] = '.';
            }
            memcpy(package_name + used, segments[i], seg_len);
            used += seg_len;
            package_name[used] = '\0';
        }
        if (used == 0) {
            continue;
        }

        PackageManifest dep_manifest;
        char dep_root[PATH_MAX];
        int found_dep = 0;

        for (int root_i = 0; root_i < external_root_count; root_i++) {
            if (try_external_package_from_root(external_roots[root_i], package_name, &dep_manifest, dep_root)) {
                found_dep = 1;
                break;
            }
        }

        if (!found_dep) {
            continue;
        }

        if (prefix_len == segment_count) {
            if (resolve_package_root_file(dep_root, &dep_manifest, out)) {
                return 1;
            }
        } else {
            char module_name[PATH_MAX] = "";
            size_t module_used = 0;
            for (int i = prefix_len; i < segment_count; i++) {
                size_t seg_len = strlen(segments[i]);
                if (module_used + seg_len + 2 >= sizeof(module_name)) {
                    module_used = 0;
                    break;
                }
                if (i > prefix_len) {
                    module_name[module_used++] = '.';
                }
                memcpy(module_name + module_used, segments[i], seg_len);
                module_used += seg_len;
                module_name[module_used] = '\0';
            }
            if (module_used == 0) {
                continue;
            }

            if (resolve_package_submodule_file(dep_root, module_name, out)) {
                return 1;
            }
        }
    }

    return 0;
}

static int is_module_load_node(ASTNodeType type) {
    switch (type) {
        case AST_USING:
        case AST_EXPORT:
        case AST_VAR_DECL:
        case AST_FUNC_DECL:
        case AST_ENTITY:
        case AST_EXTEND:
            return 1;
        default:
            return 0;
    }
}

static RuntimeValue eval_module_program(const ASTNode *program, Environment *module_env) {
    RuntimeValue last = make_null();
    ASTNode *load_nodes[1024];
    int load_count = 0;

    if (program == NULL) {
        return last;
    }

    if (program->type != AST_BLOCK) {
        if (!is_module_load_node(program->type)) {
            return last;
        }
        load_nodes[load_count++] = (ASTNode *)program;
    } else {
        for (int i = 0; i < program->data.block.count; i++) {
            ASTNode *stmt = program->data.block.items[i];
            if (stmt == NULL || !is_module_load_node(stmt->type)) {
                continue;
            }
            if (load_count >= (int)(sizeof(load_nodes) / sizeof(load_nodes[0]))) {
                runtime_error("Module has too many top-level load nodes");
            }
            load_nodes[load_count++] = stmt;
        }
    }

    if (load_count == 0) {
        return last;
    }

    ASTNode synthetic = {0};
    synthetic.type = AST_BLOCK;
    synthetic.line = program->line;
    synthetic.data.block.items = load_nodes;
    synthetic.data.block.count = load_count;

    Chunk temp_chunk;
    if (!compile_program(&synthetic, &temp_chunk)) {
        runtime_error("VM could not compile module load program");
    }

    Chunk *owned_chunk = (Chunk *)malloc(sizeof(Chunk));
    if (owned_chunk == NULL) {
        chunk_free(&temp_chunk);
        runtime_error("Out of memory storing compiled module chunk");
    }
    *owned_chunk = temp_chunk;
    vm_register_owned_chunk(owned_chunk);

    VM vm;
    vm_init(&vm, owned_chunk, module_env);
    last = vm_run(&vm);
    return last;
}

static RuntimeValue load_script(const char *abs_path) {
    char *source = read_module_source(abs_path);
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
    Environment *exports_env = env_create_child(NULL);
    gc_push_env(exports_env);

    RuntimeValue exports = make_module(exports_env);
    gc_push_root(&exports);
    env_set_fast(module_env, "exports", get_hash("exports"), exports);

    LoadedModule *record = remember_loaded(abs_path, exports, module_env, 1);

    /* Wrap eval_module_program in setjmp guard to catch panics and mark cache as failed. */
    MKSContext *ctx = mks_context_current();
    jmp_buf saved_jmp;
    int prev_active = ctx->error_active;
    memcpy(&saved_jmp, &ctx->error_jmp, sizeof(jmp_buf));
    ctx->error_active = 1;

    if (setjmp(ctx->error_jmp) != 0) {
        /* eval panicked; mark cache entry as failed and re-raise. */
        ModuleCacheEntry *entry = (ModuleCacheEntry *)record;
        entry->failed = 1;
        ctx->error_active = prev_active;
        memcpy(&ctx->error_jmp, &saved_jmp, sizeof(jmp_buf));
        /* Clean up before re-raising. */
        free(source);
        runtime_pop_source(prev_source);
        runtime_pop_file(prev_file);
        gc_pop_root();
        gc_pop_env();
        gc_pop_env();
        /* Re-raise the error. */
        if (prev_active) {
            longjmp(saved_jmp, 1);
        }
        exit(1);
    }

    eval_module_program(program, module_env);

    ctx->error_active = prev_active;
    memcpy(&ctx->error_jmp, &saved_jmp, sizeof(jmp_buf));
    record->loading = 0;

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
    gc_pop_env();

    return exports;
}

static RuntimeValue load_native_with_init(const char *key, NativeModuleInit init_fn) {
    Environment *module_env = env_create_child(modules_parent_env);
    gc_push_env(module_env);
    Environment *exports_env = env_create_child(NULL);
    gc_push_env(exports_env);

    RuntimeValue exports = make_module(exports_env);
    gc_push_root(&exports);
    env_set_fast(module_env, "exports", get_hash("exports"), exports);

    init_fn(exports, module_env);

    gc_pop_root();
    gc_pop_env();
    gc_pop_env();

    remember_loaded(key, exports, module_env, 0);
    return exports;
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
static int resolve_pkg_module(const char *pkg_name, char *out, size_t out_size) {
    if (pkg_name == NULL || out == NULL || out_size == 0) return 0;

    char start[PATH_MAX];
    const char *cur = runtime_current_file();
    if (cur != NULL && strstr(cur, "::") == NULL) {
        if (!path_dirname(cur, start, sizeof(start))) {
            if (!getcwd(start, sizeof(start))) return 0;
        }
    } else {
        if (!getcwd(start, sizeof(start))) return 0;
    }

    /* Convert to absolute path in case it's relative (e.g., ".") */
    char abs_start[PATH_MAX];
    if (!try_realpath(start, abs_start)) {
        snprintf(abs_start, sizeof(abs_start), "%s", start);
    }

    char dir[PATH_MAX];
    snprintf(dir, sizeof(dir), "%s", abs_start);

    for (int depth = 0; depth < 8; depth++) {
        /* Try mks_modules/<pkg_name>/ directory */
        char pkg_dir[PATH_MAX];
        snprintf(pkg_dir, sizeof(pkg_dir), "%s/mks_modules/%s", dir, pkg_name);
        if (path_is_directory(pkg_dir)) {
            /* Try mks.toml entry */
            PackageToml toml;
            if (package_toml_read(pkg_dir, &toml)) {
                const char *entry = toml.entry[0] ? toml.entry : toml.main;
                if (entry[0]) {
                    snprintf(out, out_size, "%s/%s", pkg_dir, entry);
                    package_toml_free(&toml);
                    if (path_is_regular_file(out)) return 1;
                }
                package_toml_free(&toml);
            }
            /* Try package.pkg manifest */
            PackageManifest manifest;
            if (read_package_manifest(pkg_dir, &manifest) && manifest.has_lib) {
                snprintf(out, out_size, "%s/%s", pkg_dir, manifest.lib_path);
                if (path_is_regular_file(out)) return 1;
            }
            /* Try conventional names */
            snprintf(out, out_size, "%s/lib.mks", pkg_dir);
            if (path_is_regular_file(out)) return 1;
            snprintf(out, out_size, "%s/index.mks", pkg_dir);
            if (path_is_regular_file(out)) return 1;
            snprintf(out, out_size, "%s/mod.mks", pkg_dir);
            if (path_is_regular_file(out)) return 1;
        }

        /* Try mks_modules/<pkg_name>.mkspkg */
        char pkg_arc[PATH_MAX];
        snprintf(pkg_arc, sizeof(pkg_arc), "%s/mks_modules/%s.mkspkg", dir, pkg_name);
        if (path_is_regular_file(pkg_arc)) {
            snprintf(out, out_size, "%s", pkg_arc);
            return 1;
        }

        /* Move up one level */
        char parent[PATH_MAX];
        if (!path_dirname(dir, parent, sizeof(parent))) break;
        if (strcmp(parent, dir) == 0) break;
        snprintf(dir, sizeof(dir), "%s", parent);
    }

    /* Check ~/.mks/packages/<pkg_name>/ */
    const char *home = getenv("HOME");
    if (home != NULL) {
        char hpkg[PATH_MAX];
        snprintf(hpkg, sizeof(hpkg), "%s/.mks/packages/%s", home, pkg_name);
        if (path_is_directory(hpkg)) {
            PackageToml toml;
            if (package_toml_read(hpkg, &toml)) {
                const char *entry = toml.entry[0] ? toml.entry : toml.main;
                if (entry[0]) {
                    snprintf(out, out_size, "%s/%s", hpkg, entry);
                    package_toml_free(&toml);
                    if (path_is_regular_file(out)) return 1;
                }
                package_toml_free(&toml);
            }
            snprintf(out, out_size, "%s/lib.mks", hpkg);
            if (path_is_regular_file(out)) return 1;
        }
    }

    return 0;
}
#pragma GCC diagnostic pop

static int native_name_valid(const char *name) {
    if (name == NULL || name[0] == '\0') return 0;
    if (strstr(name, "..") != NULL) return 0;
    if (strchr(name, '/') != NULL) return 0;
    if (strchr(name, '\\') != NULL) return 0;
    return 1;
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wformat-truncation"
static int resolve_native_module(const char *native_name, char *out, size_t out_size) {
    if (native_name == NULL || out == NULL || out_size == 0) return 0;

    char start[PATH_MAX];
    const char *cur = runtime_current_file();
    if (cur != NULL && strstr(cur, "::") == NULL) {
        if (!path_dirname(cur, start, sizeof(start))) {
            if (!getcwd(start, sizeof(start))) return 0;
        }
    } else {
        if (!getcwd(start, sizeof(start))) return 0;
    }

    char abs_start[PATH_MAX];
    if (!try_realpath(start, abs_start)) {
        snprintf(abs_start, sizeof(abs_start), "%s", start);
    }

    char dir[PATH_MAX];
    snprintf(dir, sizeof(dir), "%s", abs_start);

    for (int depth = 0; depth < 8; depth++) {
        /* Try mks_modules/<native_name>/<native_name>.so */
        char so_path[PATH_MAX];
        snprintf(so_path, sizeof(so_path), "%s/mks_modules/%s/%s.so", dir, native_name, native_name);
        if (path_is_regular_file(so_path)) {
            snprintf(out, out_size, "%s", so_path);
            return 1;
        }

        /* Try mks_modules/<native_name>.so */
        snprintf(so_path, sizeof(so_path), "%s/mks_modules/%s.so", dir, native_name);
        if (path_is_regular_file(so_path)) {
            snprintf(out, out_size, "%s", so_path);
            return 1;
        }

        /* Move up one level */
        char parent[PATH_MAX];
        if (!path_dirname(dir, parent, sizeof(parent))) break;
        if (strcmp(parent, dir) == 0) break;
        snprintf(dir, sizeof(dir), "%s", parent);
    }

    /* Check system paths */
    static const char *system_paths[] = {
        "/usr/local/lib/mks",
        "/usr/lib/mks",
        NULL
    };

    for (int i = 0; system_paths[i] != NULL; i++) {
        char so_path[PATH_MAX];
        snprintf(so_path, sizeof(so_path), "%s/%s.so", system_paths[i], native_name);
        if (path_is_regular_file(so_path)) {
            snprintf(out, out_size, "%s", so_path);
            return 1;
        }
    }

    return 0;
}
#pragma GCC diagnostic pop

#ifdef __unix__
static RuntimeValue load_native_dlopen(const char *name, const char *path) {
    /* Use RTLD_GLOBAL so the .so can access symbols from the main executable. */
    void *handle = dlopen(path, RTLD_LAZY | RTLD_GLOBAL);
    if (!handle) {
        runtime_error("dlopen '%s': %s", path, dlerror());
    }

    /* Policy: native module handles are never closed (no dlclose at exit).
     * The OS reclaims them on process termination. This avoids use-after-free
     * risks from static destructors or cleanup code in the .so that runs after
     * MKS module_free_all() has already freed the environment. */

    char sym[256];
    snprintf(sym, sizeof(sym), "mks_module_init_%s", name);
    NativeModuleInit init = (NativeModuleInit)(uintptr_t)dlsym(handle, sym);
    if (!init) {
        dlclose(handle);
        runtime_error("symbol '%s' not found in '%s'", sym, path);
    }

    /* Check ABI version compatibility before calling init. */
    typedef int (*AbiVersionFn)(void);
    AbiVersionFn abi_fn = (AbiVersionFn)(uintptr_t)dlsym(handle, "mks_module_abi_version");
    if (!abi_fn) {
        dlclose(handle);
        runtime_error("native module '%s' does not expose mks_module_abi_version", name);
    }
    int module_abi = abi_fn();
    if (module_abi != MKS_MODULE_ABI_VERSION) {
        dlclose(handle);
        runtime_error("native module '%s' ABI mismatch: expected %d, got %d",
                      name, MKS_MODULE_ABI_VERSION, module_abi);
    }

    Environment *module_env = env_create_child(modules_parent_env);
    gc_push_env(module_env);
    Environment *exports_env = env_create_child(NULL);
    gc_push_env(exports_env);

    RuntimeValue exports = make_module(exports_env);
    gc_push_root(&exports);
    env_set_fast(module_env, "exports", get_hash("exports"), exports);

    /* Wrap init() call in setjmp guard to catch panics and clean up resources. */
    MKSContext *ctx = mks_context_current();
    jmp_buf saved_jmp;
    int prev_active = ctx->error_active;
    memcpy(&saved_jmp, &ctx->error_jmp, sizeof(jmp_buf));
    ctx->error_active = 1;

    if (setjmp(ctx->error_jmp) != 0) {
        /* init() panicked; restore error state and clean up. */
        ctx->error_active = prev_active;
        memcpy(&ctx->error_jmp, &saved_jmp, sizeof(jmp_buf));
        gc_pop_root();
        gc_pop_env();
        gc_pop_env();
        /* Re-raise the error. If an outer error handler is active, longjmp to it; else exit. */
        if (prev_active) {
            longjmp(saved_jmp, 1);
        }
        exit(1);
    }

    init(exports, module_env);

    ctx->error_active = prev_active;
    memcpy(&ctx->error_jmp, &saved_jmp, sizeof(jmp_buf));

    gc_pop_root();
    gc_pop_env();
    gc_pop_env();

    remember_loaded(name, exports, module_env, 0);
    return exports;
}
#endif

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
            char alias_buf[PATH_MAX];
            if (alias == NULL) {
                module_guess_alias(spec, alias_buf, sizeof(alias_buf));
                alias = alias_buf;
            }
            bind_module_alias(requester_env, alias, exports);
            return exports;
        }
    }

    /* 1b. Built-in descriptor table */
    const ModuleDescriptor *desc = std_registry_lookup(normalized_id);

    if (desc) {
        if (desc->kind == MODULE_KIND_NATIVE) {
            LoadedModule *lm = find_loaded(desc->id);
            RuntimeValue exports = lm ? lm->exports : load_native_with_init(desc->id, desc->native_init);
            char alias_buf[PATH_MAX];
            if (alias == NULL) {
                module_guess_alias(desc->id, alias_buf, sizeof(alias_buf));
                alias = alias_buf;
            }
            bind_module_alias(requester_env, alias, exports);
            return exports;
        } else { /* MODULE_KIND_FILE */
            char abs_path[PATH_MAX];
            if (!make_builtin_module_path(desc->file_path, abs_path)) {
                runtime_error("Cannot resolve module file '%s' for '%s'", desc->file_path, desc->id);
            }
            LoadedModule *lm = find_loaded(abs_path);
            RuntimeValue exports = lm ? lm->exports : load_script(abs_path);
            char alias_buf[PATH_MAX];
            if (alias == NULL) {
                module_guess_alias(desc->id, alias_buf, sizeof(alias_buf));
                alias = alias_buf;
            }
            bind_module_alias(requester_env, alias, exports);
            return exports;
        }
    }

    /* 1b2. Explicit native.X import (dlopen) */
    if (!is_legacy_path && strncmp(spec, "native.", 7) == 0) {
#ifdef __unix__
        const char *native_name = spec + 7;
        if (!native_name_valid(native_name)) {
            runtime_error("invalid native module name: '%s' (must not contain /, \\, or ..)", native_name);
        }
        char so_path[PATH_MAX];
        if (!resolve_native_module(native_name, so_path, sizeof(so_path))) {
            runtime_error("cannot find native.%s\n  Searched: ./mks_modules/, parent dirs, /usr/local/lib/mks/, /usr/lib/mks/", native_name);
        }
        LoadedModule *lm = find_loaded(native_name);
        RuntimeValue exports = lm ? lm->exports : load_native_dlopen(native_name, so_path);
        char alias_buf[PATH_MAX];
        if (alias == NULL) {
            module_guess_alias(native_name, alias_buf, sizeof(alias_buf));
            alias = alias_buf;
        }
        bind_module_alias(requester_env, alias, exports);
        return exports;
#else
        runtime_error("native module loading not supported on this platform");
#endif
    }

    /* 1c. Explicit pkg.X import */
    if (!is_legacy_path && strncmp(spec, "pkg.", 4) == 0) {
        const char *pkg_name = spec + 4;
        if (!resolve_pkg_module(pkg_name, key, sizeof(key))) {
            runtime_error("cannot find pkg.%s\n  Searched: ./mks_modules/%s/ and parent directories", pkg_name, pkg_name);
        }
        LoadedModule *lm2 = find_loaded(key);
        RuntimeValue exports2 = lm2 ? lm2->exports : load_script(key);
        char alias_buf2[PATH_MAX];
        if (alias == NULL) {
            module_guess_alias(pkg_name, alias_buf2, sizeof(alias_buf2));
            alias = alias_buf2;
        }
        bind_module_alias(requester_env, alias, exports2);
        return exports2;
    }

    /* 2. Resolve user script path or package module. */
    if (is_legacy_path) {
        if (!make_absolute_path(spec, key)) {
            runtime_error("Cannot resolve path '%s'", spec);
        }
    } else if (strncmp(spec, "./", 2) == 0 || strncmp(spec, "../", 3) == 0) {
        if (!make_absolute_path(spec, key)) {
            runtime_error("Cannot resolve relative module '%s'", spec);
        }
    } else if (resolve_package_import(spec, key, sizeof(key))) {
        /* resolved through current package root and packages/ */
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

    char alias_buf[PATH_MAX];
    if (alias == NULL) {
        module_guess_alias(spec, alias_buf, sizeof(alias_buf));
        alias = alias_buf;
    }
    bind_module_alias(requester_env, alias, exports);
    return exports;
}

void module_guess_alias(const char *spec, char *out, size_t out_size) {
    const char *slash = strrchr(spec, '/');
    const char *dot = strrchr(spec, '.');
    const char *base = spec;

    if (slash != NULL && dot != NULL) {
        base = slash > dot ? slash + 1 : dot + 1;
    } else if (slash != NULL) {
        base = slash + 1;
    } else if (dot != NULL) {
        base = dot + 1;
    }

    char tmp[PATH_MAX];
    append_ext_if_missing(base, tmp, sizeof(tmp));
    char *ext = strrchr(tmp, '.');
    if (ext) *ext = '\0';
    strncpy(out, tmp, out_size - 1);
    out[out_size - 1] = '\0';
}
