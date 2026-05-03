#include "vm_chunk.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "../Runtime/errors.h"

static void chunk_reserve_code(Chunk *chunk, int extra) {
    if (chunk->count + extra <= chunk->capacity) {
        return;
    }

    int new_capacity = chunk->capacity == 0 ? 64 : chunk->capacity * 2;
    while (new_capacity < chunk->count + extra) {
        new_capacity *= 2;
    }

    uint8_t *new_code = (uint8_t *)realloc(chunk->code, (size_t)new_capacity);
    if (new_code == NULL) {
        runtime_error("Out of memory growing VM bytecode");
    }

    chunk->code = new_code;
    chunk->capacity = new_capacity;
}

void chunk_write_byte(Chunk *chunk, uint8_t byte) {
    chunk_reserve_code(chunk, 1);
    chunk->code[chunk->count++] = byte;
}

void chunk_write_u16(Chunk *chunk, uint16_t value) {
    chunk_write_byte(chunk, (uint8_t)((value >> 8) & 0xff));
    chunk_write_byte(chunk, (uint8_t)(value & 0xff));
}

int chunk_emit_jump(Chunk *chunk, OpCode op) {
    chunk_write_byte(chunk, (uint8_t)op);

    const int operand_pos = chunk->count;
    chunk_write_u16(chunk, 0);

    return operand_pos;
}

void chunk_patch_jump(Chunk *chunk, int operand_pos) {
    const int offset = chunk->count - (operand_pos + 2);
    if (offset < 0 || offset > 0xffff) {
        runtime_error("VM jump offset out of range");
    }

    chunk->code[operand_pos] = (uint8_t)((offset >> 8) & 0xff);
    chunk->code[operand_pos + 1] = (uint8_t)(offset & 0xff);
}

void chunk_patch_jump_to(Chunk *chunk, int operand_pos, int target) {
    const int offset = target - (operand_pos + 2);
    if (offset < 0 || offset > 0xffff) {
        runtime_error("VM jump target out of range");
    }

    chunk->code[operand_pos] = (uint8_t)((offset >> 8) & 0xff);
    chunk->code[operand_pos + 1] = (uint8_t)(offset & 0xff);
}

void chunk_emit_loop(Chunk *chunk, int loop_start) {
    chunk_write_byte(chunk, OP_LOOP);

    const int offset = chunk->count + 2 - loop_start;
    if (offset <= 0 || offset > 0xffff) {
        runtime_error("VM loop offset out of range");
    }

    chunk_write_u16(chunk, (uint16_t)offset);
}

void chunk_init(Chunk *chunk) {
    if (chunk == NULL) {
        return;
    }

    chunk->code = NULL;
    chunk->count = 0;
    chunk->capacity = 0;

    chunk->constants = NULL;
    chunk->constants_count = 0;
    chunk->constants_capacity = 0;

    chunk->functions = NULL;
    chunk->function_count = 0;
    chunk->function_capacity = 0;

    chunk->ast_refs = NULL;
    chunk->ast_ref_count = 0;
    chunk->ast_ref_capacity = 0;

    chunk->debug_name = "<chunk>";

    chunk->slot_predictions = NULL;
    chunk->slot_predict_count = 0;
}

void chunk_free(Chunk *chunk) {
    if (chunk == NULL) {
        return;
    }

    for (int i = 0; i < chunk->function_count; i++) {
        chunk_free(chunk->functions[i].chunk);
        free(chunk->functions[i].chunk);
    }

    free(chunk->functions);
    free(chunk->ast_refs);
    free(chunk->constants);
    free(chunk->code);
    free(chunk->slot_predictions);

    chunk_init(chunk);
}

int chunk_add_constant(Chunk *chunk, RuntimeValue value) {
    if (chunk->constants_count >= chunk->constants_capacity) {
        int new_capacity = chunk->constants_capacity == 0
            ? 16
            : chunk->constants_capacity * 2;

        RuntimeValue *new_constants = (RuntimeValue *)realloc(
            chunk->constants,
            sizeof(RuntimeValue) * (size_t)new_capacity
        );

        if (new_constants == NULL) {
            runtime_error("Out of memory growing VM constants");
        }

        chunk->constants = new_constants;
        chunk->constants_capacity = new_capacity;
    }

    if (chunk->constants_count >= 0xffff) {
        runtime_error("Too many VM constants");
    }

    chunk->constants[chunk->constants_count] = value;
    return chunk->constants_count++;
}

VMFunction *chunk_add_function(Chunk *chunk) {
    if (chunk->function_count >= chunk->function_capacity) {
        int new_capacity = chunk->function_capacity == 0
            ? 8
            : chunk->function_capacity * 2;

        VMFunction *new_functions = (VMFunction *)realloc(
            chunk->functions,
            sizeof(VMFunction) * (size_t)new_capacity
        );

        if (new_functions == NULL) {
            runtime_error("Out of memory growing VM function table");
        }

        chunk->functions = new_functions;
        chunk->function_capacity = new_capacity;
    }

    VMFunction *function = &chunk->functions[chunk->function_count++];
    memset(function, 0, sizeof(*function));

    function->chunk = (Chunk *)malloc(sizeof(Chunk));
    if (function->chunk == NULL) {
        runtime_error("Out of memory allocating VM function chunk");
    }

    chunk_init(function->chunk);
    return function;
}

int chunk_add_ast_ref(Chunk *chunk, const ASTNode *node) {
    if (chunk->ast_ref_count >= chunk->ast_ref_capacity) {
        int new_capacity = chunk->ast_ref_capacity == 0
            ? 8
            : chunk->ast_ref_capacity * 2;

        const ASTNode **new_refs = (const ASTNode **)realloc(
            chunk->ast_refs,
            sizeof(ASTNode *) * (size_t)new_capacity
        );

        if (new_refs == NULL) {
            runtime_error("Out of memory growing VM AST ref table");
        }

        chunk->ast_refs = new_refs;
        chunk->ast_ref_capacity = new_capacity;
    }

    chunk->ast_refs[chunk->ast_ref_count] = node;
    return chunk->ast_ref_count++;
}