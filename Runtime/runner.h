#ifndef MKS_RUNNER_H
#define MKS_RUNNER_H

#include "context.h"
#include "../env/env.h"

enum {
    MKS_VM_AUTO = 0,
    MKS_VM_FORCE = 1,
    MKS_VM_TREE = 2
};

void mks_register_builtins(MKSContext *ctx, Environment *env);
Environment *mks_create_global_env(MKSContext *ctx);
int mks_run_source(MKSContext *ctx, const char *name, const char *source, int call_main, int profile);
int mks_run_file(MKSContext *ctx, const char *path, int call_main, int profile);

#endif
