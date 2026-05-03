#include "vm_compile_can.h"

#include "../Lexer/lexer.h"

static bool compiler_can_compile_address_target(ASTNode *target);

static bool compiler_can_compile_address_target(ASTNode *target) {
    if (target == NULL) {
        return false;
    }

    if (target->type == AST_IDENTIFIER) {
        return true;
    }

    if (target->type == AST_INDEX) {
        return compiler_can_compile_impl(target->data.index.target) &&
               compiler_can_compile_impl(target->data.index.index);
    }

    if (target->type == AST_OBJ_GET) {
        return compiler_can_compile_impl(target->data.obj_get.object);
    }

    return false;
}

bool compiler_can_compile_impl(ASTNode *node) {
    if (node == NULL) {
        return true;
    }

    switch (node->type) {
        case AST_NUMBER:
        case AST_STRING:
        case AST_NULL:
        case AST_BOOL:
        case AST_IDENTIFIER:
        case AST_BREAK:
        case AST_CONTINUE:
            return true;

        case AST_SWAP:
            return compiler_can_compile_address_target(node->data.swap_stmt.left) &&
                   compiler_can_compile_address_target(node->data.swap_stmt.right);

        case AST_TEST:
            return compiler_can_compile_impl(node->data.test_block.body);

        case AST_VAR_DECL:
            return compiler_can_compile_impl(node->data.var_decl.value);

        case AST_ASSIGN:
            return compiler_can_compile_impl(node->data.assign.value);

        case AST_BINOP:
            switch (node->data.binop.op) {
                case TOKEN_PLUS:
                case TOKEN_MINUS:
                case TOKEN_STAR:
                case TOKEN_SLASH:
                case TOKEN_MOD:
                case TOKEN_EQ:
                case TOKEN_NOT_EQ:
                case TOKEN_LESS:
                case TOKEN_GREATER:
                case TOKEN_LESS_EQUAL:
                case TOKEN_GREATER_EQUAL:
                case TOKEN_AND:
                case TOKEN_OR:
                    return compiler_can_compile_impl(node->data.binop.left) &&
                           compiler_can_compile_impl(node->data.binop.right);
                default:
                    return false;
            }

        case AST_IF_BLOCK:
            return compiler_can_compile_impl(node->data.if_block.condition) &&
                   compiler_can_compile_impl(node->data.if_block.body) &&
                   compiler_can_compile_impl(node->data.if_block.else_body);

        case AST_WHILE:
            return compiler_can_compile_impl(node->data.while_block.condition) &&
                   compiler_can_compile_impl(node->data.while_block.body);

        case AST_REPEAT:
            return compiler_can_compile_impl(node->data.repeat_stmt.count_expr) &&
                   compiler_can_compile_impl(node->data.repeat_stmt.body);

        case AST_FOR:
            return compiler_can_compile_impl(node->data.for_block.init) &&
                   compiler_can_compile_impl(node->data.for_block.condition) &&
                   compiler_can_compile_impl(node->data.for_block.step) &&
                   compiler_can_compile_impl(node->data.for_block.body);

        case AST_ARRAY:
            for (int i = 0; i < node->data.array.item_count; i++) {
                if (!compiler_can_compile_impl(node->data.array.items[i])) {
                    return false;
                }
            }
            return true;

        case AST_INDEX:
            return compiler_can_compile_impl(node->data.index.target) &&
                   compiler_can_compile_impl(node->data.index.index);

        case AST_INDEX_ASSIGN:
            return node->data.index_assign.left != NULL &&
                   node->data.index_assign.left->type == AST_INDEX &&
                   compiler_can_compile_impl(node->data.index_assign.left->data.index.target) &&
                   compiler_can_compile_impl(node->data.index_assign.left->data.index.index) &&
                   compiler_can_compile_impl(node->data.index_assign.right);

        case AST_OBJ_GET:
            return compiler_can_compile_impl(node->data.obj_get.object);

        case AST_OBJ_SET:
            return compiler_can_compile_impl(node->data.obj_set.object) &&
                   compiler_can_compile_impl(node->data.obj_set.value);

        case AST_ENTITY:
        case AST_EXTEND:
        case AST_WATCH:
            return true;

        case AST_ON_CHANGE:
            return compiler_can_compile_impl(node->data.on_change_stmt.body);

        case AST_DEFER:
            return compiler_can_compile_impl(node->data.defer_stmt.body);

        case AST_ADDRESS_OF:
            return compiler_can_compile_address_target(node->data.address_of.target);

        case AST_DEREF:
            return compiler_can_compile_impl(node->data.deref.target);

        case AST_DEREF_ASSIGN:
            return compiler_can_compile_impl(node->data.deref_assign.target) &&
                   compiler_can_compile_impl(node->data.deref_assign.value);

        case AST_USING:
            return !node->data.using_stmt.star_import;

        case AST_EXPORT: {
            ASTNode *decl = node->data.export_stmt.decl;
            if (decl == NULL) {
                return false;
            }

            switch (decl->type) {
                case AST_FUNC_DECL:
                case AST_VAR_DECL:
                case AST_ENTITY:
                    return compiler_can_compile_impl(decl);
                default:
                    return false;
            }
        }

        case AST_OUTPUT:
            for (int i = 0; i < node->data.output.arg_count; i++) {
                if (!compiler_can_compile_impl(node->data.output.args[i])) {
                    return false;
                }
            }
            return true;

        case AST_BLOCK:
            for (int i = 0; i < node->data.block.count; i++) {
                if (!compiler_can_compile_impl(node->data.block.items[i])) {
                    return false;
                }
            }
            return true;

        case AST_RETURN:
            return compiler_can_compile_impl(node->data.return_stmt.value);

        case AST_FUNC_DECL:
            return compiler_can_compile_impl(node->data.func_decl.body);

        case AST_FUNC_CALL:
            for (int i = 0; i < node->data.func_call.arg_count; i++) {
                if (!compiler_can_compile_impl(node->data.func_call.args[i])) {
                    return false;
                }
            }
            return true;

        case AST_METHOD_CALL:
            if (!compiler_can_compile_impl(node->data.method_call.target)) {
                return false;
            }

            for (int i = 0; i < node->data.method_call.arg_count; i++) {
                if (!compiler_can_compile_impl(node->data.method_call.args[i])) {
                    return false;
                }
            }
            return true;

        case AST_SWITCH:
            if (!compiler_can_compile_impl(node->data.switch_stmt.value)) {
                return false;
            }

            for (int i = 0; i < node->data.switch_stmt.case_count; i++) {
                if (!compiler_can_compile_impl(node->data.switch_stmt.case_values[i]) ||
                    !compiler_can_compile_impl(node->data.switch_stmt.case_bodies[i])) {
                    return false;
                }
            }

            return compiler_can_compile_impl(node->data.switch_stmt.default_body);

        case AST_INC_OP:
            return compiler_can_compile_impl(node->data.inc_op.target);

        default:
            return false;
    }
}