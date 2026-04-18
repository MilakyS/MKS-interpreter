#ifndef MKS_RUNTIME_ERRORS_H
#define MKS_RUNTIME_ERRORS_H

#include <stdarg.h>

/*
 * Diagnostic code ranges:
 *   MKS-L0000..L0999  Lexer diagnostics
 *   MKS-S1000..S1999  Parser/syntax diagnostics
 *   MKS-T2000..T2999  Static type diagnostics (reserved)
 *   MKS-R3000..R3999  Runtime value/control-flow diagnostics
 *   MKS-M4000..M4999  Module/import/export diagnostics
 *   MKS-G5000..G5999  GC/runtime-internal diagnostics (reserved)
 *   MKS-I9000..I9999  Internal compiler/interpreter diagnostics
 *
 * The last three digits are stable IDs inside the subsystem range:
 *   x000 = generic fallback for the subsystem
 *   x001..x099 = first concrete diagnostics in that subsystem/group
 */
typedef enum MksErrorCode {
    MKS_ERR_LEXER_INVALID_TOKEN       = 1,    /* MKS-L0001 */

    MKS_ERR_SYNTAX_GENERIC           = 1000, /* MKS-S1000 */
    MKS_ERR_SYNTAX_UNEXPECTED_TOKEN  = 1001, /* MKS-S1001 */
    MKS_ERR_SYNTAX_EXPECTED_TOKEN    = 1002, /* MKS-S1002 */
    MKS_ERR_SYNTAX_INVALID_ASSIGN    = 1003, /* MKS-S1003 */

    MKS_ERR_RUNTIME_GENERIC          = 3000, /* MKS-R3000 */
    MKS_ERR_RUNTIME_UNDEFINED_NAME   = 3001, /* MKS-R3001 */
    MKS_ERR_RUNTIME_CALL_ARITY       = 3101, /* MKS-R3101 */
    MKS_ERR_RUNTIME_CALL_TARGET      = 3102, /* MKS-R3102 */
    MKS_ERR_RUNTIME_INDEX_BOUNDS     = 3201, /* MKS-R3201 */
    MKS_ERR_RUNTIME_NUMERIC          = 3301, /* MKS-R3301 */
    MKS_ERR_RUNTIME_OBJECT_ACCESS    = 3401, /* MKS-R3401 */
    MKS_ERR_RUNTIME_TYPE_MISMATCH    = 3501, /* MKS-R3501 */
    MKS_ERR_RUNTIME_CONVERSION       = 3502, /* MKS-R3502 */

    MKS_ERR_MODULE_RESOLUTION        = 4001, /* MKS-M4001 */

    MKS_ERR_INTERNAL_PARSER          = 9001  /* MKS-I9001 */
} MksErrorCode;

typedef struct MksDiagnosticInfo {
    MksErrorCode id;
    const char *code;
    const char *kind;
    const char *reason;
    const char *help;
    const char *example;
    const char *next;
} MksDiagnosticInfo;

const MksDiagnosticInfo *mks_diagnostic_info(MksErrorCode code);

void runtime_set_file(const char *file_path);
const char *runtime_current_file(void);

void runtime_set_source(const char *source);
const char *runtime_current_source(void);

void runtime_set_line(int line);
int runtime_current_line(void);

const char *runtime_push_file(const char *file_path);
void runtime_pop_file(const char *previous_file);
const char *runtime_push_source(const char *source);
void runtime_pop_source(const char *previous_source);

void runtime_print_source_context(const char *file,
                                  const char *source,
                                  int line,
                                  int column,
                                  int length);

void runtime_error(const char *fmt, ...);
void runtime_error_at(const char *file, int line, const char *fmt, ...);

#endif /* MKS_RUNTIME_ERRORS_H */
