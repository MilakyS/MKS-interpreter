#include "vm.h"

#include <stdio.h>

const char *vm_opcode_name(OpCode op) {
    switch (op) {
        case OP_CONSTANT: return "OP_CONSTANT";
        case OP_NULL: return "OP_NULL";
        case OP_DEFINE_FUNCTION: return "OP_DEFINE_FUNCTION";
        case OP_DEFINE_BLUEPRINT: return "OP_DEFINE_BLUEPRINT";
        case OP_REGISTER_EXTENSION: return "OP_REGISTER_EXTENSION";
        case OP_DEFINE_GLOBAL: return "OP_DEFINE_NAME";
        case OP_EXPORT_GLOBAL: return "OP_EXPORT_NAME";
        case OP_DEFINE_LOCAL: return "OP_DEFINE_LOCAL";
        case OP_GET_GLOBAL: return "OP_GET_NAME";
        case OP_GET_LOCAL: return "OP_GET_LOCAL";
        case OP_SET_GLOBAL: return "OP_SET_NAME";
        case OP_SET_LOCAL: return "OP_SET_LOCAL";
        case OP_ADD_GLOBAL_CONST: return "OP_ADD_GLOBAL_CONST";
        case OP_ADD_GLOBAL_CONST_BY_COUNT_TO_LIMIT: return "OP_ADD_GLOBAL_CONST_BY_COUNT_TO_LIMIT";
        case OP_ADD_LOCAL_CONST_BY_COUNT_TO_LIMIT: return "OP_ADD_LOCAL_CONST_BY_COUNT_TO_LIMIT";
        case OP_INC_LOCAL: return "OP_INC_LOCAL";
        case OP_DEC_LOCAL: return "OP_DEC_LOCAL";
        case OP_ADD_LOCAL_CONST: return "OP_ADD_LOCAL_CONST";
        case OP_SUB_LOCAL_CONST: return "OP_SUB_LOCAL_CONST";
        case OP_MUL_LOCAL_CONST: return "OP_MUL_LOCAL_CONST";
        case OP_DIV_LOCAL_CONST: return "OP_DIV_LOCAL_CONST";
        case OP_STRING_APPEND_LOCAL_CONST: return "OP_STRING_APPEND_LOCAL_CONST";
        case OP_ADD_LOCAL_LOCAL: return "OP_ADD_LOCAL_LOCAL";
        case OP_INC_LOCAL_AND_LOOP: return "OP_INC_LOCAL_AND_LOOP";
        case OP_BUILDER_START_LOCAL: return "OP_BUILDER_START_LOCAL";
        case OP_BUILDER_APPEND_LOCAL_CONST: return "OP_BUILDER_APPEND_LOCAL_CONST";
        case OP_BUILDER_APPEND_LOCAL_VALUE: return "OP_BUILDER_APPEND_LOCAL_VALUE";
        case OP_BUILDER_FINISH_LOCAL: return "OP_BUILDER_FINISH_LOCAL";
        case OP_JUMP_IF_LOCAL_LT_CONST_FALSE: return "OP_JUMP_IF_LOCAL_LT_CONST_FALSE";
        case OP_INC_LOCAL_AND_JUMP_IF_LT_CONST: return "OP_INC_LOCAL_AND_JUMP_IF_LT_CONST";
        case OP_ADD: return "OP_ADD";
        case OP_SUB: return "OP_SUB";
        case OP_MUL: return "OP_MUL";
        case OP_DIV: return "OP_DIV";
        case OP_MOD: return "OP_MOD";
        case OP_EQ: return "OP_EQ";
        case OP_NEQ: return "OP_NEQ";
        case OP_LT: return "OP_LT";
        case OP_GT: return "OP_GT";
        case OP_LE: return "OP_LE";
        case OP_GE: return "OP_GE";
        case OP_LT_LOCAL_LOCAL: return "OP_LT_LOCAL_LOCAL";
        case OP_JUMP: return "OP_JUMP";
        case OP_JUMP_IF_FALSE: return "OP_JUMP_IF_FALSE";
        case OP_JUMP_IF_TRUE: return "OP_JUMP_IF_TRUE";
        case OP_LOOP: return "OP_LOOP";
        case OP_PUSH_ENV: return "OP_PUSH_ENV";
        case OP_POP_ENV: return "OP_POP_ENV";
        case OP_PUSH_DEFER_SCOPE: return "OP_PUSH_DEFER_SCOPE";
        case OP_ADD_DEFER: return "OP_ADD_DEFER";
        case OP_POP_DEFER_SCOPE: return "OP_POP_DEFER_SCOPE";
        case OP_ARRAY_NEW: return "OP_ARRAY_NEW";
        case OP_GET_INDEX: return "OP_GET_INDEX";
        case OP_SET_INDEX: return "OP_SET_INDEX";
        case OP_GET_FIELD: return "OP_GET_FIELD";
        case OP_SET_FIELD: return "OP_SET_FIELD";
        case OP_TRUTHY: return "OP_TRUTHY";
        case OP_WATCH: return "OP_WATCH";
        case OP_WATCH_HANDLER: return "OP_WATCH_HANDLER";
        case OP_ADDRESS_OF_VAR: return "OP_ADDRESS_OF_VAR";
        case OP_ADDRESS_OF_INDEX: return "OP_ADDRESS_OF_INDEX";
        case OP_ADDRESS_OF_FIELD: return "OP_ADDRESS_OF_FIELD";
        case OP_DEREF: return "OP_DEREF";
        case OP_DEREF_ASSIGN: return "OP_DEREF_ASSIGN";
        case OP_SWAP_PTRS: return "OP_SWAP_PTRS";
        case OP_IMPORT: return "OP_IMPORT";
        case OP_CALL: return "OP_CALL";
        case OP_CALL_SELF: return "OP_CALL_SELF";
        case OP_CALL_METHOD: return "OP_CALL_METHOD";
        case OP_CALL_NATIVE: return "OP_CALL_NATIVE";
        case OP_TEST_PASS: return "OP_TEST_PASS";
        case OP_POP: return "OP_POP";
        case OP_RETURN: return "OP_RETURN";
    }
    return "OP_UNKNOWN";
}

static int dump_chunk_impl(const Chunk *chunk, FILE *out, int indent) {
    int offset = 0;
    while (offset < chunk->count) {
        for (int i = 0; i < indent; i++) {
            fputc(' ', out);
        }
        fprintf(out, "%04d  %s", offset, vm_opcode_name((OpCode)chunk->code[offset]));
        switch ((OpCode)chunk->code[offset]) {
            case OP_CONSTANT:
            case OP_DEFINE_GLOBAL:
            case OP_EXPORT_GLOBAL:
            case OP_GET_GLOBAL:
            case OP_SET_GLOBAL:
            case OP_TEST_PASS:
                fprintf(out, " %u", (unsigned)((chunk->code[offset + 1] << 8) | chunk->code[offset + 2]));
                offset += 3;
                break;
            case OP_ADD_GLOBAL_CONST:
                fprintf(out, " name=%u const=%u",
                        (unsigned)((chunk->code[offset + 1] << 8) | chunk->code[offset + 2]),
                        (unsigned)((chunk->code[offset + 3] << 8) | chunk->code[offset + 4]));
                offset += 5;
                break;
            case OP_ADD_GLOBAL_CONST_BY_COUNT_TO_LIMIT:
                fprintf(out, " name=%u counter=%u limit=%u const=%u",
                        (unsigned)((chunk->code[offset + 1] << 8) | chunk->code[offset + 2]),
                        (unsigned)chunk->code[offset + 3],
                        (unsigned)((chunk->code[offset + 4] << 8) | chunk->code[offset + 5]),
                        (unsigned)((chunk->code[offset + 6] << 8) | chunk->code[offset + 7]));
                offset += 8;
                break;
            case OP_ADD_LOCAL_CONST_BY_COUNT_TO_LIMIT:
                fprintf(out, " target=%u counter=%u limit=%u const=%u",
                        (unsigned)chunk->code[offset + 1],
                        (unsigned)chunk->code[offset + 2],
                        (unsigned)((chunk->code[offset + 3] << 8) | chunk->code[offset + 4]),
                        (unsigned)((chunk->code[offset + 5] << 8) | chunk->code[offset + 6]));
                offset += 7;
                break;
            case OP_DEFINE_LOCAL:
            case OP_GET_LOCAL:
            case OP_SET_LOCAL:
            case OP_INC_LOCAL:
            case OP_DEC_LOCAL:
                fprintf(out, " %u", (unsigned)chunk->code[offset + 1]);
                offset += 2;
                break;
            case OP_DEFINE_FUNCTION:
                fprintf(out, " fn=%u name=%u",
                        (unsigned)((chunk->code[offset + 1] << 8) | chunk->code[offset + 2]),
                        (unsigned)((chunk->code[offset + 3] << 8) | chunk->code[offset + 4]));
                offset += 5;
                break;
            case OP_DEFINE_BLUEPRINT:
                fprintf(out, " ast=%u name=%u",
                        (unsigned)((chunk->code[offset + 1] << 8) | chunk->code[offset + 2]),
                        (unsigned)((chunk->code[offset + 3] << 8) | chunk->code[offset + 4]));
                offset += 5;
                break;
            case OP_REGISTER_EXTENSION:
            case OP_ADD_DEFER:
                fprintf(out, " ast=%u",
                        (unsigned)((chunk->code[offset + 1] << 8) | chunk->code[offset + 2]));
                offset += 3;
                break;
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
                fprintf(out, " %u", (unsigned)((chunk->code[offset + 1] << 8) | chunk->code[offset + 2]));
                offset += 3;
                break;
            case OP_WATCH_HANDLER:
                fprintf(out, " name=%u ast=%u",
                        (unsigned)((chunk->code[offset + 1] << 8) | chunk->code[offset + 2]),
                        (unsigned)((chunk->code[offset + 3] << 8) | chunk->code[offset + 4]));
                offset += 5;
                break;
            case OP_CALL:
            case OP_CALL_METHOD:
                fprintf(out, " name=%u argc=%u",
                        (unsigned)((chunk->code[offset + 1] << 8) | chunk->code[offset + 2]),
                        (unsigned)chunk->code[offset + 3]);
                offset += 4;
                break;
            case OP_CALL_SELF:
                fprintf(out, " argc=%u", (unsigned)chunk->code[offset + 1]);
                offset += 2;
                break;
            case OP_IMPORT:
                fprintf(out, " spec=%u alias=%u legacy=%u star=%u",
                        (unsigned)((chunk->code[offset + 1] << 8) | chunk->code[offset + 2]),
                        (unsigned)((chunk->code[offset + 3] << 8) | chunk->code[offset + 4]),
                        (unsigned)chunk->code[offset + 5],
                        (unsigned)chunk->code[offset + 6]);
                offset += 7;
                break;
            case OP_CALL_NATIVE:
                fprintf(out, " native=%u argc=%u",
                        (unsigned)chunk->code[offset + 1],
                        (unsigned)chunk->code[offset + 2]);
                offset += 3;
                break;
            case OP_ADD_LOCAL_LOCAL:
                fprintf(out, " dst=%u src=%u",
                        (unsigned)chunk->code[offset + 1],
                        (unsigned)chunk->code[offset + 2]);
                offset += 3;
                break;
            case OP_LT_LOCAL_LOCAL:
                fprintf(out, " slot1=%u slot2=%u",
                        (unsigned)chunk->code[offset + 1],
                        (unsigned)chunk->code[offset + 2]);
                offset += 3;
                break;
            case OP_ADD_LOCAL_CONST:
            case OP_SUB_LOCAL_CONST:
            case OP_MUL_LOCAL_CONST:
            case OP_STRING_APPEND_LOCAL_CONST:
            case OP_BUILDER_APPEND_LOCAL_CONST:
                fprintf(out, " slot=%u const=%u",
                        (unsigned)chunk->code[offset + 1],
                        (unsigned)((chunk->code[offset + 2] << 8) | chunk->code[offset + 3]));
                offset += 4;
                break;
            case OP_BUILDER_START_LOCAL:
            case OP_BUILDER_FINISH_LOCAL:
                fprintf(out, " slot=%u",
                        (unsigned)chunk->code[offset + 1]);
                offset += 2;
                break;
            case OP_BUILDER_APPEND_LOCAL_VALUE:
                fprintf(out, " builder=%u value=%u",
                        (unsigned)chunk->code[offset + 1],
                        (unsigned)chunk->code[offset + 2]);
                offset += 3;
                break;
            case OP_JUMP_IF_LOCAL_LT_CONST_FALSE:
                fprintf(out, " slot=%u const=%u offset=%u",
                        (unsigned)chunk->code[offset + 1],
                        (unsigned)((chunk->code[offset + 2] << 8) | chunk->code[offset + 3]),
                        (unsigned)((chunk->code[offset + 4] << 8) | chunk->code[offset + 5]));
                offset += 6;
                break;
            case OP_INC_LOCAL_AND_LOOP:
                fprintf(out, " slot=%u offset=%u",
                        (unsigned)chunk->code[offset + 1],
                        (unsigned)((chunk->code[offset + 2] << 8) | chunk->code[offset + 3]));
                offset += 4;
                break;
            case OP_INC_LOCAL_AND_JUMP_IF_LT_CONST:
                fprintf(out, " slot=%u const=%u offset=%u",
                        (unsigned)chunk->code[offset + 1],
                        (unsigned)((chunk->code[offset + 2] << 8) | chunk->code[offset + 3]),
                        (unsigned)((chunk->code[offset + 4] << 8) | chunk->code[offset + 5]));
                offset += 6;
                break;
            default:
                offset += 1;
                break;
        }
        fputc('\n', out);
    }
    return offset;
}

void vm_dump_program(const Chunk *chunk, FILE *out) {
    if (chunk == NULL || out == NULL) {
        return;
    }

    fprintf(out, "== %s ==\n", chunk->debug_name != NULL ? chunk->debug_name : "<chunk>");
    dump_chunk_impl(chunk, out, 0);
    for (int i = 0; i < chunk->function_count; i++) {
        fputc('\n', out);
        vm_dump_program(chunk->functions[i].chunk, out);
    }
}
