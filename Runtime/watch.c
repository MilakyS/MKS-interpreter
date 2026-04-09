#include "watch.h"
#include "../Eval/eval.h"
#include <string.h>
#include <stdlib.h>

typedef struct WatchHandler {
    char *name;
    unsigned int hash;
    const ASTNode *body;
    Environment *env;
    struct WatchHandler *next;
} WatchHandler;

static WatchHandler *watch_head = NULL;

static char *watch_strdup(const char *s) {
    if (s == NULL) return NULL;
    size_t len = strlen(s);
    char *out = malloc(len + 1);
    if (!out) return NULL;
    memcpy(out, s, len + 1);
    return out;
}

void watch_register(const char *name, unsigned int hash) {
    WatchHandler *wh = malloc(sizeof(WatchHandler));
    if (!wh) return;
    wh->name = watch_strdup(name);
    wh->hash = hash;
    wh->body = NULL;
    wh->env = NULL;
    wh->next = watch_head;
    watch_head = wh;
}

void watch_register_handler(const char *name, unsigned int hash, const ASTNode *body, Environment *env) {
    WatchHandler *wh = malloc(sizeof(WatchHandler));
    if (!wh) return;
    wh->name = watch_strdup(name);
    wh->hash = hash;
    wh->body = body;
    wh->env = env;
    wh->next = watch_head;
    watch_head = wh;
}

void watch_trigger(const char *name, unsigned int hash, Environment *env) {
    WatchHandler *cur = watch_head;
    while (cur) {
        if (cur->hash == hash && strcmp(cur->name, name) == 0 && cur->body != NULL) {
            eval(cur->body, cur->env ? cur->env : env);
        }
        cur = cur->next;
    }
}

void watch_clear_all(void) {
    WatchHandler *cur = watch_head;
    while (cur) {
        WatchHandler *next = cur->next;
        free(cur->name);
        free(cur);
        cur = next;
    }
    watch_head = NULL;
}
