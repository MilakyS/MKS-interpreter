#ifndef MKS_RUNTIME_ERRORS_H
#define MKS_RUNTIME_ERRORS_H

#include <stdarg.h>


void runtime_set_file(const char *file_path);
const char *runtime_current_file(void);

void runtime_set_line(int line);
int runtime_current_line(void);

const char *runtime_push_file(const char *file_path);
void runtime_pop_file(const char *previous_file);

void runtime_error(const char *fmt, ...);
void runtime_error_at(const char *file, int line, const char *fmt, ...);

#endif /* MKS_RUNTIME_ERRORS_H */
