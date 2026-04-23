#define _XOPEN_SOURCE 700

#include "fs.h"

#include "../Runtime/errors.h"
#include "../Runtime/context.h"
#include "../Runtime/module.h"
#include "../Runtime/value.h"
#include "../Utils/hash.h"
#include "../GC/gc.h"
#include "../env/env.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define fs_module_env (mks_context_current()->fs_module_env)

static inline RuntimeValue make_bool(int cond) {
    return make_int(cond ? 1 : 0);
}

static const char *expect_path(const RuntimeValue *v) {
    if (v->type != VAL_STRING) runtime_error("fs: path must be string");
    return v->data.managed_string->data;
}

static char *read_entire_file(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;

    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long end = ftell(f);
    if (end < 0) { fclose(f); return NULL; }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }

    size_t len = (size_t)end;
    char *buf = (char *)malloc(len + 1);
    if (!buf) { fclose(f); runtime_error("fs.read: out of memory"); }

    size_t read = fread(buf, 1, len, f);
    int err = ferror(f);
    fclose(f);

    if (read != len && err) { free(buf); return NULL; }

    buf[read] = '\0';
    if (out_len) *out_len = read;
    return buf;
}

static RuntimeValue array_push_string(RuntimeValue arr, const char *s) {
    RuntimeValue str = make_string(s);
    ManagedArray *a = arr.data.managed_array;
    if (a->count >= a->capacity) {
        int newcap = a->capacity > 0 ? a->capacity * 2 : 4;
        a->elements = realloc(a->elements, (size_t)newcap * sizeof(RuntimeValue));
        a->capacity = newcap;
    }
    a->elements[a->count++] = str;
    return arr;
}

static RuntimeValue n_exists(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    (void)ctx;
    if (arg_count != 1) runtime_error("exists expects 1 arg");
    const char *p = expect_path(&args[0]);
    return make_bool(access(p, F_OK) == 0);
}

static RuntimeValue n_read(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    (void)ctx;
    if (arg_count != 1) runtime_error("read expects 1 arg");
    size_t len = 0;
    char *buf = read_entire_file(expect_path(&args[0]), &len);
    if (!buf) return make_null();
    return make_string_owned(buf, len);
}

static RuntimeValue n_write(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    (void)ctx;
    if (arg_count != 2) runtime_error("write expects (path, data)");
    const char *p = expect_path(&args[0]);
    if (args[1].type != VAL_STRING) runtime_error("write: data must be string");

    const char *data = args[1].data.managed_string->data;
    size_t len = args[1].data.managed_string->len;

    FILE *f = fopen(p, "wb");
    if (!f) runtime_error("write: cannot open file (%s)", strerror(errno));

    size_t n = fwrite(data, 1, len, f);
    if (fclose(f) != 0 || n != len) runtime_error("write: short write (%s)", strerror(errno));
    return make_null();
}

static RuntimeValue n_append(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    (void)ctx;
    if (arg_count != 2) runtime_error("append expects (path, data)");
    const char *p = expect_path(&args[0]);
    if (args[1].type != VAL_STRING) runtime_error("append: data must be string");

    const char *data = args[1].data.managed_string->data;
    size_t len = args[1].data.managed_string->len;

    FILE *f = fopen(p, "ab");
    if (!f) runtime_error("append: cannot open file (%s)", strerror(errno));

    size_t n = fwrite(data, 1, len, f);
    if (fclose(f) != 0 || n != len) runtime_error("append: short write (%s)", strerror(errno));
    return make_null();
}

static RuntimeValue n_remove(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    (void)ctx;
    if (arg_count != 1) runtime_error("remove expects 1 arg");
    const char *p = expect_path(&args[0]);
    int rc = unlink(p);
    return make_bool(rc == 0);
}

static RuntimeValue n_mkdir(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    (void)ctx;
    if (arg_count < 1 || arg_count > 2) runtime_error("mkdir expects (path [, mode])");
    const char *p = expect_path(&args[0]);
    mode_t mode = 0755;
    if (arg_count == 2) {
        mode = (mode_t)runtime_value_as_int(args[1]);
    }
    int rc = mkdir(p, mode);
    if (rc != 0 && errno != EEXIST) runtime_error("mkdir failed (%s)", strerror(errno));
    return make_bool(rc == 0 || errno == EEXIST);
}

static RuntimeValue n_stat(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    (void)ctx;
    if (arg_count != 1) runtime_error("stat expects 1 arg");
    const char *p = expect_path(&args[0]);
    struct stat st;
    if (stat(p, &st) != 0) return make_null();

    Environment *obj_env = env_create_child(fs_module_env);
    gc_push_env(obj_env);
    RuntimeValue obj = make_object(obj_env);
    MKS_WITH_GC_ROOT(&obj) {
        env_set_fast(obj_env, "size", get_hash("size"), make_int((int64_t)st.st_size));
        env_set_fast(obj_env, "mode", get_hash("mode"), make_int((int64_t)st.st_mode));
        env_set_fast(obj_env, "mtime", get_hash("mtime"), make_int((int64_t)st.st_mtime));
        env_set_fast(obj_env, "atime", get_hash("atime"), make_int((int64_t)st.st_atime));
        env_set_fast(obj_env, "is_dir", get_hash("is_dir"), make_bool(S_ISDIR(st.st_mode)));
        env_set_fast(obj_env, "is_file", get_hash("is_file"), make_bool(S_ISREG(st.st_mode)));
    }
    gc_pop_env();
    return obj;
}

static RuntimeValue n_list(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    (void)ctx;
    if (arg_count != 1) runtime_error("list expects 1 arg");
    const char *p = expect_path(&args[0]);
    DIR *d = opendir(p);
    if (!d) return make_null();
    RuntimeValue arr = make_array(16);
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
        arr = array_push_string(arr, ent->d_name);
    }
    closedir(d);
    return arr;
}

static RuntimeValue n_rename(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    (void)ctx;
    if (arg_count != 2) runtime_error("rename expects (old, new)");
    const char *oldp = expect_path(&args[0]);
    const char *newp = expect_path(&args[1]);
    return make_bool(rename(oldp, newp) == 0);
}

static void bind(RuntimeValue exports, const char *name, NativeFn fn) {
    module_bind_native(exports, name, fn);
}

void std_init_fs(RuntimeValue exports, Environment *module_env) {
    fs_module_env = module_env;
    bind(exports, "exists", n_exists);
    bind(exports, "read", n_read);
    bind(exports, "write", n_write);
    bind(exports, "append", n_append);
    bind(exports, "remove", n_remove);
    bind(exports, "mkdir", n_mkdir);
    bind(exports, "stat", n_stat);
    bind(exports, "list", n_list);
    bind(exports, "rename", n_rename);
}
