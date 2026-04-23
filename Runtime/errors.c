#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "errors.h"
#include "context.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

static char *runtime_strdup(const char *src) {
    if (src == NULL) {
        src = "";
    }

    const size_t len = strlen(src);
    char *copy = (char *)malloc(len + 1);
    if (copy == NULL) {
        fprintf(stderr, "Fatal: out of memory while saving runtime file context\n");
        exit(1);
    }

    memcpy(copy, src, len + 1);
    return copy;
}

static const MksDiagnosticInfo diagnostic_table[] = {
    {
        MKS_ERR_LEXER_INVALID_TOKEN,
        "MKS-L0001",
        "lexical syntax",
        "the lexer produced a token that is not valid MKS syntax",
        "check the highlighted character or token",
        "var name =: \"text\";",
        "fix the invalid character first; parser errors after it may disappear"
    },
    {
        MKS_ERR_SYNTAX_GENERIC,
        "MKS-S1000",
        "syntax",
        "the parser could not match the current token to any valid grammar rule",
        "check the token under the caret and the nearest missing ';', ')', ']' or '<-'",
        "check the syntax near the caret and compare it with the examples in docs/REFERENCE.md",
        "fix the highlighted token first; later parser errors may disappear after that"
    },
    {
        MKS_ERR_SYNTAX_UNEXPECTED_TOKEN,
        "MKS-S1001",
        "syntax",
        "an expression cannot start with the token highlighted below",
        "start the expression with a number, string, identifier, array, '(' or unary '-'",
        "var name =: 123;  Writeln(name);",
        "look immediately before the highlighted token for the missing expression or argument"
    },
    {
        MKS_ERR_SYNTAX_EXPECTED_TOKEN,
        "MKS-S1002",
        "syntax",
        "this grammar position only accepts the expected token, but another token was found",
        "insert the expected token or remove the unexpected token under the caret",
        "if (condition) -> Writeln(\"ok\"); <-",
        "compare the line with the expected token shown in the error message"
    },
    {
        MKS_ERR_SYNTAX_INVALID_ASSIGN,
        "MKS-S1003",
        "syntax",
        "the left side of an assignment must be a writable place",
        "only identifiers, object fields, and indexed values can be assigned",
        "value =: 10;  items[0] =: 10;  obj.name =: \"mks\";",
        "rewrite the left side so it names a variable, object field, or indexed slot"
    },
    {
        MKS_ERR_RUNTIME_GENERIC,
        "MKS-R3000",
        "runtime",
        "the program reached a runtime state that MKS cannot continue from",
        "check the highlighted line and the value types used there",
        NULL,
        "start from the expression under the caret, then inspect the values used by it"
    },
    {
        MKS_ERR_RUNTIME_UNDEFINED_NAME,
        "MKS-R3001",
        "name resolution",
        "MKS tried to read or assign a name that is not visible in the current scope",
        "declare the variable before using it, or check the spelling/scope",
        "var value =: 1; Writeln(value);",
        "look for a missing 'var', a typo, or a variable declared inside another block"
    },
    {
        MKS_ERR_RUNTIME_CALL_ARITY,
        "MKS-R3101",
        "call arity",
        "the call site passes a different number of arguments than the function declares",
        "check the function or method signature and the number of arguments",
        "fnc add(a, b) -> return a + b; <-  var x =: add(1, 2);",
        "either pass the missing arguments or change the function parameter list"
    },
    {
        MKS_ERR_RUNTIME_CALL_TARGET,
        "MKS-R3102",
        "call target",
        "the selected value is not callable, or this type does not provide the requested method",
        "check that the value is callable and the method name exists for this type",
        "var s =: \"abc\"; Writeln(s.len());",
        "verify the method spelling and the type of the value before the dot"
    },
    {
        MKS_ERR_RUNTIME_INDEX_BOUNDS,
        "MKS-R3201",
        "indexing",
        "the index points outside the valid range of the array or string",
        "check the index value and the array/string length before accessing it",
        "if (i >= 0 && i < items.len()) -> Writeln(items[i]); <-",
        "print or guard the index before indexing"
    },
    {
        MKS_ERR_RUNTIME_NUMERIC,
        "MKS-R3301",
        "numeric operation",
        "division and modulo require a non-zero right operand",
        "guard the divisor so it cannot be zero",
        "if (divisor != 0) -> Writeln(value / divisor); <-",
        "check where the divisor is computed and add a zero guard"
    },
    {
        MKS_ERR_RUNTIME_OBJECT_ACCESS,
        "MKS-R3401",
        "object access",
        "property access only works on object values",
        "make sure the left side is an object before reading or writing a property",
        "var obj =: Object(); obj.name =: \"mks\";",
        "check the expression before the dot and ensure it creates or returns an object"
    },
    {
        MKS_ERR_RUNTIME_TYPE_MISMATCH,
        "MKS-R3501",
        "type mismatch",
        "the operator does not support the two runtime value types it received",
        "convert values to compatible types before applying this operator",
        "var text =: \"count: \" + value;  var sum =: left + right;",
        "check both operands and use a compatible operator or conversion"
    },
    {
        MKS_ERR_RUNTIME_CONVERSION,
        "MKS-R3502",
        "conversion",
        "the requested value conversion is not defined for this input",
        "pass a value that the conversion function accepts, or validate the string before converting",
        "var n =: Int(\"42\");  var s =: String(n);",
        "check the source value and the exact conversion function call"
    },
    {
        MKS_ERR_MODULE_RESOLUTION,
        "MKS-M4001",
        "module resolution",
        "the module loader could not map the import name or path to a readable file/module",
        "check the module path, current working directory, and installed std files",
        "using std.math as math;",
        "verify the module id, file path, and whether the module exists under std or the project"
    },
    {
        MKS_ERR_INTERNAL_PARSER,
        "MKS-I9001",
        "internal parser",
        "parser state was missing, so no exact source location is available",
        "check overall file/block structure",
        NULL,
        "rerun after fixing the first visible syntax error"
    }
};

const MksDiagnosticInfo *mks_diagnostic_info(MksErrorCode code) {
    const size_t count = sizeof(diagnostic_table) / sizeof(diagnostic_table[0]);
    for (size_t i = 0; i < count; i++) {
        if (diagnostic_table[i].id == code) {
            return &diagnostic_table[i];
        }
    }
    return mks_diagnostic_info(MKS_ERR_RUNTIME_GENERIC);
}

void runtime_set_file(const char *file_path) {
    MKSContext *ctx = mks_context_current();
    if (file_path == NULL) {
        ctx->current_file[0] = '\0';
        return;
    }

    if (ctx->current_file == file_path) {
        return;
    }

    size_t len = strnlen(file_path, sizeof(ctx->current_file) - 1);
    memcpy(ctx->current_file, file_path, len);
    ctx->current_file[len] = '\0';
}

const char *runtime_current_file(void) {
    MKSContext *ctx = mks_context_current();
    return ctx->current_file[0] ? ctx->current_file : NULL;
}

void runtime_set_source(const char *source) {
    mks_context_current()->current_source = source;
}

const char *runtime_current_source(void) {
    return mks_context_current()->current_source;
}

void runtime_set_line(const int line) {
    mks_context_current()->current_line = line;
}

int runtime_current_line(void) {
    return mks_context_current()->current_line;
}

const char *runtime_push_file(const char *file_path) {
    char *prev = runtime_strdup(runtime_current_file());
    runtime_set_file(file_path);
    return prev;
}

void runtime_pop_file(const char *previous_file) {
    runtime_set_file(previous_file);
    free((void *)previous_file);
}

const char *runtime_push_source(const char *source) {
    const char *prev = runtime_current_source();
    runtime_set_source(source);
    return prev;
}

void runtime_pop_source(const char *previous_source) {
    runtime_set_source(previous_source);
}

static MksErrorCode runtime_error_code_for(const char *message) {
    if (message == NULL) {
        return MKS_ERR_RUNTIME_GENERIC;
    }
    if (strstr(message, "Undefined variable") != NULL ||
        strstr(message, "not defined") != NULL) {
        return MKS_ERR_RUNTIME_UNDEFINED_NAME;
    }
    if (strstr(message, "expects") != NULL && strstr(message, "arguments") != NULL) {
        return MKS_ERR_RUNTIME_CALL_ARITY;
    }
    if (strstr(message, "not a function") != NULL ||
        strstr(message, "Method") != NULL) {
        return MKS_ERR_RUNTIME_CALL_TARGET;
    }
    if (strstr(message, "out of bounds") != NULL ||
        strstr(message, "index") != NULL) {
        return MKS_ERR_RUNTIME_INDEX_BOUNDS;
    }
    if (strstr(message, "division by zero") != NULL ||
        strstr(message, "modulo by zero") != NULL) {
        return MKS_ERR_RUNTIME_NUMERIC;
    }
    if (strstr(message, "non-object") != NULL) {
        return MKS_ERR_RUNTIME_OBJECT_ACCESS;
    }
    if (strstr(message, "unsupported operand") != NULL) {
        return MKS_ERR_RUNTIME_TYPE_MISMATCH;
    }
    if (strstr(message, "cannot convert") != NULL ||
        strstr(message, "cannot parse") != NULL) {
        return MKS_ERR_RUNTIME_CONVERSION;
    }
    if (strstr(message, "Cannot resolve module") != NULL ||
        strstr(message, "Could not read file") != NULL) {
        return MKS_ERR_MODULE_RESOLUTION;
    }
    return MKS_ERR_RUNTIME_GENERIC;
}

static const char *find_line_start(const char *source, int target_line) {
    const char *p = source;
    int line = 1;

    while (*p != '\0' && line < target_line) {
        if (*p == '\n') {
            line++;
        }
        p++;
    }

    return (line == target_line) ? p : NULL;
}

static int infer_column_from_message(const char *source, int line, const char *message, int *out_len) {
    if (source == NULL || message == NULL || line <= 0) {
        return 0;
    }

    const char *quoted = strchr(message, '\'');
    if (quoted == NULL) {
        return 0;
    }
    const char *quoted_end = strchr(quoted + 1, '\'');
    if (quoted_end == NULL || quoted_end == quoted + 1) {
        return 0;
    }

    const char *line_start = find_line_start(source, line);
    if (line_start == NULL) {
        return 0;
    }

    const char *line_end = line_start;
    while (*line_end != '\0' && *line_end != '\n' && *line_end != '\r') {
        line_end++;
    }

    const int needle_len = (int)(quoted_end - quoted - 1);
    for (const char *p = line_start; p + needle_len <= line_end; p++) {
        if (strncmp(p, quoted + 1, (size_t)needle_len) == 0) {
            if (out_len != NULL) {
                *out_len = needle_len;
            }
            return (int)(p - line_start) + 1;
        }
    }

    return 0;
}

static void print_source_line(int line_no, const char *line_start) {
    if (line_start == NULL) {
        return;
    }

    const char *line_end = line_start;
    while (*line_end != '\0' && *line_end != '\n' && *line_end != '\r') {
        line_end++;
    }

    fprintf(stderr, "%4d | %.*s\n", line_no, (int)(line_end - line_start), line_start);
}

void runtime_print_source_context(const char *file,
                                  const char *source,
                                  int line,
                                  int column,
                                  int length) {
    if (source == NULL || line <= 0) {
        return;
    }

    const char *line_start = find_line_start(source, line);
    if (line_start == NULL) {
        return;
    }

    const char *line_end = line_start;
    while (*line_end != '\0' && *line_end != '\n' && *line_end != '\r') {
        line_end++;
    }

    int line_len = (int)(line_end - line_start);
    if (line_len < 0) {
        line_len = 0;
    }

    if (column <= 0) {
        column = 1;
        while (column <= line_len &&
               (line_start[column - 1] == ' ' || line_start[column - 1] == '\t')) {
            column++;
        }
    }
    if (column > line_len + 1) {
        column = line_len + 1;
    }
    if (length <= 0) {
        length = 1;
    }

    const char *safe_file = file != NULL ? file : "<source>";
    fprintf(stderr, " --> %s:%d:%d\n", safe_file, line, column);

    if (line > 1) {
        const char *prev_line = find_line_start(source, line - 1);
        if (prev_line != NULL) {
            print_source_line(line - 1, prev_line);
        }
    }

    fprintf(stderr, "%4d | %.*s\n", line, line_len, line_start);
    fprintf(stderr, "     | ");

    for (int i = 1; i < column; i++) {
        fputc(line_start[i - 1] == '\t' ? '\t' : ' ', stderr);
    }
    fputc('^', stderr);
    for (int i = 1; i < length && column + i <= line_len; i++) {
        fputc('~', stderr);
    }
    fputc('\n', stderr);

    const char *next_line = find_line_start(source, line + 1);
    if (next_line != NULL && *next_line != '\0') {
        print_source_line(line + 1, next_line);
    }
}

static void vreport(const char *file, int line, const char *fmt, va_list args) {
    char message[1024];
    vsnprintf(message, sizeof(message), fmt, args);
    const MksDiagnosticInfo *info = mks_diagnostic_info(runtime_error_code_for(message));
    int column = 0;
    int length = 1;
    column = infer_column_from_message(runtime_current_source(), line, message, &length);

    fprintf(stderr, "\n[MKS Runtime Error]\n");
    fprintf(stderr, "code: %s\n", info->code);
    fprintf(stderr, "kind: %s\n", info->kind);
    if (file != NULL && line > 0) {
        fprintf(stderr, "error: %s\n", message);
        fprintf(stderr, "reason: %s\n", info->reason);
        runtime_print_source_context(file, runtime_current_source(), line, column, length);
    } else {
        fprintf(stderr, "error: %s\n", message);
        fprintf(stderr, "reason: %s\n", info->reason);
    }
    fprintf(stderr, "help: %s\n", info->help);
    if (info->example != NULL) {
        fprintf(stderr, "example: %s\n", info->example);
    }
    if (info->next != NULL) {
        fprintf(stderr, "next: %s\n", info->next);
    }
}

void runtime_error_at(const char *file, const int line, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vreport(file, line, fmt, args);
    va_end(args);
    mks_context_abort(1);
}

void runtime_error(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vreport(runtime_current_file(), runtime_current_line(), fmt, args);
    va_end(args);
    mks_context_abort(1);
}
