#include "vm.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "vm_compile_can.h"
#include "vm_compile_scope.h"
#include "vm_peephole.h"

#include "../Eval/eval.h"
#include "../GC/gc.h"
#include "../Lexer/lexer.h"
#include "../Utils/hash.h"
#include "../Runtime/context.h"
#include "../Runtime/errors.h"
#include "vm_chunk.h"
#include "vm_compile_internal.h"

typedef enum {
    VM_NATIVE_WRITE = 1,
    VM_NATIVE_WRITELN = 2
} VmNativeId;

static int compiler_name_constant(Compiler *compiler, const char *name) {
    return chunk_add_constant(compiler->chunk, make_string_raw(name));
}
static int compiler_try_match_counted_loop(Compiler *compiler, ASTNode *condition, uint8_t *out_slot, uint16_t *out_const_idx);

static int compiler_hidden_name_constant(Compiler *compiler, const char *kind) {
    char buffer[96];
    const int index = (*compiler->hidden_counter)++;
    snprintf(buffer, sizeof(buffer), "\x1emks_vm_%s_%d", kind, index);
    return compiler_name_constant(compiler, buffer);
}

static bool compiler_emit_stmt(Compiler *compiler, ASTNode *node);
static bool compiler_emit_expr(Compiler *compiler, ASTNode *node);
static bool compiler_emit_address_of_target(Compiler *compiler, ASTNode *target);
static VMFunction *compiler_compile_function_chunk(Compiler *compiler, ASTNode *node, int allow_self_local);
static VMFunction *compiler_compile_body_chunk(Compiler *compiler,
                                               const ASTNode *body_node,
                                               const ASTNode *decl_key,
                                               const char *debug_name,
                                               int allow_self_local);
static bool compiler_register_entity_methods(Compiler *compiler, ASTNode *entity);
static bool compiler_emit_var_decl_local(Compiler *compiler, ASTNode *node);

static RuntimeValue compiler_fold_binop(int op, RuntimeValue left, RuntimeValue right) {
    if (left.type != VAL_INT && left.type != VAL_FLOAT) return make_null();
    if (right.type != VAL_INT && right.type != VAL_FLOAT) return make_null();

    double l_num = left.type == VAL_INT ? (double)left.data.int_value : left.data.float_value;
    double r_num = right.type == VAL_INT ? (double)right.data.int_value : right.data.float_value;
    int result_is_int = left.type == VAL_INT && right.type == VAL_INT;
    double result_val = 0;

    switch (op) {
        case TOKEN_PLUS: result_val = l_num + r_num; break;
        case TOKEN_MINUS: result_val = l_num - r_num; break;
        case TOKEN_STAR: result_val = l_num * r_num; break;
        case TOKEN_SLASH:
            if (r_num == 0) return make_null();
            result_val = l_num / r_num;
            break;
        case TOKEN_MOD:
            if (r_num == 0 || result_is_int == 0) return make_null();
            result_val = (double)((int64_t)l_num % (int64_t)r_num);
            break;
        case TOKEN_EQ: return make_bool(l_num == r_num);
        case TOKEN_NOT_EQ: return make_bool(l_num != r_num);
        case TOKEN_LESS: return make_bool(l_num < r_num);
        case TOKEN_GREATER: return make_bool(l_num > r_num);
        case TOKEN_LESS_EQUAL: return make_bool(l_num <= r_num);
        case TOKEN_GREATER_EQUAL: return make_bool(l_num >= r_num);
        default: return make_null();
    }

    if (result_is_int) {
        return make_int((int64_t)result_val);
    } else {
        return make_float(result_val);
    }
}

static bool compiler_is_inc_local_pattern(const ASTNode *assign_node, LocalEntry **out_entry, Compiler *compiler) {
    if (assign_node == NULL || assign_node->type != AST_ASSIGN) {
        return false;
    }

    ASTNode *rhs = assign_node->data.assign.value;
    if (rhs == NULL || rhs->type != AST_BINOP) {
        return false;
    }

    if (rhs->data.binop.op != TOKEN_PLUS) {
        return false;
    }

    ASTNode *left = rhs->data.binop.left;
    ASTNode *right = rhs->data.binop.right;

    if (left == NULL || left->type != AST_IDENTIFIER) {
        return false;
    }

    if (right == NULL || right->type != AST_NUMBER) {
        return false;
    }

    if (right->data.number.kind != NUMBER_INT || right->data.number.int_value != 1) {
        return false;
    }

    if (!compiler_name_matches(left->data.identifier.name,
                              left->data.identifier.id_hash,
                              assign_node->data.assign.name,
                              assign_node->data.assign.id_hash)) {
        return false;
    }

    LocalEntry *entry = compiler_lookup_local(compiler,
                                             assign_node->data.assign.name,
                                             assign_node->data.assign.id_hash);
    if (entry == NULL) {
        return false;
    }

    *out_entry = entry;
    return true;
}

static void compiler_emit_named_op(Compiler *compiler, OpCode op, const char *name) {
    const int constant = compiler_name_constant(compiler, name);
    chunk_write_byte(compiler->chunk, (uint8_t)op);
    chunk_write_u16(compiler->chunk, (uint16_t)constant);
}

static void compiler_emit_defer_unwind(Compiler *compiler, int target_defer_depth) {
    for (int depth = compiler->defer_depth; depth > target_defer_depth; depth--) {
        chunk_write_byte(compiler->chunk, OP_POP_DEFER_SCOPE);
    }
}

static bool compiler_emit_address_of_target(Compiler *compiler, ASTNode *target) {
    if (target == NULL) {
        return false;
    }

    if (target->type == AST_IDENTIFIER) {
        chunk_write_byte(compiler->chunk, OP_ADDRESS_OF_VAR);
        chunk_write_u16(compiler->chunk, (uint16_t)compiler_name_constant(
            compiler,
            target->data.identifier.name));
        return true;
    }

    if (target->type == AST_INDEX) {
        if (!compiler_emit_expr(compiler, target->data.index.target) ||
            !compiler_emit_expr(compiler, target->data.index.index)) {
            return false;
        }
        chunk_write_byte(compiler->chunk, OP_ADDRESS_OF_INDEX);
        return true;
    }

    if (target->type == AST_OBJ_GET) {
        if (!compiler_emit_expr(compiler, target->data.obj_get.object)) {
            return false;
        }
        chunk_write_byte(compiler->chunk, OP_ADDRESS_OF_FIELD);
        chunk_write_u16(compiler->chunk, (uint16_t)compiler_name_constant(
            compiler,
            target->data.obj_get.field));
        return true;
    }

    return false;
}

static VMFunction *compiler_compile_function_chunk(Compiler *compiler, ASTNode *node, int allow_self_local) {
    if (compiler == NULL || node == NULL || node->type != AST_FUNC_DECL) {
        return NULL;
    }

    VMFunction *function = chunk_add_function(compiler->chunk);
    function->decl_node = node;
    function->debug_name = node->data.func_decl.name;
    function->arity = node->data.func_decl.param_count;
    function->local_count = 0;
    for (size_t i = 0; i < sizeof(function->param_local_slots) / sizeof(function->param_local_slots[0]); i++) {
        function->param_local_slots[i] = -1;
    }
    function->self_local_slot = -1;
    function->chunk->debug_name = node->data.func_decl.name;

    Compiler nested = {
        .chunk = function->chunk,
        .debug_name = node->data.func_decl.name,
        .parent = compiler,
        .function_decl = node,
        .function_body = node->data.func_decl.body,
        .program_root = compiler->program_root,
        .allow_root_locals = 0,
        .loop = NULL,
        .hidden_counter = compiler->hidden_counter,
        .next_local_slot = 0,
        .local_count = 0,
        .env_scope_depth = 0,
        .defer_depth = 0,
    };

    if (allow_self_local) {
        compiler_seed_method_self_local(&nested, function);
    }
    compiler_seed_function_param_locals(&nested, function);
    compiler_preseed_import_aliases(&nested);

    if (!compiler_emit_stmt(&nested, node->data.func_decl.body)) {
        return NULL;
    }
    function->local_count = nested.local_count;
    chunk_write_byte(function->chunk, OP_NULL);
    chunk_write_byte(function->chunk, OP_RETURN);
    return function;
}

static VMFunction *compiler_compile_body_chunk(Compiler *compiler,
                                               const ASTNode *body_node,
                                               const ASTNode *decl_key,
                                               const char *debug_name,
                                               int allow_self_local) {
    if (compiler == NULL || body_node == NULL || decl_key == NULL) {
        return NULL;
    }

    VMFunction *function = chunk_add_function(compiler->chunk);
    function->decl_node = decl_key;
    function->debug_name = debug_name != NULL ? debug_name : "<body>";
    function->arity = 0;
    function->local_count = 0;
    for (size_t i = 0; i < sizeof(function->param_local_slots) / sizeof(function->param_local_slots[0]); i++) {
        function->param_local_slots[i] = -1;
    }
    function->self_local_slot = -1;
    function->chunk->debug_name = function->debug_name;

    Compiler nested = {
        .chunk = function->chunk,
        .debug_name = function->debug_name,
        .parent = compiler,
        .function_decl = NULL,
        .function_body = body_node,
        .program_root = compiler->program_root,
        .allow_root_locals = 0,
        .loop = NULL,
        .hidden_counter = compiler->hidden_counter,
        .next_local_slot = 0,
        .local_count = 0,
        .env_scope_depth = 0,
        .defer_depth = 0,
    };

    if (allow_self_local) {
        compiler_seed_method_self_local(&nested, function);
    }

    if (!compiler_emit_stmt(&nested, (ASTNode *)body_node)) {
        return NULL;
    }

    function->local_count = nested.local_count;
    chunk_write_byte(function->chunk, OP_NULL);
    chunk_write_byte(function->chunk, OP_RETURN);
    return function;
}

static bool compiler_register_entity_methods(Compiler *compiler, ASTNode *entity) {
    if (compiler == NULL || entity == NULL || entity->type != AST_ENTITY) {
        return false;
    }

    if (entity->data.entity.init_body != NULL) {
        if (compiler_compile_body_chunk(compiler,
                                        entity->data.entity.init_body,
                                        entity->data.entity.init_body,
                                        "<entity-init>",
                                        1) == NULL) {
            return false;
        }
    }

    for (int i = 0; i < entity->data.entity.method_count; i++) {
        if (compiler_compile_function_chunk(compiler, entity->data.entity.methods[i], 1) == NULL) {
            return false;
        }
    }
    return true;
}

static bool compiler_emit_function_decl(Compiler *compiler, ASTNode *node) {
    if (compiler_compile_function_chunk(compiler, node, 0) == NULL) {
        return false;
    }

    chunk_write_byte(compiler->chunk, OP_DEFINE_FUNCTION);
    chunk_write_u16(compiler->chunk, (uint16_t)(compiler->chunk->function_count - 1));
    chunk_write_u16(compiler->chunk, (uint16_t)compiler_name_constant(compiler, node->data.func_decl.name));
    return true;
}

static bool compiler_emit_output(Compiler *compiler, ASTNode *node) {
    const int count = node->data.output.arg_count;
    if (count > 255) {
        return false;
    }

    for (int i = 0; i < count; i++) {
        if (!compiler_emit_expr(compiler, node->data.output.args[i])) {
            return false;
        }
    }

    chunk_write_byte(compiler->chunk, OP_CALL_NATIVE);
    chunk_write_byte(compiler->chunk, node->data.output.is_newline ? VM_NATIVE_WRITELN : VM_NATIVE_WRITE);
    chunk_write_byte(compiler->chunk, (uint8_t)count);
    chunk_write_byte(compiler->chunk, OP_POP);
    return true;
}

static bool ast_contains_defer(const ASTNode *node) {
    if (node == NULL) return false;
    if (node->type == AST_DEFER) return true;
    switch (node->type) {
        case AST_FUNC_DECL:
        case AST_ENTITY:
        case AST_EXTEND:
            return false;
        case AST_BLOCK:
            for (int i = 0; i < node->data.block.count; i++) {
                if (ast_contains_defer(node->data.block.items[i])) return true;
            }
            return false;
        case AST_IF_BLOCK:
            return ast_contains_defer(node->data.if_block.body) ||
                   ast_contains_defer(node->data.if_block.else_body);
        case AST_WHILE:
            return ast_contains_defer(node->data.while_block.body);
        case AST_FOR:
            return ast_contains_defer(node->data.for_block.body);
        case AST_REPEAT:
            return ast_contains_defer(node->data.repeat_stmt.body);
        case AST_SWITCH:
            for (int i = 0; i < node->data.switch_stmt.case_count; i++) {
                if (ast_contains_defer(node->data.switch_stmt.case_bodies[i])) return true;
            }
            return ast_contains_defer(node->data.switch_stmt.default_body);
        case AST_TEST:
            return ast_contains_defer(node->data.test_block.body);
        default:
            return false;
    }
}

static bool compiler_emit_block(Compiler *compiler, ASTNode *node) {
    const bool needs_defer = ast_contains_defer(node);
    if (needs_defer) {
        chunk_write_byte(compiler->chunk, OP_PUSH_DEFER_SCOPE);
        compiler->defer_depth++;
    }

    for (int i = 0; i < node->data.block.count; i++) {
        if (!compiler_emit_stmt(compiler, node->data.block.items[i])) {
            return false;
        }
    }

    if (needs_defer) {
        chunk_write_byte(compiler->chunk, OP_POP_DEFER_SCOPE);
        compiler->defer_depth--;
    }
    return true;
}

static bool compiler_emit_if(Compiler *compiler, ASTNode *node) {
    if (!compiler_emit_expr(compiler, node->data.if_block.condition)) {
        return false;
    }

    const int false_jump = chunk_emit_jump(compiler->chunk, OP_JUMP_IF_FALSE);
    if (!compiler_emit_stmt(compiler, node->data.if_block.body)) {
        return false;
    }

    if (node->data.if_block.else_body != NULL) {
        const int end_jump = chunk_emit_jump(compiler->chunk, OP_JUMP);
        chunk_patch_jump(compiler->chunk, false_jump);
        if (!compiler_emit_stmt(compiler, node->data.if_block.else_body)) {
            return false;
        }
        chunk_patch_jump(compiler->chunk, end_jump);
        return true;
    }

    chunk_patch_jump(compiler->chunk, false_jump);
    return true;
}

static bool compiler_emit_switch(Compiler *compiler, ASTNode *node) {
    const int switch_name = compiler_hidden_name_constant(compiler, "switch");
    const int case_count = node->data.switch_stmt.case_count;
    int end_jumps[128];
    int end_count = 0;

    if (case_count > 128) {
        return false;
    }

    if (!compiler_emit_expr(compiler, node->data.switch_stmt.value)) {
        return false;
    }
    chunk_write_byte(compiler->chunk, OP_DEFINE_GLOBAL);
    chunk_write_u16(compiler->chunk, (uint16_t)switch_name);
    chunk_write_byte(compiler->chunk, OP_POP);

    for (int i = 0; i < case_count; i++) {
        chunk_write_byte(compiler->chunk, OP_GET_GLOBAL);
        chunk_write_u16(compiler->chunk, (uint16_t)switch_name);
        if (!compiler_emit_expr(compiler, node->data.switch_stmt.case_values[i])) {
            return false;
        }
        chunk_write_byte(compiler->chunk, OP_EQ);
        const int next_case = chunk_emit_jump(compiler->chunk, OP_JUMP_IF_FALSE);
        if (!compiler_emit_stmt(compiler, node->data.switch_stmt.case_bodies[i])) {
            return false;
        }
        end_jumps[end_count++] = chunk_emit_jump(compiler->chunk, OP_JUMP);
        chunk_patch_jump(compiler->chunk, next_case);
    }

    if (node->data.switch_stmt.default_body != NULL &&
        !compiler_emit_stmt(compiler, node->data.switch_stmt.default_body)) {
        return false;
    }

    for (int i = 0; i < end_count; i++) {
        chunk_patch_jump(compiler->chunk, end_jumps[i]);
    }
    return true;
}

static bool compiler_emit_loop_body(Compiler *compiler, ASTNode *body, LoopContext *loop) {
    loop->prev = compiler->loop;
    compiler->loop = loop;
    const bool ok = compiler_emit_stmt(compiler, body);
    compiler->loop = loop->prev;
    return ok;
}

static void compiler_patch_loop_exits(Compiler *compiler, LoopContext *loop, int break_target) {
    for (int i = 0; i < loop->break_count; i++) {
        chunk_patch_jump_to(compiler->chunk, loop->break_jumps[i], break_target);
    }
    for (int i = 0; i < loop->continue_count; i++) {
        chunk_patch_jump_to(compiler->chunk, loop->continue_jumps[i], loop->continue_target);
    }
}

static bool compiler_emit_while(Compiler *compiler, ASTNode *node) {
    LoopContext loop = {.continue_target = -1};
    loop.defer_depth_at_entry = compiler->defer_depth;
    const int loop_start = compiler->chunk->count;

    int exit_jump = -1;

    uint8_t slot;
    uint16_t const_idx;

    if (compiler_try_match_counted_loop(compiler,
                                        node->data.while_block.condition,
                                        &slot,
                                        &const_idx)) {
        chunk_write_byte(compiler->chunk, OP_JUMP_IF_LOCAL_LT_CONST_FALSE);
        chunk_write_byte(compiler->chunk, slot);
        chunk_write_u16(compiler->chunk, const_idx);

        exit_jump = compiler->chunk->count;
        chunk_write_u16(compiler->chunk, 0);
                                        } else {
                                            if (!compiler_emit_expr(compiler, node->data.while_block.condition)) {
                                                return false;
                                            }

                                            exit_jump = chunk_emit_jump(compiler->chunk, OP_JUMP_IF_FALSE);
                                        }

    loop.continue_target = loop_start;

    if (!compiler_emit_loop_body(compiler, node->data.while_block.body, &loop)) {
        return false;
    }

    chunk_emit_loop(compiler->chunk, loop_start);
    chunk_patch_jump(compiler->chunk, exit_jump);
    compiler_patch_loop_exits(compiler, &loop, compiler->chunk->count);
    return true;
}

static bool compiler_emit_repeat(Compiler *compiler, ASTNode *node) {
    const int count_name = compiler_hidden_name_constant(compiler, "repeat_count");
    const int index_name = compiler_hidden_name_constant(compiler, "repeat_index");
    const int use_local_slots = compiler->parent != NULL;
    const int count_slot = use_local_slots ? compiler_alloc_local_slot(compiler) : -1;
    const int index_slot = use_local_slots ? compiler_alloc_local_slot(compiler) : -1;

    if (node->data.repeat_stmt.has_iterator) {
        chunk_write_byte(compiler->chunk, OP_CONSTANT);
        chunk_write_u16(compiler->chunk, (uint16_t)chunk_add_constant(compiler->chunk, make_int(0)));
        chunk_write_byte(compiler->chunk, OP_DEFINE_GLOBAL);
        chunk_write_u16(compiler->chunk, (uint16_t)compiler_name_constant(compiler, node->data.repeat_stmt.iter_name));
        chunk_write_byte(compiler->chunk, OP_POP);
    }

    if (!compiler_emit_expr(compiler, node->data.repeat_stmt.count_expr)) {
        return false;
    }
    chunk_write_byte(compiler->chunk, use_local_slots ? OP_DEFINE_LOCAL : OP_DEFINE_GLOBAL);
    if (use_local_slots) {
        chunk_write_byte(compiler->chunk, (uint8_t)count_slot);
    } else {
        chunk_write_u16(compiler->chunk, (uint16_t)count_name);
    }
    chunk_write_byte(compiler->chunk, OP_POP);

    chunk_write_byte(compiler->chunk, OP_CONSTANT);
    chunk_write_u16(compiler->chunk, (uint16_t)chunk_add_constant(compiler->chunk, make_int(0)));
    chunk_write_byte(compiler->chunk, use_local_slots ? OP_DEFINE_LOCAL : OP_DEFINE_GLOBAL);
    if (use_local_slots) {
        chunk_write_byte(compiler->chunk, (uint8_t)index_slot);
    } else {
        chunk_write_u16(compiler->chunk, (uint16_t)index_name);
    }
    chunk_write_byte(compiler->chunk, OP_POP);

    LoopContext loop = {.continue_target = -1};
    loop.defer_depth_at_entry = compiler->defer_depth;
    const int loop_start = compiler->chunk->count;
    chunk_write_byte(compiler->chunk, use_local_slots ? OP_GET_LOCAL : OP_GET_GLOBAL);
    if (use_local_slots) {
        chunk_write_byte(compiler->chunk, (uint8_t)index_slot);
    } else {
        chunk_write_u16(compiler->chunk, (uint16_t)index_name);
    }
    chunk_write_byte(compiler->chunk, use_local_slots ? OP_GET_LOCAL : OP_GET_GLOBAL);
    if (use_local_slots) {
        chunk_write_byte(compiler->chunk, (uint8_t)count_slot);
    } else {
        chunk_write_u16(compiler->chunk, (uint16_t)count_name);
    }
    chunk_write_byte(compiler->chunk, OP_LT);
    const int exit_jump = chunk_emit_jump(compiler->chunk, OP_JUMP_IF_FALSE);

    if (node->data.repeat_stmt.has_iterator) {
        chunk_write_byte(compiler->chunk, use_local_slots ? OP_GET_LOCAL : OP_GET_GLOBAL);
        if (use_local_slots) {
            chunk_write_byte(compiler->chunk, (uint8_t)index_slot);
        } else {
            chunk_write_u16(compiler->chunk, (uint16_t)index_name);
        }
        chunk_write_byte(compiler->chunk, OP_SET_GLOBAL);
        chunk_write_u16(compiler->chunk, (uint16_t)compiler_name_constant(compiler, node->data.repeat_stmt.iter_name));
        chunk_write_byte(compiler->chunk, OP_POP);
    }

    loop.continue_target = -1;
    if (!compiler_emit_loop_body(compiler, node->data.repeat_stmt.body, &loop)) {
        return false;
    }

    loop.continue_target = compiler->chunk->count;
    chunk_write_byte(compiler->chunk, use_local_slots ? OP_GET_LOCAL : OP_GET_GLOBAL);
    if (use_local_slots) {
        chunk_write_byte(compiler->chunk, (uint8_t)index_slot);
    } else {
        chunk_write_u16(compiler->chunk, (uint16_t)index_name);
    }
    chunk_write_byte(compiler->chunk, OP_CONSTANT);
    chunk_write_u16(compiler->chunk, (uint16_t)chunk_add_constant(compiler->chunk, make_int(1)));
    chunk_write_byte(compiler->chunk, OP_ADD);
    chunk_write_byte(compiler->chunk, use_local_slots ? OP_SET_LOCAL : OP_SET_GLOBAL);
    if (use_local_slots) {
        chunk_write_byte(compiler->chunk, (uint8_t)index_slot);
    } else {
        chunk_write_u16(compiler->chunk, (uint16_t)index_name);
    }
    chunk_write_byte(compiler->chunk, OP_POP);

    chunk_emit_loop(compiler->chunk, loop_start);
    chunk_patch_jump(compiler->chunk, exit_jump);
    compiler_patch_loop_exits(compiler, &loop, compiler->chunk->count);
    return true;
}

static bool compiler_emit_var_decl_local(Compiler *compiler, ASTNode *node) {
    if (compiler == NULL || node == NULL || node->type != AST_VAR_DECL) {
        return false;
    }

    if (!compiler_emit_expr(compiler, node->data.var_decl.value)) {
        return false;
    }

    LocalEntry *entry = compiler_lookup_local(compiler,
                                              node->data.var_decl.name,
                                              node->data.var_decl.id_hash);
    if (entry != NULL || !compiler_can_alloc_local(compiler)) {
        compiler_emit_named_op(compiler, OP_DEFINE_GLOBAL, node->data.var_decl.name);
        chunk_write_byte(compiler->chunk, OP_POP);
        return true;
    }

    entry = compiler_add_local(compiler,
                               node->data.var_decl.name,
                               node->data.var_decl.id_hash);
    chunk_write_byte(compiler->chunk, OP_DEFINE_LOCAL);
    chunk_write_byte(compiler->chunk, (uint8_t)entry->slot);
    chunk_write_byte(compiler->chunk, OP_POP);
    return true;
}

static int compiler_try_match_counted_loop(Compiler *compiler, ASTNode *condition, uint8_t *out_slot, uint16_t *out_const_idx) {
    if (condition == NULL || condition->type != AST_BINOP) {
        return 0;
    }
    if (condition->data.binop.op != TOKEN_LESS) {
        return 0;
    }
    ASTNode *left = condition->data.binop.left;
    ASTNode *right = condition->data.binop.right;
    if (left == NULL || left->type != AST_IDENTIFIER) {
        return 0;
    }
    if (right == NULL || right->type != AST_NUMBER) {
        return 0;
    }
    LocalEntry *entry = compiler_lookup_local(compiler, left->data.identifier.name, left->data.identifier.id_hash);
    if (entry == NULL) {
        return 0;
    }
    RuntimeValue value = right->data.number.kind == NUMBER_INT
        ? make_int(right->data.number.int_value)
        : make_float(right->data.number.float_value);
    uint16_t const_idx = chunk_add_constant(compiler->chunk, value);
    *out_slot = (uint8_t)entry->slot;
    *out_const_idx = const_idx;
    return 1;
}

static bool compiler_emit_for(Compiler *compiler, ASTNode *node) {
    const int scoped_local_base = compiler->local_count;
    chunk_write_byte(compiler->chunk, OP_PUSH_ENV);
    compiler->env_scope_depth++;

    if (node->data.for_block.init != NULL) {
        ASTNode *init = node->data.for_block.init;
        const int can_localize_init =
            init->type == AST_VAR_DECL &&
            compiler_can_use_fast_locals(compiler) &&
            !ast_name_must_remain_env_backed(compiler, init) &&
            compiler_lookup_local(compiler, init->data.var_decl.name, init->data.var_decl.id_hash) == NULL &&
            compiler_can_alloc_local(compiler);

        if (can_localize_init) {
            if (!compiler_emit_var_decl_local(compiler, init)) {
                compiler->local_count = scoped_local_base;
                compiler->env_scope_depth--;
                return false;
            }
        } else if (!compiler_emit_stmt(compiler, init)) {
            compiler->local_count = scoped_local_base;
            compiler->env_scope_depth--;
            return false;
        }
    }

    LoopContext loop = {.continue_target = -1};
    loop.defer_depth_at_entry = compiler->defer_depth;
    const int loop_start = compiler->chunk->count;

    int exit_jump = -1;
    if (node->data.for_block.condition != NULL) {
        uint8_t slot;
        uint16_t const_idx;
        if (compiler_try_match_counted_loop(compiler, node->data.for_block.condition, &slot, &const_idx)) {
            chunk_write_byte(compiler->chunk, OP_JUMP_IF_LOCAL_LT_CONST_FALSE);
            chunk_write_byte(compiler->chunk, slot);
            chunk_write_u16(compiler->chunk, const_idx);
            exit_jump = compiler->chunk->count;
            chunk_write_u16(compiler->chunk, 0);
        } else {
            if (!compiler_emit_expr(compiler, node->data.for_block.condition)) {
                compiler->local_count = scoped_local_base;
                compiler->env_scope_depth--;
                return false;
            }
            exit_jump = chunk_emit_jump(compiler->chunk, OP_JUMP_IF_FALSE);
        }
    }

    if (!compiler_emit_loop_body(compiler, node->data.for_block.body, &loop)) {
        compiler->local_count = scoped_local_base;
        compiler->env_scope_depth--;
        return false;
    }

    loop.continue_target = compiler->chunk->count;
    if (node->data.for_block.step != NULL && !compiler_emit_stmt(compiler, node->data.for_block.step)) {
        compiler->local_count = scoped_local_base;
        compiler->env_scope_depth--;
        return false;
    }

    chunk_emit_loop(compiler->chunk, loop_start);
    if (exit_jump >= 0) {
        chunk_patch_jump(compiler->chunk, exit_jump);
    }
    compiler_patch_loop_exits(compiler, &loop, compiler->chunk->count);
    compiler->local_count = scoped_local_base;
    compiler->env_scope_depth--;
    chunk_write_byte(compiler->chunk, OP_POP_ENV);
    return true;
}

static bool compiler_emit_stmt(Compiler *compiler, ASTNode *node) {
    if (node == NULL) {
        return true;
    }

    switch (node->type) {
        case AST_BLOCK:
            return compiler_emit_block(compiler, node);

        case AST_NUMBER:
        case AST_STRING:
        case AST_NULL:
        case AST_BOOL:
        case AST_IDENTIFIER:
        case AST_BINOP:
        case AST_ARRAY:
        case AST_INDEX:
        case AST_FUNC_CALL:
        case AST_METHOD_CALL:
            if (!compiler_emit_expr(compiler, node)) {
                return false;
            }
            chunk_write_byte(compiler->chunk, OP_POP);
            return true;

        case AST_VAR_DECL:
            if (compiler_can_use_fast_locals(compiler) &&
                compiler->env_scope_depth == 0 &&
                !ast_name_must_remain_env_backed(compiler, node)) {
                return compiler_emit_var_decl_local(compiler, node);
            }
            if (!compiler_emit_expr(compiler, node->data.var_decl.value)) {
                return false;
            }
            compiler_emit_named_op(compiler, OP_DEFINE_GLOBAL, node->data.var_decl.name);
            chunk_write_byte(compiler->chunk, OP_POP);
            return true;

        case AST_ASSIGN: {
            LocalEntry *inc_entry = NULL;
            if (compiler_is_inc_local_pattern(node, &inc_entry, compiler)) {
                chunk_write_byte(compiler->chunk, OP_INC_LOCAL);
                chunk_write_byte(compiler->chunk, (uint8_t)inc_entry->slot);
                return true;
            }

            if (!compiler_emit_expr(compiler, node->data.assign.value)) {
                return false;
            }
            {
                LocalEntry *entry = compiler_lookup_local(compiler,
                                                          node->data.assign.name,
                                                          node->data.assign.id_hash);
                if (entry != NULL) {
                    chunk_write_byte(compiler->chunk, OP_SET_LOCAL);
                    chunk_write_byte(compiler->chunk, (uint8_t)entry->slot);
                } else {
                    compiler_emit_named_op(compiler, OP_SET_GLOBAL, node->data.assign.name);
                }
            }
            chunk_write_byte(compiler->chunk, OP_POP);
            return true;
        }

        case AST_INDEX_ASSIGN:
            if (!compiler_emit_expr(compiler, node->data.index_assign.left->data.index.target) ||
                !compiler_emit_expr(compiler, node->data.index_assign.left->data.index.index) ||
                !compiler_emit_expr(compiler, node->data.index_assign.right)) {
                return false;
            }
            chunk_write_byte(compiler->chunk, OP_SET_INDEX);
            chunk_write_byte(compiler->chunk, OP_POP);
            return true;

        case AST_OBJ_SET:
            if (!compiler_emit_expr(compiler, node->data.obj_set.object) ||
                !compiler_emit_expr(compiler, node->data.obj_set.value)) {
                return false;
            }
            chunk_write_byte(compiler->chunk, OP_SET_FIELD);
            chunk_write_u16(compiler->chunk, (uint16_t)compiler_name_constant(compiler, node->data.obj_set.field));
            chunk_write_byte(compiler->chunk, OP_POP);
            return true;

        case AST_DEREF_ASSIGN:
            if (!compiler_emit_expr(compiler, node->data.deref_assign.target) ||
                !compiler_emit_expr(compiler, node->data.deref_assign.value)) {
                return false;
            }
            chunk_write_byte(compiler->chunk, OP_DEREF_ASSIGN);
            chunk_write_byte(compiler->chunk, OP_POP);
            return true;

        case AST_SWAP:
            if (!compiler_emit_address_of_target(compiler, node->data.swap_stmt.left) ||
                !compiler_emit_address_of_target(compiler, node->data.swap_stmt.right)) {
                return false;
            }
            chunk_write_byte(compiler->chunk, OP_SWAP_PTRS);
            chunk_write_byte(compiler->chunk, OP_POP);
            return true;

        case AST_TEST:
            if (!compiler_emit_stmt(compiler, node->data.test_block.body)) {
                return false;
            }
            chunk_write_byte(compiler->chunk, OP_TEST_PASS);
            chunk_write_u16(compiler->chunk, (uint16_t)compiler_name_constant(compiler, node->data.test_block.name));
            return true;

        case AST_OUTPUT:
            return compiler_emit_output(compiler, node);

        case AST_EXPORT: {
            ASTNode *decl = node->data.export_stmt.decl;
            if (decl == NULL) {
                return false;
            }

            switch (decl->type) {
                case AST_FUNC_DECL:
                    if (!compiler_emit_function_decl(compiler, decl)) {
                        return false;
                    }
                    compiler_emit_named_op(compiler, OP_EXPORT_GLOBAL, decl->data.func_decl.name);
                    return true;

                case AST_VAR_DECL:
                    if (!compiler_emit_expr(compiler, decl->data.var_decl.value)) {
                        return false;
                    }
                    compiler_emit_named_op(compiler, OP_DEFINE_GLOBAL, decl->data.var_decl.name);
                    compiler_emit_named_op(compiler, OP_EXPORT_GLOBAL, decl->data.var_decl.name);
                    chunk_write_byte(compiler->chunk, OP_POP);
                    return true;

                case AST_ENTITY: {
                    if (!compiler_register_entity_methods(compiler, decl)) {
                        return false;
                    }
                    const int ast_ref = chunk_add_ast_ref(compiler->chunk, decl);
                    chunk_write_byte(compiler->chunk, OP_DEFINE_BLUEPRINT);
                    chunk_write_u16(compiler->chunk, (uint16_t)ast_ref);
                    chunk_write_u16(compiler->chunk, (uint16_t)compiler_name_constant(compiler, decl->data.entity.name));
                    compiler_emit_named_op(compiler, OP_EXPORT_GLOBAL, decl->data.entity.name);
                    chunk_write_byte(compiler->chunk, OP_POP);
                    return true;
                }

                default:
                    return false;
            }
        }

        case AST_DEFER: {
            const int ast_ref = chunk_add_ast_ref(compiler->chunk, node->data.defer_stmt.body);
            chunk_write_byte(compiler->chunk, OP_ADD_DEFER);
            chunk_write_u16(compiler->chunk, (uint16_t)ast_ref);
            return true;
        }

        case AST_USING: {
            const int spec_constant = chunk_add_constant(compiler->chunk, make_string_raw(node->data.using_stmt.path));
            const int alias_constant = node->data.using_stmt.alias != NULL
                ? chunk_add_constant(compiler->chunk, make_string_raw(node->data.using_stmt.alias))
                : 0xffff;
            chunk_write_byte(compiler->chunk, OP_IMPORT);
            chunk_write_u16(compiler->chunk, (uint16_t)spec_constant);
            chunk_write_u16(compiler->chunk, (uint16_t)alias_constant);
            chunk_write_byte(compiler->chunk, node->data.using_stmt.is_legacy_path ? 1 : 0);
            chunk_write_byte(compiler->chunk, node->data.using_stmt.star_import ? 1 : 0);
            chunk_write_byte(compiler->chunk, OP_POP);
            return true;
        }

        case AST_ENTITY: {
            if (!compiler_register_entity_methods(compiler, node)) {
                return false;
            }
            const int ast_ref = chunk_add_ast_ref(compiler->chunk, node);
            chunk_write_byte(compiler->chunk, OP_DEFINE_BLUEPRINT);
            chunk_write_u16(compiler->chunk, (uint16_t)ast_ref);
            chunk_write_u16(compiler->chunk, (uint16_t)compiler_name_constant(compiler, node->data.entity.name));
            chunk_write_byte(compiler->chunk, OP_POP);
            return true;
        }

        case AST_EXTEND: {
            const int ast_ref = chunk_add_ast_ref(compiler->chunk, node);
            chunk_write_byte(compiler->chunk, OP_REGISTER_EXTENSION);
            chunk_write_u16(compiler->chunk, (uint16_t)ast_ref);
            chunk_write_byte(compiler->chunk, OP_POP);
            return true;
        }

        case AST_WATCH:
            chunk_write_byte(compiler->chunk, OP_WATCH);
            chunk_write_u16(compiler->chunk, (uint16_t)compiler_name_constant(compiler, node->data.watch_stmt.name));
            chunk_write_byte(compiler->chunk, OP_POP);
            return true;

        case AST_ON_CHANGE: {
            const int ast_ref = chunk_add_ast_ref(compiler->chunk, node->data.on_change_stmt.body);
            chunk_write_byte(compiler->chunk, OP_WATCH_HANDLER);
            chunk_write_u16(compiler->chunk, (uint16_t)compiler_name_constant(compiler, node->data.on_change_stmt.name));
            chunk_write_u16(compiler->chunk, (uint16_t)ast_ref);
            chunk_write_byte(compiler->chunk, OP_POP);
            return true;
        }

        case AST_IF_BLOCK:
            return compiler_emit_if(compiler, node);

        case AST_SWITCH:
            return compiler_emit_switch(compiler, node);

        case AST_WHILE:
            return compiler_emit_while(compiler, node);

        case AST_REPEAT:
            return compiler_emit_repeat(compiler, node);

        case AST_FOR:
            return compiler_emit_for(compiler, node);

        case AST_BREAK:
            if (compiler->loop == NULL || compiler->loop->break_count >= 128) {
                return false;
            }
            compiler_emit_defer_unwind(compiler, compiler->loop->defer_depth_at_entry);
            compiler->loop->break_jumps[compiler->loop->break_count++] = chunk_emit_jump(compiler->chunk, OP_JUMP);
            return true;

        case AST_CONTINUE:
            if (compiler->loop == NULL) {
                return false;
            }
            compiler_emit_defer_unwind(compiler, compiler->loop->defer_depth_at_entry);
            if (compiler->loop->continue_target >= 0 &&
                compiler->loop->continue_target <= compiler->chunk->count) {
                chunk_emit_loop(compiler->chunk, compiler->loop->continue_target);
                return true;
            }
            if (compiler->loop->continue_count >= 128) {
                return false;
            }
            compiler->loop->continue_jumps[compiler->loop->continue_count++] =
                chunk_emit_jump(compiler->chunk, OP_JUMP);
            return true;

        case AST_RETURN:
            if (!compiler_emit_expr(compiler, node->data.return_stmt.value)) {
                return false;
            }
            compiler_emit_defer_unwind(compiler, 0);
            chunk_write_byte(compiler->chunk, OP_RETURN);
            return true;

        case AST_FUNC_DECL:
            return compiler_emit_function_decl(compiler, node);

        default:
            return false;
    }
}

static bool compiler_emit_expr(Compiler *compiler, ASTNode *node) {
    if (node == NULL) {
        return false;
    }

    switch (node->type) {
        case AST_NUMBER: {
            RuntimeValue value = node->data.number.kind == NUMBER_INT
                ? make_int(node->data.number.int_value)
                : make_float(node->data.number.float_value);
            chunk_write_byte(compiler->chunk, OP_CONSTANT);
            chunk_write_u16(compiler->chunk, (uint16_t)chunk_add_constant(compiler->chunk, value));
            return true;
        }

        case AST_STRING:
            chunk_write_byte(compiler->chunk, OP_CONSTANT);
            chunk_write_u16(compiler->chunk, (uint16_t)chunk_add_constant(
                compiler->chunk,
                make_string(node->data.string_value)));
            return true;

        case AST_NULL:
            chunk_write_byte(compiler->chunk, OP_NULL);
            return true;

        case AST_BOOL:
            chunk_write_byte(compiler->chunk, OP_CONSTANT);
            chunk_write_u16(compiler->chunk, (uint16_t)chunk_add_constant(
                compiler->chunk,
                make_bool(node->data.bool_value)));
            return true;

        case AST_IDENTIFIER:
            {
                LocalEntry *entry = compiler_lookup_local(compiler,
                                                          node->data.identifier.name,
                                                          node->data.identifier.id_hash);
                if (entry != NULL) {
                    chunk_write_byte(compiler->chunk, OP_GET_LOCAL);
                    chunk_write_byte(compiler->chunk, (uint8_t)entry->slot);
                } else if (compiler_can_cache_import_alias(compiler,
                                                          node->data.identifier.name,
                                                          node->data.identifier.id_hash)) {
                    entry = compiler_add_local(compiler,
                                               node->data.identifier.name,
                                               node->data.identifier.id_hash);
                    compiler_emit_named_op(compiler, OP_GET_GLOBAL, node->data.identifier.name);
                    chunk_write_byte(compiler->chunk, OP_DEFINE_LOCAL);
                    chunk_write_byte(compiler->chunk, (uint8_t)entry->slot);
                } else {
                    compiler_emit_named_op(compiler, OP_GET_GLOBAL, node->data.identifier.name);
                }
            }
            return true;

        case AST_BINOP: {
            if (node->data.binop.op == TOKEN_AND) {
                if (!compiler_emit_expr(compiler, node->data.binop.left)) {
                    return false;
                }
                const int false_jump = chunk_emit_jump(compiler->chunk, OP_JUMP_IF_FALSE);
                if (!compiler_emit_expr(compiler, node->data.binop.right)) {
                    return false;
                }
                chunk_write_byte(compiler->chunk, OP_TRUTHY);
                const int end_jump = chunk_emit_jump(compiler->chunk, OP_JUMP);
                chunk_patch_jump(compiler->chunk, false_jump);
                chunk_write_byte(compiler->chunk, OP_CONSTANT);
                chunk_write_u16(compiler->chunk, (uint16_t)chunk_add_constant(compiler->chunk, make_bool(false)));
                chunk_patch_jump(compiler->chunk, end_jump);
                return true;
            }

            if (node->data.binop.op == TOKEN_OR) {
                if (!compiler_emit_expr(compiler, node->data.binop.left)) {
                    return false;
                }
                const int true_jump = chunk_emit_jump(compiler->chunk, OP_JUMP_IF_TRUE);
                if (!compiler_emit_expr(compiler, node->data.binop.right)) {
                    return false;
                }
                chunk_write_byte(compiler->chunk, OP_TRUTHY);
                const int end_jump = chunk_emit_jump(compiler->chunk, OP_JUMP);
                chunk_patch_jump(compiler->chunk, true_jump);
                chunk_write_byte(compiler->chunk, OP_CONSTANT);
                chunk_write_u16(compiler->chunk, (uint16_t)chunk_add_constant(compiler->chunk, make_bool(true)));
                chunk_patch_jump(compiler->chunk, end_jump);
                return true;
            }

            // Constant folding: if both operands are literals, evaluate at compile time
            ASTNode *left = node->data.binop.left;
            ASTNode *right = node->data.binop.right;
            if (left != NULL && right != NULL &&
                left->type == AST_NUMBER && right->type == AST_NUMBER) {
                RuntimeValue left_val = left->data.number.kind == NUMBER_INT
                    ? make_int(left->data.number.int_value)
                    : make_float(left->data.number.float_value);
                RuntimeValue right_val = right->data.number.kind == NUMBER_INT
                    ? make_int(right->data.number.int_value)
                    : make_float(right->data.number.float_value);

                RuntimeValue result = compiler_fold_binop(node->data.binop.op, left_val, right_val);

                chunk_write_byte(compiler->chunk, OP_CONSTANT);
                chunk_write_u16(compiler->chunk, (uint16_t)chunk_add_constant(compiler->chunk, result));
                return true;
            }

            OpCode opcode;
            switch (node->data.binop.op) {
                case TOKEN_PLUS: opcode = OP_ADD; break;
                case TOKEN_MINUS: opcode = OP_SUB; break;
                case TOKEN_STAR: opcode = OP_MUL; break;
                case TOKEN_SLASH: opcode = OP_DIV; break;
                case TOKEN_MOD: opcode = OP_MOD; break;
                case TOKEN_EQ: opcode = OP_EQ; break;
                case TOKEN_NOT_EQ: opcode = OP_NEQ; break;
                case TOKEN_LESS: opcode = OP_LT; break;
                case TOKEN_GREATER: opcode = OP_GT; break;
                case TOKEN_LESS_EQUAL: opcode = OP_LE; break;
                case TOKEN_GREATER_EQUAL: opcode = OP_GE; break;
                default: return false;
            }

            if (!compiler_emit_expr(compiler, node->data.binop.left) ||
                !compiler_emit_expr(compiler, node->data.binop.right)) {
                return false;
            }
            chunk_write_byte(compiler->chunk, (uint8_t)opcode);
            return true;
        }

        case AST_ARRAY:
            for (int i = 0; i < node->data.array.item_count; i++) {
                if (!compiler_emit_expr(compiler, node->data.array.items[i])) {
                    return false;
                }
            }
            chunk_write_byte(compiler->chunk, OP_ARRAY_NEW);
            chunk_write_u16(compiler->chunk, (uint16_t)node->data.array.item_count);
            return true;

        case AST_INDEX:
            if (!compiler_emit_expr(compiler, node->data.index.target) ||
                !compiler_emit_expr(compiler, node->data.index.index)) {
                return false;
            }
            chunk_write_byte(compiler->chunk, OP_GET_INDEX);
            return true;

        case AST_FUNC_CALL: {
            const int arg_count = node->data.func_call.arg_count;
            if (arg_count > 255) {
                return false;
            }
            for (int i = 0; i < arg_count; i++) {
                if (!compiler_emit_expr(compiler, node->data.func_call.args[i])) {
                    return false;
                }
            }
            chunk_write_byte(compiler->chunk, OP_CALL);
            chunk_write_u16(compiler->chunk, (uint16_t)compiler_name_constant(compiler, node->data.func_call.name));
            chunk_write_byte(compiler->chunk, (uint8_t)arg_count);
            return true;
        }

        case AST_METHOD_CALL: {
            const int arg_count = node->data.method_call.arg_count;
            if (arg_count > 255) {
                return false;
            }
            if (!compiler_emit_expr(compiler, node->data.method_call.target)) {
                return false;
            }
            for (int i = 0; i < arg_count; i++) {
                if (!compiler_emit_expr(compiler, node->data.method_call.args[i])) {
                    return false;
                }
            }
            chunk_write_byte(compiler->chunk, OP_CALL_METHOD);
            chunk_write_u16(compiler->chunk, (uint16_t)compiler_name_constant(compiler, node->data.method_call.method_name));
            chunk_write_byte(compiler->chunk, (uint8_t)arg_count);
            return true;
        }

        case AST_OBJ_GET:
            if (!compiler_emit_expr(compiler, node->data.obj_get.object)) {
                return false;
            }
            chunk_write_byte(compiler->chunk, OP_GET_FIELD);
            chunk_write_u16(compiler->chunk, (uint16_t)compiler_name_constant(compiler, node->data.obj_get.field));
            return true;

        case AST_OBJ_SET:
            if (!compiler_emit_expr(compiler, node->data.obj_set.object) ||
                !compiler_emit_expr(compiler, node->data.obj_set.value)) {
                return false;
            }
            chunk_write_byte(compiler->chunk, OP_SET_FIELD);
            chunk_write_u16(compiler->chunk, (uint16_t)compiler_name_constant(compiler, node->data.obj_set.field));
            return true;

        case AST_ADDRESS_OF:
            return compiler_emit_address_of_target(compiler, node->data.address_of.target);

        case AST_DEREF:
            if (!compiler_emit_expr(compiler, node->data.deref.target)) {
                return false;
            }
            chunk_write_byte(compiler->chunk, OP_DEREF);
            return true;

        case AST_DEREF_ASSIGN:
            if (!compiler_emit_expr(compiler, node->data.deref_assign.target) ||
                !compiler_emit_expr(compiler, node->data.deref_assign.value)) {
                return false;
            }
            chunk_write_byte(compiler->chunk, OP_DEREF_ASSIGN);
            return true;

        default:
            return false;
    }
}

bool compiler_can_compile(ASTNode *node) {
    return compiler_can_compile_impl(node);
}

static bool compile_program_mode(ASTNode *node, Chunk *out, int allow_root_locals) {
    if (node == NULL || out == NULL || !compiler_can_compile(node)) {
        return false;
    }

    chunk_init(out);
    out->debug_name = "<program>";
    int hidden_counter = 0;
    Compiler compiler = {
        .chunk = out,
        .debug_name = "<program>",
        .parent = NULL,
        .function_decl = NULL,
        .function_body = allow_root_locals ? node : NULL,
        .program_root = node,
        .allow_root_locals = allow_root_locals,
        .loop = NULL,
        .hidden_counter = &hidden_counter,
        .next_local_slot = 0,
        .local_count = 0,
        .env_scope_depth = 0,
        .defer_depth = 0,
    };

    gc_pause();
    const bool ok = compiler_emit_stmt(&compiler, node);
    if (ok) {
        chunk_write_byte(out, OP_NULL);
        chunk_write_byte(out, OP_RETURN);
        vm_peephole_optimize(out);
    } else {
        chunk_free(out);
    }
    gc_resume();
    return ok;
}

bool compile_program(ASTNode *node, Chunk *out) {
    return compile_program_mode(node, out, 0);
}

bool compile_script_program(ASTNode *node, Chunk *out) {
    return compile_program_mode(node, out, 1);
}
