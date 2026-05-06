#ifndef MKS_VM_COMPILE_SCOPE_H
#define MKS_VM_COMPILE_SCOPE_H

#include "vm_compile_internal.h"

int compiler_name_matches(const char *name_a,
                          unsigned int hash_a,
                          const char *name_b,
                          unsigned int hash_b);

int compiler_alloc_local_slot(Compiler *compiler);

int compiler_can_alloc_local(const Compiler *compiler);

LocalEntry *compiler_lookup_local(Compiler *compiler, const char *name, unsigned int hash);

LocalEntry *compiler_add_local(Compiler *compiler, const char *name, unsigned int hash);

int compiler_can_use_fast_locals(const Compiler *compiler);

int compiler_can_cache_import_alias(const Compiler *compiler,
                                    const char *name,
                                    unsigned int hash);

int ast_name_must_remain_env_backed(Compiler *compiler, const ASTNode *decl_node);

int ast_name_must_remain_env_backed_in_node(Compiler *compiler,
                                            const ASTNode *decl_node,
                                            const ASTNode *scope_node);

void compiler_seed_function_param_locals(Compiler *compiler, VMFunction *function);

void compiler_seed_method_self_local(Compiler *compiler, VMFunction *function);

void compiler_preseed_import_aliases(Compiler *compiler);

#endif
