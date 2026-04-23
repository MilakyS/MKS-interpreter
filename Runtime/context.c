#include "context.h"

#include "extension.h"
#include "module.h"
#include "../std/watch.h"

#include <string.h>
#include <stdlib.h>
#include <time.h>

static MKSContext default_context;
static _Thread_local MKSContext *current_context = &default_context;

MKSContext *mks_context_default(void) {
    return &default_context;
}

MKSContext *mks_context_current(void) {
    return current_context != NULL ? current_context : &default_context;
}

void mks_context_set_current(MKSContext *ctx) {
    current_context = ctx != NULL ? ctx : &default_context;
}

void mks_context_init(MKSContext *ctx, const size_t initial_gc_threshold) {
    if (ctx == NULL) {
        return;
    }

    memset(ctx, 0, sizeof(*ctx));
    ctx->current_line = -1;
    ctx->random_state = ((uint64_t)time(NULL) << 32) ^ (uint64_t)(uintptr_t)ctx;
    mks_context_set_current(ctx);
    gc_init(initial_gc_threshold);
}

void mks_context_dispose(MKSContext *ctx) {
    if (ctx == NULL) {
        return;
    }

    MKSContext *previous = mks_context_current();
    mks_context_set_current(ctx);

    module_free_all();
    extension_free_all();
    watch_clear_all();
    gc_free_all();

    if (previous != ctx) {
        mks_context_set_current(previous);
    }
}

void mks_context_abort(const int status) {
    MKSContext *ctx = mks_context_current();
    if (ctx->error_active) {
        ctx->error_status = status;
        longjmp(ctx->error_jmp, 1);
    }
    exit(status);
}
