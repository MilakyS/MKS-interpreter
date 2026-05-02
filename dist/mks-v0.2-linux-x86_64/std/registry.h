#ifndef MKS_STD_REGISTRY_H
#define MKS_STD_REGISTRY_H

#include "../Runtime/module.h"

/* Initialize std registry (sets native init pointers). */
void std_registry_init(void);

/* Lookup std descriptor by normalized module id, or NULL. */
const ModuleDescriptor *std_registry_lookup(const char *module_id);

#endif
