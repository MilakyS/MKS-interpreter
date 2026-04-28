#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "Eval/eval.h"
#include "GC/gc.h"
#include "Lexer/lexer.h"
#include "Parser/parser.h"
#include "Runtime/context.h"
#include "Runtime/errors.h"
#include "Runtime/runner.h"
#include "Utils/file.h"

#ifndef MKS_VERSION
#define MKS_VERSION "dev"
#endif

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} CliBuffer;

typedef struct {
    char name[256];
    char version[64];
    char main_path[512];
    char lib[512];
    int has_name;
    int has_version;
    int has_main;
    int has_lib;
} PackageInfo;

static void print_usage(const char *argv0) {
    printf("Monkey Kernel Syntax (MKS) Interpreter\n");
    printf("\n");
    printf("Usage:\n");
    printf("  %s <file.mks>              Execute a file\n", argv0);
    printf("  %s repl                    Start interactive REPL\n", argv0);
    printf("  %s check <file.mks>        Parse/check a file without executing it\n", argv0);
    printf("  %s pkg init <name>         Create package.pkg and src/lib.mks\n", argv0);
    printf("  %s pkg build [dir]         Build .mkspkg artifacts under dist/\n", argv0);
    printf("  %s pkg check [dir]         Validate package.pkg\n", argv0);
    printf("  %s pkg info [dir]          Print package metadata\n", argv0);
    printf("  %s --version               Show version\n", argv0);
    printf("  %s --help                  Show this help\n", argv0);
    printf("\n");
    printf("Compatibility:\n");
    printf("  %s --repl                  Alias for `repl`\n", argv0);
    printf("  %s --profile <file.mks>    Execute a file with profiler\n", argv0);
}

static void cli_buffer_free(CliBuffer *buf) {
    free(buf->data);
    buf->data = NULL;
    buf->len = 0;
    buf->cap = 0;
}

static int cli_buffer_reserve(CliBuffer *buf, size_t extra) {
    if (buf->len + extra + 1 <= buf->cap) {
        return 1;
    }

    size_t new_cap = buf->cap == 0 ? 4096 : buf->cap;
    while (new_cap < buf->len + extra + 1) {
        new_cap *= 2;
    }

    char *new_data = (char *)realloc(buf->data, new_cap);
    if (new_data == NULL) {
        fprintf(stderr, "mks: out of memory\n");
        return 0;
    }

    buf->data = new_data;
    buf->cap = new_cap;
    return 1;
}

static int cli_buffer_append(CliBuffer *buf, const char *text) {
    size_t len = strlen(text);
    if (!cli_buffer_reserve(buf, len)) {
        return 0;
    }
    memcpy(buf->data + buf->len, text, len);
    buf->len += len;
    buf->data[buf->len] = '\0';
    return 1;
}

static void cli_buffer_clear(CliBuffer *buf) {
    buf->len = 0;
    if (buf->data != NULL) {
        buf->data[0] = '\0';
    }
}

static int path_exists(const char *path) {
    struct stat st;
    return path != NULL && stat(path, &st) == 0;
}

static int is_directory(const char *path) {
    struct stat st;
    return path != NULL && stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static int ensure_dir(const char *path) {
    if (is_directory(path)) {
        return 1;
    }
    if (mkdir(path, 0777) == 0) {
        return 1;
    }
    fprintf(stderr, "mks: cannot create directory '%s': %s\n", path, strerror(errno));
    return 0;
}

static int write_text_file(const char *path, const char *content) {
    FILE *f = fopen(path, "w");
    if (f == NULL) {
        fprintf(stderr, "mks: cannot write '%s': %s\n", path, strerror(errno));
        return 0;
    }
    fputs(content, f);
    if (fclose(f) != 0) {
        fprintf(stderr, "mks: cannot close '%s': %s\n", path, strerror(errno));
        return 0;
    }
    return 1;
}

static int copy_file(const char *src, const char *dst) {
    FILE *in = fopen(src, "rb");
    if (in == NULL) {
        fprintf(stderr, "mks: cannot read '%s': %s\n", src, strerror(errno));
        return 0;
    }

    FILE *out = fopen(dst, "wb");
    if (out == NULL) {
        fprintf(stderr, "mks: cannot write '%s': %s\n", dst, strerror(errno));
        fclose(in);
        return 0;
    }

    char buffer[8192];
    size_t n;
    int ok = 1;
    while ((n = fread(buffer, 1, sizeof(buffer), in)) > 0) {
        if (fwrite(buffer, 1, n, out) != n) {
            fprintf(stderr, "mks: failed writing '%s'\n", dst);
            ok = 0;
            break;
        }
    }
    if (ferror(in)) {
        fprintf(stderr, "mks: failed reading '%s': %s\n", src, strerror(errno));
        ok = 0;
    }
    if (fclose(out) != 0) {
        fprintf(stderr, "mks: cannot close '%s': %s\n", dst, strerror(errno));
        ok = 0;
    }
    fclose(in);
    if (!ok) {
        remove(dst);
    }
    return ok;
}

static const char *skip_space(const char *p) {
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    return p;
}

static int read_quoted_field(const char *line, const char *field, char *out, size_t out_size) {
    line = skip_space(line);
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

static int valid_package_name(const char *name) {
    if (name == NULL || name[0] == '\0' || name[0] == '.' || strchr(name, '/') != NULL || strchr(name, '\\') != NULL) {
        return 0;
    }

    int prev_dot = 0;
    for (const char *p = name; *p != '\0'; p++) {
        unsigned char c = (unsigned char)*p;
        if (*p == '.') {
            if (prev_dot) {
                return 0;
            }
            prev_dot = 1;
            continue;
        }
        if (!(isalnum(c) || *p == '_' || *p == '-')) {
            return 0;
        }
        prev_dot = 0;
    }

    return !prev_dot;
}

static int directory_is_init_safe(const char *path) {
    DIR *dir = opendir(path);
    if (dir == NULL) {
        fprintf(stderr, "mks: cannot open directory '%s': %s\n", path, strerror(errno));
        return 0;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        const char *name = entry->d_name;
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0 || strcmp(name, ".git") == 0) {
            continue;
        }
        closedir(dir);
        fprintf(stderr, "mks: current directory is not empty; refusing to initialize package\n");
        return 0;
    }

    closedir(dir);
    return 1;
}

static int parse_package_file(const char *dir, PackageInfo *info) {
    memset(info, 0, sizeof(*info));

    char path[1024];
    if (snprintf(path, sizeof(path), "%s/package.pkg", dir) >= (int)sizeof(path)) {
        fprintf(stderr, "mks: package path is too long\n");
        return 0;
    }

    char *source = mks_read_file(path);
    if (source == NULL) {
        return 0;
    }

    char *cursor = source;
    while (*cursor != '\0') {
        char *line_end = strchr(cursor, '\n');
        if (line_end != NULL) {
            *line_end = '\0';
        }

        const char *line = skip_space(cursor);
        if (*line == '#' || *line == '\0') {
            /* skip */
        } else if (read_quoted_field(line, "package", info->name, sizeof(info->name))) {
            info->has_name = 1;
        } else if (read_quoted_field(line, "version", info->version, sizeof(info->version))) {
            info->has_version = 1;
        } else if (read_quoted_field(line, "main", info->main_path, sizeof(info->main_path))) {
            info->has_main = 1;
        } else if (read_quoted_field(line, "lib", info->lib, sizeof(info->lib))) {
            info->has_lib = 1;
        }

        if (line_end == NULL) {
            break;
        }
        cursor = line_end + 1;
    }

    free(source);
    return 1;
}

static int package_lib_exists(const char *dir, const PackageInfo *info, char *out, size_t out_size) {
    const char *lib = info->has_lib ? info->lib : "src/lib.mks";
    if (snprintf(out, out_size, "%s/%s", dir, lib) >= (int)out_size) {
        return 0;
    }
    return path_exists(out);
}

static int package_relative_file_exists(const char *dir, const char *relative_path, char *out, size_t out_size) {
    if (snprintf(out, out_size, "%s/%s", dir, relative_path) >= (int)out_size) {
        return 0;
    }
    return path_exists(out);
}

static int path_is_under_src(const char *relative_path) {
    return strncmp(relative_path, "src/", 4) == 0 || strcmp(relative_path, "src") == 0;
}

static int has_suffix(const char *text, const char *suffix) {
    size_t text_len = strlen(text);
    size_t suffix_len = strlen(suffix);
    return text_len >= suffix_len && strcmp(text + text_len - suffix_len, suffix) == 0;
}

static int join_path(char *out, size_t out_size, const char *left, const char *right) {
    return snprintf(out, out_size, "%s/%s", left, right) < (int)out_size;
}

static int write_u32_le(FILE *f, uint32_t value) {
    unsigned char b[4] = {
        (unsigned char)(value & 0xff),
        (unsigned char)((value >> 8) & 0xff),
        (unsigned char)((value >> 16) & 0xff),
        (unsigned char)((value >> 24) & 0xff)
    };
    return fwrite(b, 1, sizeof(b), f) == sizeof(b);
}

static int write_u64_le(FILE *f, uint64_t value) {
    unsigned char b[8] = {
        (unsigned char)(value & 0xff),
        (unsigned char)((value >> 8) & 0xff),
        (unsigned char)((value >> 16) & 0xff),
        (unsigned char)((value >> 24) & 0xff),
        (unsigned char)((value >> 32) & 0xff),
        (unsigned char)((value >> 40) & 0xff),
        (unsigned char)((value >> 48) & 0xff),
        (unsigned char)((value >> 56) & 0xff)
    };
    return fwrite(b, 1, sizeof(b), f) == sizeof(b);
}

static int archive_add_file(FILE *archive, const char *src_path, const char *entry_path) {
    FILE *in = fopen(src_path, "rb");
    if (in == NULL) {
        fprintf(stderr, "mks: cannot read '%s': %s\n", src_path, strerror(errno));
        return 0;
    }
    if (fseek(in, 0, SEEK_END) != 0) {
        fprintf(stderr, "mks: cannot seek '%s': %s\n", src_path, strerror(errno));
        fclose(in);
        return 0;
    }
    long file_size = ftell(in);
    if (file_size < 0) {
        fprintf(stderr, "mks: cannot size '%s': %s\n", src_path, strerror(errno));
        fclose(in);
        return 0;
    }
    if (fseek(in, 0, SEEK_SET) != 0) {
        fprintf(stderr, "mks: cannot rewind '%s': %s\n", src_path, strerror(errno));
        fclose(in);
        return 0;
    }

    size_t path_len = strlen(entry_path);
    if (path_len == 0 || path_len > UINT32_MAX) {
        fprintf(stderr, "mks: archive entry path is too long: %s\n", entry_path);
        fclose(in);
        return 0;
    }
    if (!write_u32_le(archive, (uint32_t)path_len) ||
        !write_u64_le(archive, (uint64_t)file_size) ||
        fwrite(entry_path, 1, path_len, archive) != path_len) {
        fprintf(stderr, "mks: failed writing archive header for '%s'\n", entry_path);
        fclose(in);
        return 0;
    }

    char buffer[8192];
    size_t n;
    while ((n = fread(buffer, 1, sizeof(buffer), in)) > 0) {
        if (fwrite(buffer, 1, n, archive) != n) {
            fprintf(stderr, "mks: failed writing archive entry '%s'\n", entry_path);
            fclose(in);
            return 0;
        }
    }
    if (ferror(in)) {
        fprintf(stderr, "mks: failed reading '%s': %s\n", src_path, strerror(errno));
        fclose(in);
        return 0;
    }
    fclose(in);
    return 1;
}

static int archive_add_mks_tree(FILE *archive, const char *src_dir, const char *entry_prefix) {
    DIR *dir = opendir(src_dir);
    if (dir == NULL) {
        fprintf(stderr, "mks: cannot open source directory '%s': %s\n", src_dir, strerror(errno));
        return 0;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        const char *name = entry->d_name;
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
            continue;
        }

        char src_path[1024];
        char entry_path[1024];
        if (!join_path(src_path, sizeof(src_path), src_dir, name) ||
            !join_path(entry_path, sizeof(entry_path), entry_prefix, name)) {
            fprintf(stderr, "mks: package path is too long\n");
            closedir(dir);
            return 0;
        }

        if (is_directory(src_path)) {
            if (!archive_add_mks_tree(archive, src_path, entry_path)) {
                closedir(dir);
                return 0;
            }
        } else if (has_suffix(name, ".mks")) {
            if (!archive_add_file(archive, src_path, entry_path)) {
                closedir(dir);
                return 0;
            }
        }
    }

    closedir(dir);
    return 1;
}

static int archive_add_optional_file(FILE *archive, const char *src_dir, const char *name) {
    char src_path[1024];
    if (!join_path(src_path, sizeof(src_path), src_dir, name)) {
        fprintf(stderr, "mks: package path is too long\n");
        return 0;
    }
    if (!path_exists(src_path)) {
        return 1;
    }
    return archive_add_file(archive, src_path, name);
}

static int archive_add_package_path(FILE *archive, const char *src_dir, const char *relative_path) {
    char src_path[1024];
    if (!package_relative_file_exists(src_dir, relative_path, src_path, sizeof(src_path))) {
        fprintf(stderr, "mks: package file is missing: %s\n", relative_path);
        return 0;
    }
    return archive_add_file(archive, src_path, relative_path);
}

static int validate_package_for_build(const char *dir, PackageInfo *info, char *lib_path, size_t lib_path_size) {
    if (!parse_package_file(dir, info)) {
        return 0;
    }
    if (!info->has_name || !valid_package_name(info->name)) {
        fprintf(stderr, "mks: package.pkg has invalid or missing package name\n");
        return 0;
    }
    if (!package_lib_exists(dir, info, lib_path, lib_path_size)) {
        fprintf(stderr, "mks: package lib file is missing: %s\n", info->has_lib ? info->lib : "src/lib.mks");
        return 0;
    }
    if (info->has_main) {
        char main_path[1024];
        if (!package_relative_file_exists(dir, info->main_path, main_path, sizeof(main_path))) {
            fprintf(stderr, "mks: package main file is missing: %s\n", info->main_path);
            return 0;
        }
    }
    return 1;
}

static int cmd_pkg_init(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr, "Usage: %s pkg init <name>\n", argv[0]);
        return 1;
    }
    if (argc > 4) {
        fprintf(stderr, "mks: pkg init takes exactly one package name\n");
        return 1;
    }

    const char *name = argv[3];
    if (!valid_package_name(name)) {
        fprintf(stderr, "mks: invalid package name '%s'\n", name);
        return 1;
    }
    if (path_exists("package.pkg")) {
        fprintf(stderr, "mks: package.pkg already exists\n");
        return 1;
    }
    if (!directory_is_init_safe(".")) {
        return 1;
    }
    if (!ensure_dir("src")) {
        return 1;
    }

    char manifest[1024];
    snprintf(manifest,
             sizeof(manifest),
             "package \"%s\";\n"
             "version \"0.1.0\";\n"
             "lib \"src/lib.mks\";\n",
             name);

    char lib_source[1024];
    snprintf(lib_source,
             sizeof(lib_source),
             "export fnc hello() ->\n"
             "    return \"hello from %s\";\n"
             "<-\n",
             name);

    if (!write_text_file("package.pkg", manifest)) {
        return 1;
    }
    if (!write_text_file("src/lib.mks", lib_source)) {
        return 1;
    }

    printf("created package %s\n", name);
    printf("  package.pkg\n");
    printf("  src/lib.mks\n");
    return 0;
}

static int cmd_pkg_check_or_info(int argc, char **argv, int show_info_only) {
    const char *dir = argc >= 4 ? argv[3] : ".";
    if (argc > 4) {
        fprintf(stderr, "Usage: %s pkg %s [dir]\n", argv[0], show_info_only ? "info" : "check");
        return 1;
    }

    PackageInfo info;
    if (!parse_package_file(dir, &info)) {
        return 1;
    }

    char lib_path[1024];
    int ok = 1;
    if (!info.has_name) {
        fprintf(stderr, "mks: package.pkg is missing package \"name\";\n");
        ok = 0;
    } else if (!valid_package_name(info.name)) {
        fprintf(stderr, "mks: invalid package name '%s'\n", info.name);
        ok = 0;
    }
    if (!package_lib_exists(dir, &info, lib_path, sizeof(lib_path))) {
        fprintf(stderr, "mks: package lib file is missing: %s\n", info.has_lib ? info.lib : "src/lib.mks");
        ok = 0;
    }
    if (info.has_main) {
        char main_path[1024];
        if (!package_relative_file_exists(dir, info.main_path, main_path, sizeof(main_path))) {
            fprintf(stderr, "mks: package main file is missing: %s\n", info.main_path);
            ok = 0;
        }
    }

    if (show_info_only || ok) {
        printf("name: %s\n", info.has_name ? info.name : "<missing>");
        printf("version: %s\n", info.has_version ? info.version : "<none>");
        printf("main: %s\n", info.has_main ? info.main_path : "<none>");
        printf("lib: %s\n", info.has_lib ? info.lib : "src/lib.mks");
    }
    if (!show_info_only) {
        printf("status: %s\n", ok ? "ok" : "failed");
    }

    return ok ? 0 : 1;
}

static int cmd_pkg_build(int argc, char **argv) {
    const char *dir = argc >= 4 ? argv[3] : ".";
    if (argc > 4) {
        fprintf(stderr, "Usage: %s pkg build [dir]\n", argv[0]);
        return 1;
    }

    PackageInfo info;
    char lib_path[1024];
    if (!validate_package_for_build(dir, &info, lib_path, sizeof(lib_path))) {
        return 1;
    }

    const char *version = info.has_version ? info.version : "0.0.0";
    char dist_dir[1024];
    char artifact_dir[1024];
    char install_ready_path[1024];
    if (!join_path(dist_dir, sizeof(dist_dir), dir, "dist")) {
        fprintf(stderr, "mks: package path is too long\n");
        return 1;
    }
    if (snprintf(artifact_dir, sizeof(artifact_dir), "%s/%s-%s.mkspkg", dist_dir, info.name, version) >= (int)sizeof(artifact_dir)) {
        fprintf(stderr, "mks: artifact path is too long\n");
        return 1;
    }
    if (snprintf(install_ready_path, sizeof(install_ready_path), "%s/%s.mkspkg", dist_dir, info.name) >= (int)sizeof(install_ready_path)) {
        fprintf(stderr, "mks: artifact path is too long\n");
        return 1;
    }
    if (path_exists(artifact_dir)) {
        fprintf(stderr, "mks: artifact already exists: %s\n", artifact_dir);
        return 1;
    }
    if (path_exists(install_ready_path)) {
        fprintf(stderr, "mks: install-ready artifact already exists: %s\n", install_ready_path);
        return 1;
    }

    if (!ensure_dir(dist_dir)) {
        return 1;
    }

    char src_manifest[1024];
    if (!join_path(src_manifest, sizeof(src_manifest), dir, "package.pkg")) {
        fprintf(stderr, "mks: package path is too long\n");
        return 1;
    }

    FILE *archive = fopen(artifact_dir, "wb");
    if (archive == NULL) {
        fprintf(stderr, "mks: cannot write artifact '%s': %s\n", artifact_dir, strerror(errno));
        return 1;
    }
    if (fwrite("MKSPKG1\n", 1, 8, archive) != 8) {
        fprintf(stderr, "mks: failed writing artifact header\n");
        fclose(archive);
        return 1;
    }

    int ok = archive_add_file(archive, src_manifest, "package.pkg");

    char src_dir[1024];
    if (!join_path(src_dir, sizeof(src_dir), dir, "src")) {
        fprintf(stderr, "mks: package path is too long\n");
        fclose(archive);
        return 1;
    }
    if (ok && is_directory(src_dir)) {
        ok = archive_add_mks_tree(archive, src_dir, "src");
    }
    if (ok && info.has_lib && !path_is_under_src(info.lib)) {
        ok = archive_add_package_path(archive, dir, info.lib);
    }
    if (ok && info.has_main && !path_is_under_src(info.main_path) &&
        (!info.has_lib || strcmp(info.main_path, info.lib) != 0)) {
        ok = archive_add_package_path(archive, dir, info.main_path);
    }
    if (ok) {
        ok = archive_add_optional_file(archive, dir, "README.md");
    }
    if (ok) {
        ok = archive_add_optional_file(archive, dir, "LICENSE");
    }
    if (ok && !write_u32_le(archive, 0)) {
        fprintf(stderr, "mks: failed writing artifact terminator\n");
        ok = 0;
    }
    if (fclose(archive) != 0) {
        fprintf(stderr, "mks: cannot close artifact '%s': %s\n", artifact_dir, strerror(errno));
        ok = 0;
    }
    if (!ok) {
        remove(artifact_dir);
        return 1;
    }

    if (!copy_file(artifact_dir, install_ready_path)) {
        return 1;
    }

    printf("built package %s %s\n", info.name, version);
    printf("  %s\n", artifact_dir);
    printf("  %s\n", install_ready_path);
    return 0;
}

static int cmd_pkg(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s pkg init <name> | pkg build [dir] | pkg check [dir] | pkg info [dir]\n", argv[0]);
        return 1;
    }
    if (strcmp(argv[2], "init") == 0) {
        return cmd_pkg_init(argc, argv);
    }
    if (strcmp(argv[2], "build") == 0) {
        return cmd_pkg_build(argc, argv);
    }
    if (strcmp(argv[2], "check") == 0) {
        return cmd_pkg_check_or_info(argc, argv, 0);
    }
    if (strcmp(argv[2], "info") == 0) {
        return cmd_pkg_check_or_info(argc, argv, 1);
    }

    fprintf(stderr, "mks: unknown pkg command '%s'\n", argv[2]);
    return 1;
}

static int repl_depth_delta(const char *line) {
    int delta = 0;
    int in_string = 0;
    int escaped = 0;

    for (const char *p = line; *p != '\0'; p++) {
        if (in_string) {
            if (escaped) {
                escaped = 0;
            } else if (*p == '\\') {
                escaped = 1;
            } else if (*p == '"') {
                in_string = 0;
            }
            continue;
        }

        if (*p == '"') {
            in_string = 1;
        } else if (*p == '/' && p[1] == '/') {
            break;
        } else if (*p == '-' && p[1] == '>') {
            delta++;
            p++;
        } else if (*p == '<' && p[1] == '-') {
            delta--;
            p++;
        }
    }

    return delta;
}

static int source_complete(const CliBuffer *buf, int depth) {
    if (buf->len == 0 || depth > 0) {
        return 0;
    }
    if (depth < 0) {
        return 1;
    }

    for (size_t i = buf->len; i > 0; i--) {
        unsigned char c = (unsigned char)buf->data[i - 1];
        if (isspace(c)) {
            continue;
        }
        return c == ';' || c == '-';
    }

    return 0;
}

static int eval_repl_source(MKSContext *ctx, Environment *env, const char *source) {
    const int was_active = ctx->error_active;
    if (setjmp(ctx->error_jmp) != 0) {
        ctx->error_active = was_active;
        return 1;
    }
    ctx->error_active = 1;
    ctx->error_status = 0;

    runtime_set_source(source);
    runtime_set_file("<repl>");

    struct Lexer lexer;
    Token_init(&lexer, source);
    Parser parser;
    parser_init(&parser, &lexer);
    ASTNode *program = parser_parse_program(&parser);
    if (program != NULL) {
        eval(program, env);
    }
    delete_ast_node(program);

    ctx->error_active = was_active;
    return 0;
}

static int run_repl(MKSContext *ctx) {
    char line[2048];
    int depth = 0;
    CliBuffer input = {0};
    Environment *env = mks_create_global_env(ctx);

    runtime_set_file("<repl>");
    printf("MKS %s REPL. Type :help for commands, Ctrl+D to exit.\n", MKS_VERSION);

    while (1) {
        fputs(depth > 0 || input.len > 0 ? "...> " : "mks> ", stdout);
        fflush(stdout);

        if (fgets(line, sizeof(line), stdin) == NULL) {
            break;
        }

        char *trimmed = line;
        while (*trimmed == ' ' || *trimmed == '\t') {
            trimmed++;
        }

        if (input.len == 0 && trimmed[0] == ':') {
            if (strncmp(trimmed, ":quit", 5) == 0 || strncmp(trimmed, ":q", 2) == 0) {
                break;
            }
            if (strncmp(trimmed, ":clear", 6) == 0) {
                cli_buffer_clear(&input);
                depth = 0;
                continue;
            }
            if (strncmp(trimmed, ":help", 5) == 0) {
                printf(":help   show REPL commands\n");
                printf(":clear  clear current buffered input\n");
                printf(":quit   exit REPL\n");
                continue;
            }
            printf("unknown REPL command; type :help\n");
            continue;
        }

        if (input.len == 0 && trimmed[0] == '\n') {
            continue;
        }

        if (!cli_buffer_append(&input, line)) {
            gc_pop_env();
            return 1;
        }
        depth += repl_depth_delta(line);

        if (!source_complete(&input, depth)) {
            continue;
        }

        (void)eval_repl_source(ctx, env, input.data);
        cli_buffer_clear(&input);
        depth = 0;
    }

    cli_buffer_free(&input);
    gc_collect(env, env);
    gc_pop_env();
    return 0;
}

static int check_source(MKSContext *ctx, const char *path, char *source) {
    const int was_active = ctx->error_active;
    if (setjmp(ctx->error_jmp) != 0) {
        ctx->error_active = was_active;
        free(source);
        return 1;
    }
    ctx->error_active = 1;
    ctx->error_status = 0;

    runtime_set_file(path);
    runtime_set_source(source);

    struct Lexer lexer;
    Token_init(&lexer, source);
    Parser parser;
    parser_init(&parser, &lexer);
    ASTNode *program = parser_parse_program(&parser);
    delete_ast_node(program);

    ctx->error_active = was_active;
    free(source);
    printf("ok: %s\n", path);
    return 0;
}

static int check_file(MKSContext *ctx, const char *path) {
    char *source = mks_read_file(path);
    if (source == NULL) {
        return 1;
    }
    return check_source(ctx, path, source);
}

int main(const int argc, char **argv) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    if (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "version") == 0) {
        printf("mks %s\n", MKS_VERSION);
        return 0;
    }

    if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "help") == 0) {
        print_usage(argv[0]);
        return 0;
    }

    if (strcmp(argv[1], "pkg") == 0) {
        return cmd_pkg(argc, argv);
    }

    MKSContext root_context;
    mks_context_init(&root_context, 1024 * 1024);
    mks_context_set_cli_args(&root_context, argc, argv);

    const char *gc_debug = getenv("MKS_GC_DEBUG");
    if (gc_debug != NULL && strcmp(gc_debug, "1") == 0) {
        gc_set_debug(1);
        fprintf(stderr, "[MKS] GC debug enabled\n");
    }

    int status = 0;
    if (strcmp(argv[1], "repl") == 0 || strcmp(argv[1], "--repl") == 0) {
        status = run_repl(&root_context);
    } else if (strcmp(argv[1], "check") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Usage: %s check <file.mks>\n", argv[0]);
            status = 1;
        } else {
            status = check_file(&root_context, argv[2]);
        }
    } else if (strcmp(argv[1], "--profile") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Usage: %s --profile <file.mks>\n", argv[0]);
            status = 1;
        } else {
            status = mks_run_file(&root_context, argv[2], 1, 1);
        }
    } else {
        status = mks_run_file(&root_context, argv[1], 1, 0);
    }

    mks_context_dispose(&root_context);
    return status;
}
