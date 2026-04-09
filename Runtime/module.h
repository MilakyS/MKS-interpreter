#ifndef MKS_RUNTIME_MODULE_H
#define MKS_RUNTIME_MODULE_H

#include "../env/env.h"

void module_init(void);
void module_free_all(void);
int module_eval_file(const char *path, Environment *env);

#endif /* MKS_RUNTIME_MODULE_H */
