#include "vm_peephole.h"
#include <string.h>
#include <stdint.h>
#include <stdlib.h>

typedef struct {
    int old_start;
    int old_end;
    uint8_t *new_code;
    int new_len;
} Replacement;

typedef struct {
    Replacement *items;
    int count;
    int capacity;
} ReplacementList;

static int is_numeric_constant(const Chunk *chunk, uint16_t const_idx) {
    if (const_idx >= (uint16_t)chunk->constants_count) {
        return 0;
    }
    RuntimeValue *val = &chunk->constants[const_idx];
    return val->type == VAL_INT || val->type == VAL_FLOAT;
}

static void replacement_list_add(ReplacementList *list, int old_start, int old_end, uint8_t *new_code, int new_len) {
    if (list->count >= list->capacity) {
        list->capacity = list->capacity == 0 ? 16 : list->capacity * 2;
        list->items = realloc(list->items, list->capacity * sizeof(Replacement));
    }
    list->items[list->count].old_start = old_start;
    list->items[list->count].old_end = old_end;
    list->items[list->count].new_code = new_code;
    list->items[list->count].new_len = new_len;
    list->count++;
}

static int get_opcode_len(uint8_t op) {
    switch (op) {
        case OP_CONSTANT:
        case OP_DEFINE_GLOBAL:
        case OP_EXPORT_GLOBAL:
        case OP_GET_GLOBAL:
        case OP_SET_GLOBAL:
        case OP_TEST_PASS:
        case OP_JUMP:
        case OP_JUMP_IF_FALSE:
        case OP_JUMP_IF_TRUE:
        case OP_LOOP:
        case OP_ARRAY_NEW:
        case OP_GET_FIELD:
        case OP_SET_FIELD:
        case OP_WATCH:
        case OP_ADDRESS_OF_VAR:
        case OP_ADDRESS_OF_FIELD:
        case OP_REGISTER_EXTENSION:
        case OP_ADD_DEFER:
            return 3;
        case OP_DEFINE_LOCAL:
        case OP_GET_LOCAL:
        case OP_SET_LOCAL:
        case OP_INC_LOCAL:
        case OP_DEC_LOCAL:
        case OP_CALL_NATIVE:
            return 2;
        case OP_DEFINE_FUNCTION:
        case OP_DEFINE_BLUEPRINT:
        case OP_WATCH_HANDLER:
            return 5;
        case OP_CALL:
        case OP_CALL_METHOD:
        case OP_INC_LOCAL_AND_LOOP:
            return 4;
        case OP_INC_LOCAL_AND_JUMP_IF_LT_CONST:
        case OP_JUMP_IF_LOCAL_LT_CONST_FALSE:
            return 6;
        case OP_IMPORT:
            return 7;
        case OP_ADD_LOCAL_CONST:
        case OP_SUB_LOCAL_CONST:
        case OP_MUL_LOCAL_CONST:
        case OP_STRING_APPEND_LOCAL_CONST:
        case OP_BUILDER_APPEND_LOCAL_CONST:
            return 4;
        case OP_BUILDER_START_LOCAL:
        case OP_BUILDER_FINISH_LOCAL:
            return 2;
        case OP_BUILDER_APPEND_LOCAL_VALUE:
        case OP_ADD_LOCAL_LOCAL:
        case OP_LT_LOCAL_LOCAL:
            return 3;
        default:
            return 1;
    }
}

static int try_match_lt_local_local(Chunk *chunk, int offset, uint8_t **out_new_code, int *out_new_len) {
    // Detects: OP_GET_LOCAL slot1, OP_GET_LOCAL slot2, OP_LT
    // Replaces with: OP_LT_LOCAL_LOCAL slot1 slot2
    if (offset + 5 > chunk->count) return 0;

    uint8_t *code = chunk->code;
    if (code[offset] != OP_GET_LOCAL) return 0;
    uint8_t slot1 = code[offset + 1];

    if (code[offset + 2] != OP_GET_LOCAL) return 0;
    uint8_t slot2 = code[offset + 3];

    if (code[offset + 4] != OP_LT) return 0;

    uint8_t *new_code = malloc(3);
    new_code[0] = OP_LT_LOCAL_LOCAL;
    new_code[1] = slot1;
    new_code[2] = slot2;

    *out_new_code = new_code;
    *out_new_len = 3;
    return 5;
}

static int try_match_add_local_const(Chunk *chunk, int offset, uint8_t **out_new_code, int *out_new_len) {
    if (offset + 9 > chunk->count) return 0;

    uint8_t *code = chunk->code;
    if (code[offset] != OP_GET_LOCAL) return 0;
    uint8_t slot = code[offset + 1];

    if (code[offset + 2] != OP_CONSTANT) return 0;
    uint16_t const_idx = ((uint16_t)code[offset + 3] << 8) | code[offset + 4];

    if (code[offset + 5] != OP_ADD) return 0;

    if (code[offset + 6] != OP_SET_LOCAL) return 0;
    if (code[offset + 7] != slot) return 0;

    if (code[offset + 8] != OP_POP) return 0;

    if (!is_numeric_constant(chunk, const_idx)) return 0;

    uint8_t *new_code = malloc(4);
    new_code[0] = OP_ADD_LOCAL_CONST;
    new_code[1] = slot;
    new_code[2] = (uint8_t)(const_idx >> 8);
    new_code[3] = (uint8_t)(const_idx & 0xff);
    *out_new_code = new_code;
    *out_new_len = 4;
    return 9;
}
static int try_match_add_local_local(Chunk *chunk, int offset, uint8_t **out_new_code, int *out_new_len) {
    if (offset + 8 > chunk->count) return 0;

    uint8_t *code = chunk->code;

    if (code[offset] != OP_GET_LOCAL) return 0;
    uint8_t dst = code[offset + 1];

    if (code[offset + 2] != OP_GET_LOCAL) return 0;
    uint8_t src = code[offset + 3];

    if (code[offset + 4] != OP_ADD) return 0;

    if (code[offset + 5] != OP_SET_LOCAL) return 0;
    if (code[offset + 6] != dst) return 0;

    if (code[offset + 7] != OP_POP) return 0;

    uint8_t *new_code = malloc(3);
    new_code[0] = OP_ADD_LOCAL_LOCAL;
    new_code[1] = dst;
    new_code[2] = src;

    *out_new_code = new_code;
    *out_new_len = 3;
    return 8;
}

static int try_match_sub_local_const(Chunk *chunk, int offset, uint8_t **out_new_code, int *out_new_len) {
    if (offset + 9 > chunk->count) return 0;

    uint8_t *code = chunk->code;
    if (code[offset] != OP_GET_LOCAL) return 0;
    uint8_t slot = code[offset + 1];

    if (code[offset + 2] != OP_CONSTANT) return 0;
    uint16_t const_idx = ((uint16_t)code[offset + 3] << 8) | code[offset + 4];

    if (code[offset + 5] != OP_SUB) return 0;

    if (code[offset + 6] != OP_SET_LOCAL) return 0;
    if (code[offset + 7] != slot) return 0;

    if (code[offset + 8] != OP_POP) return 0;

    if (!is_numeric_constant(chunk, const_idx)) return 0;

    uint8_t *new_code = malloc(4);
    new_code[0] = OP_SUB_LOCAL_CONST;
    new_code[1] = slot;
    new_code[2] = (uint8_t)(const_idx >> 8);
    new_code[3] = (uint8_t)(const_idx & 0xff);
    *out_new_code = new_code;
    *out_new_len = 4;
    return 9;
}

static int try_match_mul_local_const(Chunk *chunk, int offset, uint8_t **out_new_code, int *out_new_len) {
    if (offset + 9 > chunk->count) return 0;

    uint8_t *code = chunk->code;
    if (code[offset] != OP_GET_LOCAL) return 0;
    uint8_t slot = code[offset + 1];

    if (code[offset + 2] != OP_CONSTANT) return 0;
    uint16_t const_idx = ((uint16_t)code[offset + 3] << 8) | code[offset + 4];

    if (code[offset + 5] != OP_MUL) return 0;

    if (code[offset + 6] != OP_SET_LOCAL) return 0;
    if (code[offset + 7] != slot) return 0;

    if (code[offset + 8] != OP_POP) return 0;

    if (!is_numeric_constant(chunk, const_idx)) return 0;

    uint8_t *new_code = malloc(4);
    new_code[0] = OP_MUL_LOCAL_CONST;
    new_code[1] = slot;
    new_code[2] = (uint8_t)(const_idx >> 8);
    new_code[3] = (uint8_t)(const_idx & 0xff);
    *out_new_code = new_code;
    *out_new_len = 4;
    return 9;
}

static int try_match_string_append_local_const(Chunk *chunk, int offset, uint8_t **out_new_code, int *out_new_len) {
    if (offset + 9 > chunk->count) return 0;

    uint8_t *code = chunk->code;
    if (code[offset] != OP_GET_LOCAL) return 0;
    uint8_t slot = code[offset + 1];

    if (code[offset + 2] != OP_CONSTANT) return 0;
    uint16_t const_idx = ((uint16_t)code[offset + 3] << 8) | code[offset + 4];

    if (code[offset + 5] != OP_ADD) return 0;

    if (code[offset + 6] != OP_SET_LOCAL) return 0;
    if (code[offset + 7] != slot) return 0;

    if (code[offset + 8] != OP_POP) return 0;

    if (const_idx >= (uint16_t)chunk->constants_count) return 0;
    RuntimeValue *val = &chunk->constants[const_idx];
    if (val->type != VAL_STRING) return 0;

    uint8_t *new_code = malloc(4);
    new_code[0] = OP_STRING_APPEND_LOCAL_CONST;
    new_code[1] = slot;
    new_code[2] = (uint8_t)(const_idx >> 8);
    new_code[3] = (uint8_t)(const_idx & 0xff);
    *out_new_code = new_code;
    *out_new_len = 4;
    return 9;
}

static int try_match_inc_local_and_loop(Chunk *chunk, int offset, uint8_t **out_new_code, int *out_new_len) {
    if (offset + 5 > chunk->count) return 0;

    uint8_t *code = chunk->code;
    if (code[offset] != OP_INC_LOCAL) return 0;
    if (code[offset + 2] != OP_LOOP) return 0;

    uint8_t slot = code[offset + 1];
    uint8_t back_hi = code[offset + 3];
    uint8_t back_lo = code[offset + 4];

    uint8_t *new_code = malloc(4);
    new_code[0] = OP_INC_LOCAL_AND_LOOP;
    new_code[1] = slot;
    new_code[2] = back_hi;
    new_code[3] = back_lo;
    *out_new_code = new_code;
    *out_new_len = 4;
    return 5;
}

static int try_match_jump_if_inc_local_loop(Chunk *chunk, int offset, uint8_t **out_new_code, int *out_new_len) {
    // Detects: OP_JUMP_IF_LOCAL_LT_CONST_FALSE slot const_hi const_lo offset_hi offset_lo (6 bytes)
    //          OP_INC_LOCAL slot (2 bytes)
    //          OP_LOOP back_hi back_lo (3 bytes)
    // Replaces with: OP_INC_LOCAL_AND_JUMP_IF_LT_CONST slot const_hi const_lo back_hi back_lo (6 bytes)

    if (offset + 11 > chunk->count) return 0;

    uint8_t *code = chunk->code;

    if (code[offset] != OP_JUMP_IF_LOCAL_LT_CONST_FALSE) return 0;
    uint8_t slot1 = code[offset + 1];
    uint16_t const_idx = ((uint16_t)code[offset + 2] << 8) | code[offset + 3];

    if (code[offset + 6] != OP_INC_LOCAL) return 0;
    uint8_t slot2 = code[offset + 7];

    if (code[offset + 8] != OP_LOOP) return 0;
    uint16_t loop_back = ((uint16_t)code[offset + 9] << 8) | code[offset + 10];

    if (slot1 != slot2) return 0;
    if (!is_numeric_constant(chunk, const_idx)) return 0;

    uint8_t *new_code = malloc(6);
    new_code[0] = OP_INC_LOCAL_AND_JUMP_IF_LT_CONST;
    new_code[1] = slot1;
    new_code[2] = (uint8_t)(const_idx >> 8);
    new_code[3] = (uint8_t)(const_idx & 0xff);
    new_code[4] = (uint8_t)(loop_back >> 8);
    new_code[5] = (uint8_t)(loop_back & 0xff);

    *out_new_code = new_code;
    *out_new_len = 6;
    return 11;  // consumed 6 + 2 + 3 = 11 bytes
}

static void scan_for_patterns(Chunk *chunk, ReplacementList *replacements) {
    int offset = 0;
    while (offset < chunk->count) {
        uint8_t *new_code = NULL;
        int new_len = 0;
        int matched_len = 0;

        if ((matched_len = try_match_lt_local_local(chunk, offset, &new_code, &new_len)) > 0) {
            replacement_list_add(replacements, offset, offset + matched_len, new_code, new_len);
            offset += matched_len;
        }
        else if ((matched_len = try_match_jump_if_inc_local_loop(chunk, offset, &new_code, &new_len)) > 0) {
            replacement_list_add(replacements, offset, offset + matched_len, new_code, new_len);
            offset += matched_len;
        }
        else if ((matched_len = try_match_inc_local_and_loop(chunk, offset, &new_code, &new_len)) > 0) {
            replacement_list_add(replacements, offset, offset + matched_len, new_code, new_len);
            offset += matched_len;
        }
        else if ((matched_len = try_match_add_local_local(chunk, offset, &new_code, &new_len)) > 0) {
            replacement_list_add(replacements, offset, offset + matched_len, new_code, new_len);
            offset += matched_len;
        }
        else if ((matched_len = try_match_add_local_const(chunk, offset, &new_code, &new_len)) > 0) {
            replacement_list_add(replacements, offset, offset + matched_len, new_code, new_len);
            offset += matched_len;

        }
        else if ((matched_len = try_match_sub_local_const(chunk, offset, &new_code, &new_len)) > 0) {
            replacement_list_add(replacements, offset, offset + matched_len, new_code, new_len);
            offset += matched_len;
        } else if ((matched_len = try_match_mul_local_const(chunk, offset, &new_code, &new_len)) > 0) {
            replacement_list_add(replacements, offset, offset + matched_len, new_code, new_len);
            offset += matched_len;
        } else if ((matched_len = try_match_string_append_local_const(chunk, offset, &new_code, &new_len)) > 0) {
            replacement_list_add(replacements, offset, offset + matched_len, new_code, new_len);
            offset += matched_len;
        } else {
            offset += get_opcode_len(chunk->code[offset]);
        }
    }
}

static void vm_peephole_optimize_chunk(Chunk *chunk) {
    if (chunk == NULL || chunk->code == NULL || chunk->count == 0) {
        return;
    }

    ReplacementList replacements = {0};
    scan_for_patterns(chunk, &replacements);

    if (replacements.count == 0) {
        for (int i = 0; i < chunk->function_count; i++) {
            vm_peephole_optimize_chunk(chunk->functions[i].chunk);
        }
        return;
    }

    // Build offset map and new bytecode
    int new_count = 0;
    int old_offset = 0;
    int rep_idx = 0;

    while (old_offset < chunk->count) {
        if (rep_idx < replacements.count && replacements.items[rep_idx].old_start == old_offset) {
            new_count += replacements.items[rep_idx].new_len;
            old_offset = replacements.items[rep_idx].old_end;
            rep_idx++;
        } else {
            new_count += get_opcode_len(chunk->code[old_offset]);
            old_offset += get_opcode_len(chunk->code[old_offset]);
        }
    }

    uint8_t *old_code = chunk->code;
    int old_count = chunk->count;
    uint8_t *new_code = malloc(new_count);

    // Build offset map: for each old position, where it goes in new code
    int *offset_map = malloc((old_count + 1) * sizeof(int));
    int new_pos = 0;
    old_offset = 0;
    rep_idx = 0;

    for (int i = 0; i <= old_count; i++) {
        offset_map[i] = 0;
    }

    while (old_offset < old_count) {
        offset_map[old_offset] = new_pos;

        if (rep_idx < replacements.count && replacements.items[rep_idx].old_start == old_offset) {
            Replacement *rep = &replacements.items[rep_idx];
            memcpy(new_code + new_pos, rep->new_code, rep->new_len);
            new_pos += rep->new_len;
            old_offset = rep->old_end;
            rep_idx++;
        } else {
            int op_len = get_opcode_len(old_code[old_offset]);
            memcpy(new_code + new_pos, old_code + old_offset, op_len);
            new_pos += op_len;
            old_offset += op_len;
        }
    }
    offset_map[old_count] = new_count;

    // Patch jump offsets in new code
    new_pos = 0;
    while (new_pos < new_count) {
        uint8_t op = new_code[new_pos];

        if (op == OP_JUMP || op == OP_JUMP_IF_FALSE || op == OP_JUMP_IF_TRUE) {
            uint16_t old_offset_delta = ((uint16_t)new_code[new_pos + 1] << 8) | new_code[new_pos + 2];
            // Find which old instruction this new instruction came from
            int search_old = 0;
            int old_instr_pos = 0;
            rep_idx = 0;
            while (search_old < old_count) {
                if (rep_idx < replacements.count && replacements.items[rep_idx].old_start == search_old) {
                    if (offset_map[search_old] == new_pos) {
                        old_instr_pos = search_old;
                        break;
                    }
                    search_old = replacements.items[rep_idx].old_end;
                    rep_idx++;
                } else {
                    if (offset_map[search_old] == new_pos) {
                        old_instr_pos = search_old;
                        break;
                    }
                    search_old += get_opcode_len(old_code[search_old]);
                }
            }

            int old_target = old_instr_pos + 3 + (int)old_offset_delta;
            if (old_target > old_count) old_target = old_count;
            int new_target = offset_map[old_target];
            int new_offset_delta = new_target - (new_pos + 3);

            new_code[new_pos + 1] = (uint8_t)(new_offset_delta >> 8);
            new_code[new_pos + 2] = (uint8_t)(new_offset_delta & 0xff);
            new_pos += 3;
        } else if (op == OP_LOOP) {
            uint16_t old_loop_back = ((uint16_t)new_code[new_pos + 1] << 8) | new_code[new_pos + 2];
            // Find old instruction at this position
            int search_old = 0;
            int old_instr_pos = 0;
            rep_idx = 0;
            while (search_old < old_count) {
                if (rep_idx < replacements.count && replacements.items[rep_idx].old_start == search_old) {
                    if (offset_map[search_old] == new_pos) {
                        old_instr_pos = search_old;
                        break;
                    }
                    search_old = replacements.items[rep_idx].old_end;
                    rep_idx++;
                } else {
                    if (offset_map[search_old] == new_pos) {
                        old_instr_pos = search_old;
                        break;
                    }
                    search_old += get_opcode_len(old_code[search_old]);
                }
            }

            int old_loop_start = old_instr_pos + 3 - (int)old_loop_back;
            if (old_loop_start < 0) old_loop_start = 0;
            int new_loop_start = offset_map[old_loop_start];
            int new_loop_back = new_pos + 3 - new_loop_start;

            new_code[new_pos + 1] = (uint8_t)(new_loop_back >> 8);
            new_code[new_pos + 2] = (uint8_t)(new_loop_back & 0xff);
            new_pos += 3;
        } else if (op == OP_INC_LOCAL_AND_LOOP) {
            uint16_t old_loop_back = ((uint16_t)new_code[new_pos + 2] << 8) | new_code[new_pos + 3];
            // Find old instruction at this position
            int search_old = 0;
            int old_instr_pos = 0;
            rep_idx = 0;
            while (search_old < old_count) {
                if (rep_idx < replacements.count && replacements.items[rep_idx].old_start == search_old) {
                    if (offset_map[search_old] == new_pos) {
                        old_instr_pos = search_old;
                        break;
                    }
                    search_old = replacements.items[rep_idx].old_end;
                    rep_idx++;
                } else {
                    if (offset_map[search_old] == new_pos) {
                        old_instr_pos = search_old;
                        break;
                    }
                    search_old += get_opcode_len(old_code[search_old]);
                }
            }

            int old_loop_start = old_instr_pos + 5 - (int)old_loop_back;
            if (old_loop_start < 0) old_loop_start = 0;
            int new_loop_start = offset_map[old_loop_start];
            int new_loop_back = new_pos + 4 - new_loop_start;

            new_code[new_pos + 2] = (uint8_t)(new_loop_back >> 8);
            new_code[new_pos + 3] = (uint8_t)(new_loop_back & 0xff);
            new_pos += 4;
        } else if (op == OP_JUMP_IF_LOCAL_LT_CONST_FALSE) {
            uint16_t old_offset_delta = ((uint16_t)new_code[new_pos + 4] << 8) | new_code[new_pos + 5];
            int search_old = 0;
            int old_instr_pos = 0;
            rep_idx = 0;
            while (search_old < old_count) {
                if (rep_idx < replacements.count && replacements.items[rep_idx].old_start == search_old) {
                    if (offset_map[search_old] == new_pos) {
                        old_instr_pos = search_old;
                        break;
                    }
                    search_old = replacements.items[rep_idx].old_end;
                    rep_idx++;
                } else {
                    if (offset_map[search_old] == new_pos) {
                        old_instr_pos = search_old;
                        break;
                    }
                    search_old += get_opcode_len(old_code[search_old]);
                }
            }

            int old_target = old_instr_pos + 6 + (int)old_offset_delta;
            if (old_target > old_count) old_target = old_count;
            int new_target = offset_map[old_target];
            int new_offset_delta = new_target - (new_pos + 6);

            new_code[new_pos + 4] = (uint8_t)(new_offset_delta >> 8);
            new_code[new_pos + 5] = (uint8_t)(new_offset_delta & 0xff);
            new_pos += 6;
        } else if (op == OP_INC_LOCAL_AND_JUMP_IF_LT_CONST) {
            uint16_t stored_back = ((uint16_t)new_code[new_pos + 4] << 8) | new_code[new_pos + 5];
            // Find old instruction at this position
            int search_old = 0;
            int old_instr_pos = 0;
            rep_idx = 0;
            while (search_old < old_count) {
                if (rep_idx < replacements.count && replacements.items[rep_idx].old_start == search_old) {
                    if (offset_map[search_old] == new_pos) {
                        old_instr_pos = search_old;
                        break;
                    }
                    search_old = replacements.items[rep_idx].old_end;
                    rep_idx++;
                } else {
                    if (offset_map[search_old] == new_pos) {
                        old_instr_pos = search_old;
                        break;
                    }
                    search_old += get_opcode_len(old_code[search_old]);
                }
            }

            // This came from fusing JUMP_IF (6) + INC_LOCAL (2) + LOOP (3) = 11 bytes
            // The loop back offset is from OP_LOOP, which jumps from (old_instr_pos + 11) back 'stored_back' bytes
            int old_loop_start = old_instr_pos + 11 - (int)stored_back;
            if (old_loop_start < 0) old_loop_start = 0;
            int new_loop_start = offset_map[old_loop_start];
            int new_loop_back = new_pos + 6 - new_loop_start;

            new_code[new_pos + 4] = (uint8_t)(new_loop_back >> 8);
            new_code[new_pos + 5] = (uint8_t)(new_loop_back & 0xff);
            new_pos += 6;
        } else {
            new_pos += get_opcode_len(op);
        }
    }

    free(old_code);
    free(offset_map);
    chunk->code = new_code;
    chunk->count = new_count;
    chunk->capacity = new_count;

    for (int i = 0; i < replacements.count; i++) {
        free(replacements.items[i].new_code);
    }
    free(replacements.items);

    for (int i = 0; i < chunk->function_count; i++) {
        vm_peephole_optimize_chunk(chunk->functions[i].chunk);
    }
}

void vm_peephole_optimize(Chunk *chunk) {
    vm_peephole_optimize_chunk(chunk);
}
