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

typedef struct ImportNode {
    char *path;
    struct ImportNode *next;
} ImportNode;

static ImportNode *imports = NULL;
typedef struct ProgramNode {
    ASTNode *program;
    struct ProgramNode *next;
} ProgramNode;

static ProgramNode *programs = NULL;
static char exec_dir[PATH_MAX] = "";
static int exec_dir_inited = 0;

static int try_realpath(const char *candidate, char *out);

static const char *get_exec_dir(void) {
    if (exec_dir_inited) {
        return exec_dir;
    }

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

static int try_base_join(const char *base, const char *with_ext, char *out) {
    if (base == NULL || base[0] == '\0') {
        return 0;
    }
    char tmp_out[PATH_MAX];
    if (snprintf(tmp_out, PATH_MAX, "%s/%s", base, with_ext) >= PATH_MAX) {
        return 0;
    }
    if (try_realpath(tmp_out, out)) {
        return 1;
    }
    return 0;
}

static int already_imported(const char *abs_path) {
    for (ImportNode *n = imports; n != NULL; n = n->next) {
        if (strcmp(n->path, abs_path) == 0) {
            return 1;
        }
    }
    return 0;
}

static void remember_import(const char *abs_path) {
    ImportNode *node = (ImportNode *)malloc(sizeof(ImportNode));
    if (node == NULL) {
        runtime_error("Out of memory while tracking imports");
    }
    node->path = strdup(abs_path);
    if (node->path == NULL) {
        free(node);
        runtime_error("Out of memory while tracking imports");
    }
    node->next = imports;
    imports = node;
}

void module_init(void) {
    imports = NULL;
    programs = NULL;
}

void module_free_all(void) {
    ImportNode *node = imports;
    while (node != NULL) {
        ImportNode *next = node->next;
        free(node->path);
        free(node);
        node = next;
    }
    imports = NULL;

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
    if (candidate == NULL) {
        return 0;
    }
    if (realpath(candidate, out) != NULL) {
        return 1;
    }
    return 0;
}

static void append_ext_if_missing(const char *path, char *buffer, size_t buffer_size) {
    const char *dot = strrchr(path, '.');
    if (dot == NULL) {
        snprintf(buffer, buffer_size, "%s.mks", path);
    } else {
        snprintf(buffer, buffer_size, "%s", path);
    }
}

static int make_absolute_path(const char *path, char *out) {
    if (path == NULL) return 0;

    char with_ext[PATH_MAX];
    append_ext_if_missing(path, with_ext, sizeof(with_ext));
    const bool is_std = strncmp(path, "std/", 4) == 0 || strncmp(path, "std\\", 4) == 0;

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

    if (is_std) {
        const char *base = get_exec_dir();
        if (base && base[0]) {
            if (try_base_join(base, with_ext, out)) return 1;
            char parent[PATH_MAX];
            if (snprintf(parent, PATH_MAX, "%s/..", base) < PATH_MAX &&
                try_base_join(parent, with_ext, out)) return 1;
        }

        const char *env_std = getenv("MKS_STD_PATH");
        if (env_std && env_std[0] && try_base_join(env_std, with_ext, out)) return 1;

        static const char *system_std_paths[] = {"/usr/local/share/mks", "/usr/share/mks", NULL};
        for (int i = 0; system_std_paths[i] != NULL; i++) {
            if (try_base_join(system_std_paths[i], with_ext, out)) return 1;
        }
    }
    return 0;
}

int module_eval_file(const char *path, Environment *env){
    if (path == NULL) {
        runtime_error("Import path is null");
    }

    char abs_path[PATH_MAX];
    if (!make_absolute_path(path, abs_path)) {
        runtime_error("Cannot resolve path '%s'", path);
    }

    if (already_imported(abs_path)) {
        return 0;
    }

    char *source = mks_read_file(abs_path);
    if (source == NULL) {
        runtime_error("Could not read file '%s'", abs_path);
    }

    const char *prev_file = runtime_push_file(abs_path);

    struct Lexer lexer;
    Token_init(&lexer, source);

    Parser parser;
    parser_init(&parser, &lexer);

    ASTNode *program = parser_parse_program(&parser);

    remember_import(abs_path);

    if (program != NULL) {
        eval(program, env);
    }

    ProgramNode *pnode = (ProgramNode *)malloc(sizeof(ProgramNode));
    if (pnode == NULL) {
        runtime_error("Out of memory tracking module program");
    }
    pnode->program = program;
    pnode->next = programs;
    programs = pnode;
    free(source);

    runtime_pop_file(prev_file);
    return 0;
}
