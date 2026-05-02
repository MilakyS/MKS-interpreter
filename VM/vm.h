#ifndef MKS_VM_H
#define MKS_VM_H

#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>

#include "../Parser/AST.h"
#include "../env/env.h"
#include "../Runtime/value.h"

typedef enum {
    OP_CONSTANT,
    OP_NULL,
    OP_DEFINE_FUNCTION,
    OP_DEFINE_BLUEPRINT,
    OP_REGISTER_EXTENSION,
    OP_DEFINE_GLOBAL,
    OP_EXPORT_GLOBAL,
    OP_DEFINE_LOCAL,
    OP_GET_GLOBAL,
    OP_GET_LOCAL,
    OP_SET_GLOBAL,
    OP_SET_LOCAL,
    OP_INC_LOCAL,
    OP_ADD_LOCAL_CONST,
    OP_SUB_LOCAL_CONST,
    OP_MUL_LOCAL_CONST,
    OP_DIV_LOCAL_CONST,
    OP_ADD_LOCAL_LOCAL,
    OP_STRING_APPEND_LOCAL_CONST,
    OP_BUILDER_START_LOCAL,
    OP_BUILDER_APPEND_LOCAL_CONST,
    OP_BUILDER_APPEND_LOCAL_VALUE,
    OP_BUILDER_FINISH_LOCAL,
    OP_JUMP_IF_LOCAL_LT_CONST_FALSE,
    OP_ADD,
    OP_SUB,
    OP_MUL,
    OP_DIV,
    OP_MOD,
    OP_EQ,
    OP_NEQ,
    OP_LT,
    OP_GT,
    OP_LE,
    OP_GE,
    OP_JUMP,
    OP_JUMP_IF_FALSE,
    OP_JUMP_IF_TRUE,
    OP_LOOP,
    OP_PUSH_ENV,
    OP_POP_ENV,
    OP_PUSH_DEFER_SCOPE,
    OP_ADD_DEFER,
    OP_POP_DEFER_SCOPE,
    OP_ARRAY_NEW,
    OP_GET_INDEX,
    OP_SET_INDEX,
    OP_GET_FIELD,
    OP_SET_FIELD,
    OP_TRUTHY,
    OP_WATCH,
    OP_WATCH_HANDLER,
    OP_ADDRESS_OF_VAR,
    OP_ADDRESS_OF_INDEX,
    OP_ADDRESS_OF_FIELD,
    OP_DEREF,
    OP_DEREF_ASSIGN,
    OP_SWAP_PTRS,
    OP_IMPORT,
    OP_CALL,
    OP_CALL_METHOD,
    OP_CALL_NATIVE,
    OP_TEST_PASS,
    OP_POP,
    OP_RETURN
} OpCode;

typedef struct Chunk Chunk;

typedef struct VMFunction {
    const ASTNode *decl_node;
    const char *debug_name;
    int arity;
    int local_count;
    int param_local_slots[64];
    int self_local_slot;
    Chunk *chunk;
} VMFunction;

struct Chunk {
    uint8_t *code;
    int count;
    int capacity;

    RuntimeValue *constants;
    int constants_count;
    int constants_capacity;

    VMFunction *functions;
    int function_count;
    int function_capacity;

    const ASTNode **ast_refs;
    int ast_ref_count;
    int ast_ref_capacity;
    const char *debug_name;
};

typedef struct VMCallFrame {
    Chunk *chunk;
    uint8_t *ip;
    Environment *base_env;
    int owns_base_env;
    RuntimeValue locals[64];
    Environment *scope_envs[64];
    int scope_depth;
    struct {
        const ASTNode *body;
        Environment *env;
    } defer_items[256];
    int defer_scope_starts[64];
    int defer_scope_depth;
    int defer_count;
} VMCallFrame;

typedef struct {
    RuntimeValue stack[1024];
    RuntimeValue *sp;

    Chunk *root_chunk;
    Environment *global_env;
    VMCallFrame frames[64];
    int frame_count;
    int gc_tick;
} VM;

void chunk_init(Chunk *chunk);
void chunk_free(Chunk *chunk);

bool compiler_can_compile(ASTNode *node);
bool compile_program(ASTNode *node, Chunk *out);
bool compile_script_program(ASTNode *node, Chunk *out);

void vm_init(VM *vm, Chunk *root_chunk, Environment *env);
RuntimeValue vm_run(VM *vm);
RuntimeValue vm_call_named(Chunk *root_chunk,
                           Environment *env,
                           const char *name,
                           const RuntimeValue *args,
                           int arg_count);
int vm_has_compiled_function(const ASTNode *decl);
RuntimeValue vm_call_function_value(RuntimeValue callable,
                                    const RuntimeValue *args,
                                    int arg_count,
                                    const RuntimeValue *self_value);
RuntimeValue vm_run_ast(Environment *env, ASTNode *node);
RuntimeValue vm_run_compiled_ast_body(Environment *env, ASTNode *node);
void vm_register_owned_chunk(Chunk *chunk);
void vm_free_all(void);
void vm_dump_program(const Chunk *chunk, FILE *out);
const char *vm_opcode_name(OpCode op);

#endif
