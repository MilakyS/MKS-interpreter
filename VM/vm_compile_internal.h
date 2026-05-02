#ifndef MKS_VM_COMPILE_INTERNAL_H
#define MKS_VM_COMPILE_INTERNAL_H

#include "vm.h"

#define VM_MAX_LOCALS 64
#define VM_MAX_LOOP_JUMPS 128
#define VM_MAX_CALL_ARGS 255
#define VM_U16_SENTINEL 0xffff

typedef struct LoopContext {
    int continue_target;
    int defer_depth_at_entry;

    int break_jumps[VM_MAX_LOOP_JUMPS];
    int break_count;

    int continue_jumps[VM_MAX_LOOP_JUMPS];
    int continue_count;

    uint8_t builder_slot;
    int builder_active;

    struct LoopContext *prev;
} LoopContext;

typedef struct {
    const char *name;
    unsigned int hash;
    int slot;
} LocalEntry;

typedef struct Compiler {
    Chunk *chunk;
    const char *debug_name;

    struct Compiler *parent;

    const ASTNode *function_decl;
    const ASTNode *function_body;
    const ASTNode *program_root;

    int allow_root_locals;

    LoopContext *loop;
    int *hidden_counter;

    int next_local_slot;
    LocalEntry locals[VM_MAX_LOCALS];
    int local_count;

    int env_scope_depth;
    int defer_depth;
} Compiler;

#endif