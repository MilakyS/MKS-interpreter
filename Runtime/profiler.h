#ifndef MKS_PROFILER_H
#define MKS_PROFILER_H

#include "../Parser/AST.h"

void profiler_enable(void);
int  profiler_is_enabled(void);
void profiler_on_eval(ASTNodeType type);
void profiler_report(void);

#endif
