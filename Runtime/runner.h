#ifndef MKS_RUNNER_H
#define MKS_RUNNER_H

#include "context.h"
#include "../env/env.h"

void mks_register_builtins(MKSContext *ctx, Environment *env);
Environment *mks_create_global_env(MKSContext *ctx);
int mks_run_source(MKSContext *ctx, const char *name, const char *source, int call_main, int profile);
int mks_run_file(MKSContext *ctx, const char *path, int call_main, int profile);

#endif
