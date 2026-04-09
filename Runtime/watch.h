#ifndef MKS_WATCH_H
#define MKS_WATCH_H

#include "../Parser/AST.h"
#include "../env/env.h"

void watch_register(const char *name, unsigned int hash);
void watch_register_handler(const char *name, unsigned int hash, const ASTNode *body, Environment *env);
void watch_trigger(const char *name, unsigned int hash, Environment *env);
void watch_clear_all(void);

#endif
