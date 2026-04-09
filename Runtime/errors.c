#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "errors.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <stdarg.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

static char current_file[PATH_MAX] = "";
static int current_line = -1;

void runtime_set_file(const char *file_path) {
    if (file_path == NULL) {
        current_file[0] = '\0';
        return;
    }

    if (current_file == file_path) {
        return;
    }

    size_t len = strnlen(file_path, sizeof(current_file) - 1);
    memcpy(current_file, file_path, len);
    current_file[len] = '\0';
}

const char *runtime_current_file(void) {
    return current_file[0] ? current_file : NULL;
}

void runtime_set_line(const int line) {
    current_line = line;
}

int runtime_current_line(void) {
    return current_line;
}

const char *runtime_push_file(const char *file_path) {
    const char *prev = runtime_current_file();
    runtime_set_file(file_path);
    return prev;
}

void runtime_pop_file(const char *previous_file) {
    runtime_set_file(previous_file);
}

static void vreport(const char *file, int line, const char *fmt, va_list args) {
    fprintf(stderr, "\n[MKS Runtime Error]\n");
    if (file != NULL && line > 0) {
        fprintf(stderr, "%s:%d: ", file, line);
    }
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    fprintf(stderr, "Hint: check syntax/values near this line (missing ';', wrong type, or variable name)\n");
}

void runtime_error_at(const char *file, const int line, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vreport(file, line, fmt, args);
    va_end(args);
    exit(1);
}

void runtime_error(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vreport(runtime_current_file(), runtime_current_line(), fmt, args);
    va_end(args);
    exit(1);
}
