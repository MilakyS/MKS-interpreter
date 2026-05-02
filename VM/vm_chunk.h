#ifndef MKS_VM_CHUNK_H
#define MKS_VM_CHUNK_H

#include "vm.h"

void chunk_write_byte(Chunk *chunk, uint8_t byte);
void chunk_write_u16(Chunk *chunk, uint16_t value);

int chunk_emit_jump(Chunk *chunk, OpCode op);
void chunk_patch_jump(Chunk *chunk, int operand_pos);
void chunk_patch_jump_to(Chunk *chunk, int operand_pos, int target);
void chunk_emit_loop(Chunk *chunk, int loop_start);

int chunk_add_constant(Chunk *chunk, RuntimeValue value);
VMFunction *chunk_add_function(Chunk *chunk);
int chunk_add_ast_ref(Chunk *chunk, const ASTNode *node);

#endif