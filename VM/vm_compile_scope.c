#include "vm_compile_scope.h"
#include "vm_chunk.h"

#include <string.h>
#include "../Utils/hash.h"
#include "../Runtime/value.h"
#include "../Runtime/errors.h"

typedef struct {
    const ASTNode *skip_decl;
    const char *name;
    unsigned int hash;
    int duplicate_var_decl_count;
    int unsafe;
} LocalSafetyScan;

static void ast_scan_local_safety(const ASTNode *node,
                                  LocalSafetyScan *scan,
                                  int nested_capture,
                                  int in_for_scope);

static int compiler_name_constant(Compiler *compiler, const char *name) {
    return chunk_add_constant(compiler->chunk, make_string_raw(name));
}

static void compiler_emit_named_op(Compiler *compiler, OpCode op, const char *name) {
    const int constant = compiler_name_constant(compiler, name);
    chunk_write_byte(compiler->chunk, (uint8_t)op);
    chunk_write_u16(compiler->chunk, (uint16_t)constant);
}

typedef struct {
    const ASTNode *skip_using;
    const char *name;
    unsigned int hash;
    int found_using;
    int unsafe;
} ImportAliasScan;

int compiler_name_matches(const char *name_a,
                          unsigned int hash_a,
                          const char *name_b,
                          unsigned int hash_b) {
    return name_a != NULL &&
           name_b != NULL &&
           hash_a == hash_b &&
           strcmp(name_a, name_b) == 0;
}

int compiler_alloc_local_slot(Compiler *compiler) {
    if (compiler->next_local_slot >= 64) {
        runtime_error("VM local slot overflow");
    }
    return compiler->next_local_slot++;
}

int compiler_can_alloc_local(const Compiler *compiler) {
    return compiler != NULL &&
           compiler->next_local_slot < 64 &&
           compiler->local_count < (int)(sizeof(compiler->locals) / sizeof(compiler->locals[0]));
}

LocalEntry *compiler_lookup_local(Compiler *compiler, const char *name, unsigned int hash) {
    if (compiler == NULL || name == NULL) {
        return NULL;
    }

    for (int i = compiler->local_count - 1; i >= 0; i--) {
        if (compiler_name_matches(compiler->locals[i].name, compiler->locals[i].hash, name, hash)) {
            return &compiler->locals[i];
        }
    }

    return NULL;
}

LocalEntry *compiler_add_local(Compiler *compiler, const char *name, unsigned int hash) {
    if (compiler == NULL || name == NULL) {
        return NULL;
    }
    if (compiler->local_count >= (int)(sizeof(compiler->locals) / sizeof(compiler->locals[0]))) {
        runtime_error("VM local table overflow");
    }

    LocalEntry *entry = &compiler->locals[compiler->local_count++];
    entry->name = name;
    entry->hash = hash;
    entry->slot = compiler_alloc_local_slot(compiler);
    return entry;
}

static void ast_scan_local_safety(const ASTNode *node,
                                  LocalSafetyScan *scan,
                                  int nested_capture,
                                  int in_for_scope) {
    (void)in_for_scope;
    if (node == NULL || scan == NULL || scan->unsafe) {
        return;
    }

    switch (node->type) {
        case AST_IDENTIFIER:
            if (nested_capture &&
                compiler_name_matches(node->data.identifier.name,
                                      node->data.identifier.id_hash,
                                      scan->name,
                                      scan->hash)) {
                scan->unsafe = 1;
            }
            return;

        case AST_VAR_DECL:
            if (compiler_name_matches(node->data.var_decl.name,
                                      node->data.var_decl.id_hash,
                                      scan->name,
                                      scan->hash) &&
                node != scan->skip_decl) {
                scan->duplicate_var_decl_count++;
                scan->unsafe = 1;
            }
            ast_scan_local_safety(node->data.var_decl.value, scan, nested_capture, in_for_scope);
            return;

        case AST_ASSIGN:
            if (nested_capture &&
                compiler_name_matches(node->data.assign.name,
                                      node->data.assign.id_hash,
                                      scan->name,
                                      scan->hash)) {
                scan->unsafe = 1;
                return;
            }
            ast_scan_local_safety(node->data.assign.value, scan, nested_capture, in_for_scope);
            return;

        case AST_RETURN:
            ast_scan_local_safety(node->data.return_stmt.value, scan, nested_capture, in_for_scope);
            return;

        case AST_BLOCK:
            for (int i = 0; i < node->data.block.count && !scan->unsafe; i++) {
                ast_scan_local_safety(node->data.block.items[i], scan, nested_capture, in_for_scope);
            }
            return;

        case AST_BINOP:
            ast_scan_local_safety(node->data.binop.left, scan, nested_capture, in_for_scope);
            ast_scan_local_safety(node->data.binop.right, scan, nested_capture, in_for_scope);
            return;

        case AST_IF_BLOCK:
            ast_scan_local_safety(node->data.if_block.condition, scan, nested_capture, in_for_scope);
            ast_scan_local_safety(node->data.if_block.body, scan, nested_capture, in_for_scope);
            ast_scan_local_safety(node->data.if_block.else_body, scan, nested_capture, in_for_scope);
            return;

        case AST_WHILE:
            ast_scan_local_safety(node->data.while_block.condition, scan, nested_capture, in_for_scope);
            ast_scan_local_safety(node->data.while_block.body, scan, nested_capture, in_for_scope);
            return;

        case AST_REPEAT:
            if (node->data.repeat_stmt.has_iterator &&
                compiler_name_matches(node->data.repeat_stmt.iter_name,
                                      node->data.repeat_stmt.iter_hash,
                                      scan->name,
                                      scan->hash)) {
                scan->unsafe = 1;
                return;
            }
            ast_scan_local_safety(node->data.repeat_stmt.count_expr, scan, nested_capture, in_for_scope);
            ast_scan_local_safety(node->data.repeat_stmt.body, scan, nested_capture, in_for_scope);
            return;

        case AST_FOR:
            ast_scan_local_safety(node->data.for_block.init, scan, nested_capture, 1);
            ast_scan_local_safety(node->data.for_block.condition, scan, nested_capture, 1);
            ast_scan_local_safety(node->data.for_block.step, scan, nested_capture, 1);
            ast_scan_local_safety(node->data.for_block.body, scan, nested_capture, 1);
            return;

        case AST_FUNC_DECL:
            if (compiler_name_matches(node->data.func_decl.name,
                                      node->data.func_decl.name_hash,
                                      scan->name,
                                      scan->hash)) {
                scan->unsafe = 1;
                return;
            }
            ast_scan_local_safety(node->data.func_decl.body, scan, 1, 0);
            return;

        case AST_FUNC_CALL:
            for (int i = 0; i < node->data.func_call.arg_count && !scan->unsafe; i++) {
                ast_scan_local_safety(node->data.func_call.args[i], scan, nested_capture, in_for_scope);
            }
            return;

        case AST_METHOD_CALL:
            ast_scan_local_safety(node->data.method_call.target, scan, nested_capture, in_for_scope);
            for (int i = 0; i < node->data.method_call.arg_count && !scan->unsafe; i++) {
                ast_scan_local_safety(node->data.method_call.args[i], scan, nested_capture, in_for_scope);
            }
            return;

        case AST_INDEX:
            ast_scan_local_safety(node->data.index.target, scan, nested_capture, in_for_scope);
            ast_scan_local_safety(node->data.index.index, scan, nested_capture, in_for_scope);
            return;

        case AST_ARRAY:
            for (int i = 0; i < node->data.array.item_count && !scan->unsafe; i++) {
                ast_scan_local_safety(node->data.array.items[i], scan, nested_capture, in_for_scope);
            }
            return;

        case AST_INDEX_ASSIGN:
            ast_scan_local_safety(node->data.index_assign.left, scan, nested_capture, in_for_scope);
            ast_scan_local_safety(node->data.index_assign.right, scan, nested_capture, in_for_scope);
            return;

        case AST_OUTPUT:
            for (int i = 0; i < node->data.output.arg_count && !scan->unsafe; i++) {
                ast_scan_local_safety(node->data.output.args[i], scan, nested_capture, in_for_scope);
            }
            return;

        case AST_OBJ_GET:
            ast_scan_local_safety(node->data.obj_get.object, scan, nested_capture, in_for_scope);
            return;

        case AST_OBJ_SET:
            ast_scan_local_safety(node->data.obj_set.object, scan, nested_capture, in_for_scope);
            ast_scan_local_safety(node->data.obj_set.value, scan, nested_capture, in_for_scope);
            return;

        case AST_ENTITY:
            if (compiler_name_matches(node->data.entity.name,
                                      node->data.entity.name_hash,
                                      scan->name,
                                      scan->hash)) {
                scan->unsafe = 1;
                return;
            }
            ast_scan_local_safety(node->data.entity.init_body, scan, 1, 0);
            for (int i = 0; i < node->data.entity.method_count && !scan->unsafe; i++) {
                ast_scan_local_safety(node->data.entity.methods[i], scan, 1, 0);
            }
            return;

        case AST_EXTEND:
            for (int i = 0; i < node->data.extend.method_count && !scan->unsafe; i++) {
                ast_scan_local_safety(node->data.extend.methods[i], scan, 1, 0);
            }
            return;

        case AST_DEFER:
            ast_scan_local_safety(node->data.defer_stmt.body, scan, nested_capture, in_for_scope);
            return;

        case AST_WATCH:
            if (compiler_name_matches(node->data.watch_stmt.name,
                                      node->data.watch_stmt.hash,
                                      scan->name,
                                      scan->hash)) {
                scan->unsafe = 1;
            }
            return;

        case AST_ON_CHANGE:
            if (compiler_name_matches(node->data.on_change_stmt.name,
                                      node->data.on_change_stmt.hash,
                                      scan->name,
                                      scan->hash)) {
                scan->unsafe = 1;
                return;
            }
            ast_scan_local_safety(node->data.on_change_stmt.body, scan, 1, in_for_scope);
            return;

        case AST_EXPORT:
            if (node->data.export_stmt.decl != NULL) {
                ASTNode *decl = node->data.export_stmt.decl;
                if ((decl->type == AST_VAR_DECL &&
                     compiler_name_matches(decl->data.var_decl.name,
                                           decl->data.var_decl.id_hash,
                                           scan->name,
                                           scan->hash)) ||
                    (decl->type == AST_FUNC_DECL &&
                     compiler_name_matches(decl->data.func_decl.name,
                                           decl->data.func_decl.name_hash,
                                           scan->name,
                                           scan->hash)) ||
                    (decl->type == AST_ENTITY &&
                     compiler_name_matches(decl->data.entity.name,
                                           decl->data.entity.name_hash,
                                           scan->name,
                                           scan->hash))) {
                    scan->unsafe = 1;
                    return;
                }
                ast_scan_local_safety(decl, scan, nested_capture, in_for_scope);
            }
            return;

        case AST_ADDRESS_OF:
            if (node->data.address_of.target != NULL &&
                node->data.address_of.target->type == AST_IDENTIFIER &&
                compiler_name_matches(node->data.address_of.target->data.identifier.name,
                                      node->data.address_of.target->data.identifier.id_hash,
                                      scan->name,
                                      scan->hash)) {
                scan->unsafe = 1;
                return;
            }
            ast_scan_local_safety(node->data.address_of.target, scan, nested_capture, in_for_scope);
            return;

        case AST_DEREF:
            ast_scan_local_safety(node->data.deref.target, scan, nested_capture, in_for_scope);
            return;

        case AST_DEREF_ASSIGN:
            ast_scan_local_safety(node->data.deref_assign.target, scan, nested_capture, in_for_scope);
            ast_scan_local_safety(node->data.deref_assign.value, scan, nested_capture, in_for_scope);
            return;

        case AST_USING:
            if (node->data.using_stmt.alias != NULL &&
                compiler_name_matches(node->data.using_stmt.alias,
                                      get_hash(node->data.using_stmt.alias),
                                      scan->name,
                                      scan->hash)) {
                scan->unsafe = 1;
            }
            return;

        case AST_SWITCH:
            ast_scan_local_safety(node->data.switch_stmt.value, scan, nested_capture, in_for_scope);
            for (int i = 0; i < node->data.switch_stmt.case_count && !scan->unsafe; i++) {
                ast_scan_local_safety(node->data.switch_stmt.case_values[i], scan, nested_capture, in_for_scope);
                ast_scan_local_safety(node->data.switch_stmt.case_bodies[i], scan, nested_capture, in_for_scope);
            }
            ast_scan_local_safety(node->data.switch_stmt.default_body, scan, nested_capture, in_for_scope);
            return;

        case AST_SWAP:
            if ((node->data.swap_stmt.left != NULL &&
                 node->data.swap_stmt.left->type == AST_IDENTIFIER &&
                 compiler_name_matches(node->data.swap_stmt.left->data.identifier.name,
                                       node->data.swap_stmt.left->data.identifier.id_hash,
                                       scan->name,
                                       scan->hash)) ||
                (node->data.swap_stmt.right != NULL &&
                 node->data.swap_stmt.right->type == AST_IDENTIFIER &&
                 compiler_name_matches(node->data.swap_stmt.right->data.identifier.name,
                                       node->data.swap_stmt.right->data.identifier.id_hash,
                                       scan->name,
                                       scan->hash))) {
                scan->unsafe = 1;
                return;
            }
            ast_scan_local_safety(node->data.swap_stmt.left, scan, nested_capture, in_for_scope);
            ast_scan_local_safety(node->data.swap_stmt.right, scan, nested_capture, in_for_scope);
            return;

        case AST_TEST:
            ast_scan_local_safety(node->data.test_block.body, scan, nested_capture, in_for_scope);
            return;

        case AST_NUMBER:
        case AST_STRING:
        case AST_NULL:
        case AST_BOOL:
        case AST_BREAK:
        case AST_CONTINUE:
        case AST_INC_OP:
            return;
    }
}

int ast_name_must_remain_env_backed(Compiler *compiler, const ASTNode *decl_node) {
    if (compiler == NULL || decl_node == NULL || compiler->function_body == NULL) {
        return 1;
    }
    if (decl_node->type != AST_VAR_DECL) {
        return 1;
    }

    const char *name = decl_node->data.var_decl.name;
    const unsigned int hash = decl_node->data.var_decl.id_hash;
    if (name == NULL || strcmp(name, "self") == 0) {
        return 1;
    }

    if (compiler->function_decl != NULL) {
        for (int i = 0; i < compiler->function_decl->data.func_decl.param_count; i++) {
            if (compiler_name_matches(compiler->function_decl->data.func_decl.params[i],
                                      compiler->function_decl->data.func_decl.param_hashes[i],
                                      name,
                                      hash)) {
                return 1;
            }
        }
    }

    LocalSafetyScan scan = {
        .skip_decl = decl_node,
        .name = name,
        .hash = hash,
        .duplicate_var_decl_count = 0,
        .unsafe = 0,
    };
    ast_scan_local_safety(compiler->function_body, &scan, 0, 0);
    return scan.unsafe;
}

static int compiler_param_must_remain_env_backed(Compiler *compiler,
                                                 const char *name,
                                                 unsigned int hash) {
    if (compiler == NULL || compiler->function_body == NULL || name == NULL || strcmp(name, "self") == 0) {
        return 1;
    }

    LocalSafetyScan scan = {
        .skip_decl = NULL,
        .name = name,
        .hash = hash,
        .duplicate_var_decl_count = 0,
        .unsafe = 0,
    };
    ast_scan_local_safety(compiler->function_body, &scan, 0, 0);
    return scan.unsafe;
}

static int compiler_self_must_remain_env_backed(Compiler *compiler) {
    if (compiler == NULL || compiler->function_body == NULL) {
        return 1;
    }

    LocalSafetyScan scan = {
        .skip_decl = NULL,
        .name = "self",
        .hash = get_hash("self"),
        .duplicate_var_decl_count = 0,
        .unsafe = 0,
    };
    ast_scan_local_safety(compiler->function_body, &scan, 0, 0);
    return scan.unsafe;
}

int compiler_can_use_fast_locals(const Compiler *compiler) {
    return compiler != NULL &&
           compiler->function_body != NULL &&
           (compiler->function_decl != NULL || compiler->allow_root_locals);
}

static void ast_scan_import_alias_safety(const ASTNode *node,
                                         ImportAliasScan *scan,
                                         int skip_nested_bodies) {
    if (node == NULL || scan == NULL || scan->unsafe) {
        return;
    }

    switch (node->type) {
        case AST_BLOCK:
            for (int i = 0; i < node->data.block.count && !scan->unsafe; i++) {
                ast_scan_import_alias_safety(node->data.block.items[i], scan, skip_nested_bodies);
            }
            return;

        case AST_USING:
            if (node->data.using_stmt.alias != NULL &&
                compiler_name_matches(node->data.using_stmt.alias,
                                      get_hash(node->data.using_stmt.alias),
                                      scan->name,
                                      scan->hash)) {
                if (scan->skip_using == node) {
                    scan->found_using = 1;
                } else {
                    scan->unsafe = 1;
                }
            }
            return;

        case AST_VAR_DECL:
            if (compiler_name_matches(node->data.var_decl.name,
                                      node->data.var_decl.id_hash,
                                      scan->name,
                                      scan->hash)) {
                scan->unsafe = 1;
                return;
            }
            ast_scan_import_alias_safety(node->data.var_decl.value, scan, skip_nested_bodies);
            return;

        case AST_ASSIGN:
            if (compiler_name_matches(node->data.assign.name,
                                      node->data.assign.id_hash,
                                      scan->name,
                                      scan->hash)) {
                scan->unsafe = 1;
                return;
            }
            ast_scan_import_alias_safety(node->data.assign.value, scan, skip_nested_bodies);
            return;

        case AST_WATCH:
            if (compiler_name_matches(node->data.watch_stmt.name,
                                      node->data.watch_stmt.hash,
                                      scan->name,
                                      scan->hash)) {
                scan->unsafe = 1;
            }
            return;

        case AST_ON_CHANGE:
            if (compiler_name_matches(node->data.on_change_stmt.name,
                                      node->data.on_change_stmt.hash,
                                      scan->name,
                                      scan->hash)) {
                scan->unsafe = 1;
                return;
            }
            ast_scan_import_alias_safety(node->data.on_change_stmt.body, scan, skip_nested_bodies);
            return;

        case AST_ADDRESS_OF:
            if (node->data.address_of.target != NULL &&
                node->data.address_of.target->type == AST_IDENTIFIER &&
                compiler_name_matches(node->data.address_of.target->data.identifier.name,
                                      node->data.address_of.target->data.identifier.id_hash,
                                      scan->name,
                                      scan->hash)) {
                scan->unsafe = 1;
                return;
            }
            ast_scan_import_alias_safety(node->data.address_of.target, scan, skip_nested_bodies);
            return;

        case AST_SWAP:
            if ((node->data.swap_stmt.left != NULL &&
                 node->data.swap_stmt.left->type == AST_IDENTIFIER &&
                 compiler_name_matches(node->data.swap_stmt.left->data.identifier.name,
                                       node->data.swap_stmt.left->data.identifier.id_hash,
                                       scan->name,
                                       scan->hash)) ||
                (node->data.swap_stmt.right != NULL &&
                 node->data.swap_stmt.right->type == AST_IDENTIFIER &&
                 compiler_name_matches(node->data.swap_stmt.right->data.identifier.name,
                                       node->data.swap_stmt.right->data.identifier.id_hash,
                                       scan->name,
                                       scan->hash))) {
                scan->unsafe = 1;
                return;
            }
            ast_scan_import_alias_safety(node->data.swap_stmt.left, scan, skip_nested_bodies);
            ast_scan_import_alias_safety(node->data.swap_stmt.right, scan, skip_nested_bodies);
            return;

        case AST_REPEAT:
            if (node->data.repeat_stmt.has_iterator &&
                compiler_name_matches(node->data.repeat_stmt.iter_name,
                                      node->data.repeat_stmt.iter_hash,
                                      scan->name,
                                      scan->hash)) {
                scan->unsafe = 1;
                return;
            }
            ast_scan_import_alias_safety(node->data.repeat_stmt.count_expr, scan, skip_nested_bodies);
            ast_scan_import_alias_safety(node->data.repeat_stmt.body, scan, skip_nested_bodies);
            return;

        case AST_FUNC_DECL:
            if (compiler_name_matches(node->data.func_decl.name,
                                      node->data.func_decl.name_hash,
                                      scan->name,
                                      scan->hash)) {
                scan->unsafe = 1;
                return;
            }
            for (int i = 0; i < node->data.func_decl.param_count; i++) {
                if (compiler_name_matches(node->data.func_decl.params[i],
                                          node->data.func_decl.param_hashes[i],
                                          scan->name,
                                          scan->hash)) {
                    scan->unsafe = 1;
                    return;
                }
            }
            if (!skip_nested_bodies) {
                ast_scan_import_alias_safety(node->data.func_decl.body, scan, skip_nested_bodies);
            }
            return;

        case AST_ENTITY:
            if (compiler_name_matches(node->data.entity.name,
                                      node->data.entity.name_hash,
                                      scan->name,
                                      scan->hash)) {
                scan->unsafe = 1;
                return;
            }
            if (!skip_nested_bodies) {
                ast_scan_import_alias_safety(node->data.entity.init_body, scan, skip_nested_bodies);
                for (int i = 0; i < node->data.entity.method_count && !scan->unsafe; i++) {
                    ast_scan_import_alias_safety(node->data.entity.methods[i], scan, skip_nested_bodies);
                }
            }
            return;

        case AST_EXPORT:
            if (node->data.export_stmt.decl != NULL) {
                ASTNode *decl = node->data.export_stmt.decl;
                if ((decl->type == AST_VAR_DECL &&
                     compiler_name_matches(decl->data.var_decl.name,
                                           decl->data.var_decl.id_hash,
                                           scan->name,
                                           scan->hash)) ||
                    (decl->type == AST_FUNC_DECL &&
                     compiler_name_matches(decl->data.func_decl.name,
                                           decl->data.func_decl.name_hash,
                                           scan->name,
                                           scan->hash)) ||
                    (decl->type == AST_ENTITY &&
                     compiler_name_matches(decl->data.entity.name,
                                           decl->data.entity.name_hash,
                                           scan->name,
                                           scan->hash))) {
                    scan->unsafe = 1;
                    return;
                }
                ast_scan_import_alias_safety(decl, scan, skip_nested_bodies);
            }
            return;

        case AST_IF_BLOCK:
            ast_scan_import_alias_safety(node->data.if_block.condition, scan, skip_nested_bodies);
            ast_scan_import_alias_safety(node->data.if_block.body, scan, skip_nested_bodies);
            ast_scan_import_alias_safety(node->data.if_block.else_body, scan, skip_nested_bodies);
            return;

        case AST_WHILE:
            ast_scan_import_alias_safety(node->data.while_block.condition, scan, skip_nested_bodies);
            ast_scan_import_alias_safety(node->data.while_block.body, scan, skip_nested_bodies);
            return;

        case AST_FOR:
            ast_scan_import_alias_safety(node->data.for_block.init, scan, skip_nested_bodies);
            ast_scan_import_alias_safety(node->data.for_block.condition, scan, skip_nested_bodies);
            ast_scan_import_alias_safety(node->data.for_block.step, scan, skip_nested_bodies);
            ast_scan_import_alias_safety(node->data.for_block.body, scan, skip_nested_bodies);
            return;

        case AST_RETURN:
            ast_scan_import_alias_safety(node->data.return_stmt.value, scan, skip_nested_bodies);
            return;

        case AST_BINOP:
            ast_scan_import_alias_safety(node->data.binop.left, scan, skip_nested_bodies);
            ast_scan_import_alias_safety(node->data.binop.right, scan, skip_nested_bodies);
            return;

        case AST_FUNC_CALL:
            for (int i = 0; i < node->data.func_call.arg_count && !scan->unsafe; i++) {
                ast_scan_import_alias_safety(node->data.func_call.args[i], scan, skip_nested_bodies);
            }
            return;

        case AST_METHOD_CALL:
            ast_scan_import_alias_safety(node->data.method_call.target, scan, skip_nested_bodies);
            for (int i = 0; i < node->data.method_call.arg_count && !scan->unsafe; i++) {
                ast_scan_import_alias_safety(node->data.method_call.args[i], scan, skip_nested_bodies);
            }
            return;

        case AST_INDEX:
            ast_scan_import_alias_safety(node->data.index.target, scan, skip_nested_bodies);
            ast_scan_import_alias_safety(node->data.index.index, scan, skip_nested_bodies);
            return;

        case AST_INDEX_ASSIGN:
            ast_scan_import_alias_safety(node->data.index_assign.left, scan, skip_nested_bodies);
            ast_scan_import_alias_safety(node->data.index_assign.right, scan, skip_nested_bodies);
            return;

        case AST_ARRAY:
            for (int i = 0; i < node->data.array.item_count && !scan->unsafe; i++) {
                ast_scan_import_alias_safety(node->data.array.items[i], scan, skip_nested_bodies);
            }
            return;

        case AST_OBJ_GET:
            ast_scan_import_alias_safety(node->data.obj_get.object, scan, skip_nested_bodies);
            return;

        case AST_OBJ_SET:
            ast_scan_import_alias_safety(node->data.obj_set.object, scan, skip_nested_bodies);
            ast_scan_import_alias_safety(node->data.obj_set.value, scan, skip_nested_bodies);
            return;

        case AST_DEREF:
            ast_scan_import_alias_safety(node->data.deref.target, scan, skip_nested_bodies);
            return;

        case AST_DEREF_ASSIGN:
            ast_scan_import_alias_safety(node->data.deref_assign.target, scan, skip_nested_bodies);
            ast_scan_import_alias_safety(node->data.deref_assign.value, scan, skip_nested_bodies);
            return;

        case AST_OUTPUT:
            for (int i = 0; i < node->data.output.arg_count && !scan->unsafe; i++) {
                ast_scan_import_alias_safety(node->data.output.args[i], scan, skip_nested_bodies);
            }
            return;

        case AST_SWITCH:
            ast_scan_import_alias_safety(node->data.switch_stmt.value, scan, skip_nested_bodies);
            for (int i = 0; i < node->data.switch_stmt.case_count && !scan->unsafe; i++) {
                ast_scan_import_alias_safety(node->data.switch_stmt.case_values[i], scan, skip_nested_bodies);
                ast_scan_import_alias_safety(node->data.switch_stmt.case_bodies[i], scan, skip_nested_bodies);
            }
            ast_scan_import_alias_safety(node->data.switch_stmt.default_body, scan, skip_nested_bodies);
            return;

        case AST_DEFER:
            ast_scan_import_alias_safety(node->data.defer_stmt.body, scan, skip_nested_bodies);
            return;

        case AST_TEST:
            ast_scan_import_alias_safety(node->data.test_block.body, scan, skip_nested_bodies);
            return;

        case AST_EXTEND:
        case AST_IDENTIFIER:
        case AST_NUMBER:
        case AST_STRING:
        case AST_NULL:
        case AST_BOOL:
        case AST_BREAK:
        case AST_CONTINUE:
        case AST_INC_OP:
            return;
    }
}

static const ASTNode *compiler_find_import_alias_decl(const Compiler *compiler,
                                                      const char *name,
                                                      unsigned int hash) {
    if (compiler == NULL || compiler->program_root == NULL || compiler->program_root->type != AST_BLOCK) {
        return NULL;
    }

    for (int i = 0; i < compiler->program_root->data.block.count; i++) {
        ASTNode *item = compiler->program_root->data.block.items[i];
        if (item == NULL || item->type != AST_USING || item->data.using_stmt.alias == NULL) {
            continue;
        }
        if (compiler_name_matches(item->data.using_stmt.alias,
                                  get_hash(item->data.using_stmt.alias),
                                  name,
                                  hash)) {
            return item;
        }
    }

    return NULL;
}

int compiler_can_cache_import_alias(const Compiler *compiler,
                                    const char *name,
                                    unsigned int hash) {
    if (!compiler_can_use_fast_locals(compiler) || !compiler_can_alloc_local(compiler)) {
        return 0;
    }
    if (name == NULL || compiler_lookup_local((Compiler *)compiler, name, hash) != NULL) {
        return 0;
    }
    if (compiler->function_decl != NULL) {
        for (int i = 0; i < compiler->function_decl->data.func_decl.param_count; i++) {
            if (compiler_name_matches(compiler->function_decl->data.func_decl.params[i],
                                      compiler->function_decl->data.func_decl.param_hashes[i],
                                      name,
                                      hash)) {
                return 0;
            }
        }
    }

    const ASTNode *using_decl = compiler_find_import_alias_decl(compiler, name, hash);
    if (using_decl == NULL) {
        return 0;
    }

    ImportAliasScan scope_scan = {
        .skip_using = compiler->allow_root_locals ? using_decl : NULL,
        .name = name,
        .hash = hash,
        .found_using = 0,
        .unsafe = 0,
    };
    ast_scan_import_alias_safety(compiler->function_body, &scope_scan, 1);
    if (scope_scan.unsafe) {
        return 0;
    }

    ImportAliasScan program_scan = {
        .skip_using = using_decl,
        .name = name,
        .hash = hash,
        .found_using = 0,
        .unsafe = 0,
    };
    ast_scan_import_alias_safety(compiler->program_root, &program_scan, 0);
    return program_scan.found_using && !program_scan.unsafe;
}

void compiler_seed_function_param_locals(Compiler *compiler, VMFunction *function) {
    if (compiler == NULL || function == NULL || compiler->function_decl == NULL) {
        return;
    }

    for (int i = 0; i < compiler->function_decl->data.func_decl.param_count; i++) {
        function->param_local_slots[i] = -1;

        if (!compiler_can_alloc_local(compiler)) {
            continue;
        }

        const char *name = compiler->function_decl->data.func_decl.params[i];
        const unsigned int hash = compiler->function_decl->data.func_decl.param_hashes[i];
        if (compiler_param_must_remain_env_backed(compiler, name, hash)) {
            continue;
        }

        LocalEntry *entry = compiler_lookup_local(compiler, name, hash);
        if (entry == NULL) {
            entry = compiler_add_local(compiler, name, hash);
        }
        function->param_local_slots[i] = entry->slot;
    }
}

void compiler_seed_method_self_local(Compiler *compiler, VMFunction *function) {
    if (compiler == NULL || function == NULL || !compiler_can_alloc_local(compiler)) {
        return;
    }
    if (compiler_self_must_remain_env_backed(compiler)) {
        return;
    }

    LocalEntry *entry = compiler_lookup_local(compiler, "self", get_hash("self"));
    if (entry == NULL) {
        entry = compiler_add_local(compiler, "self", get_hash("self"));
    }
    function->self_local_slot = entry->slot;
}

static void compiler_collect_alias_names(const ASTNode *node,
                                         const char **names,
                                         unsigned int *hashes,
                                         int *count,
                                         int cap) {
    if (node == NULL || *count >= cap) {
        return;
    }
    if (node->type == AST_IDENTIFIER) {
        const char *name = node->data.identifier.name;
        const unsigned int hash = node->data.identifier.id_hash;
        if (name != NULL) {
            for (int i = 0; i < *count; i++) {
                if (hashes[i] == hash && strcmp(names[i], name) == 0) {
                    return;
                }
            }
            names[*count] = name;
            hashes[*count] = hash;
            (*count)++;
        }
        return;
    }
    switch (node->type) {
        case AST_BLOCK:
            for (int i = 0; i < node->data.block.count && *count < cap; i++) {
                compiler_collect_alias_names(node->data.block.items[i], names, hashes, count, cap);
            }
            break;
        case AST_IF_BLOCK:
            compiler_collect_alias_names(node->data.if_block.condition, names, hashes, count, cap);
            compiler_collect_alias_names(node->data.if_block.body, names, hashes, count, cap);
            compiler_collect_alias_names(node->data.if_block.else_body, names, hashes, count, cap);
            break;
        case AST_WHILE:
            compiler_collect_alias_names(node->data.while_block.condition, names, hashes, count, cap);
            compiler_collect_alias_names(node->data.while_block.body, names, hashes, count, cap);
            break;
        case AST_FOR:
            compiler_collect_alias_names(node->data.for_block.init, names, hashes, count, cap);
            compiler_collect_alias_names(node->data.for_block.condition, names, hashes, count, cap);
            compiler_collect_alias_names(node->data.for_block.step, names, hashes, count, cap);
            compiler_collect_alias_names(node->data.for_block.body, names, hashes, count, cap);
            break;
        case AST_REPEAT:
            compiler_collect_alias_names(node->data.repeat_stmt.count_expr, names, hashes, count, cap);
            compiler_collect_alias_names(node->data.repeat_stmt.body, names, hashes, count, cap);
            break;
        case AST_VAR_DECL:
            compiler_collect_alias_names(node->data.var_decl.value, names, hashes, count, cap);
            break;
        case AST_ASSIGN:
            compiler_collect_alias_names(node->data.assign.value, names, hashes, count, cap);
            break;
        case AST_RETURN:
            compiler_collect_alias_names(node->data.return_stmt.value, names, hashes, count, cap);
            break;
        case AST_OUTPUT:
            for (int i = 0; i < node->data.output.arg_count && *count < cap; i++) {
                compiler_collect_alias_names(node->data.output.args[i], names, hashes, count, cap);
            }
            break;
        case AST_BINOP:
            compiler_collect_alias_names(node->data.binop.left, names, hashes, count, cap);
            compiler_collect_alias_names(node->data.binop.right, names, hashes, count, cap);
            break;
        case AST_INDEX:
            compiler_collect_alias_names(node->data.index.target, names, hashes, count, cap);
            compiler_collect_alias_names(node->data.index.index, names, hashes, count, cap);
            break;
        case AST_OBJ_GET:
            compiler_collect_alias_names(node->data.obj_get.object, names, hashes, count, cap);
            break;
        case AST_OBJ_SET:
            compiler_collect_alias_names(node->data.obj_set.object, names, hashes, count, cap);
            compiler_collect_alias_names(node->data.obj_set.value, names, hashes, count, cap);
            break;
        case AST_METHOD_CALL:
            compiler_collect_alias_names(node->data.method_call.target, names, hashes, count, cap);
            for (int i = 0; i < node->data.method_call.arg_count && *count < cap; i++) {
                compiler_collect_alias_names(node->data.method_call.args[i], names, hashes, count, cap);
            }
            break;
        case AST_FUNC_CALL:
            for (int i = 0; i < node->data.func_call.arg_count && *count < cap; i++) {
                compiler_collect_alias_names(node->data.func_call.args[i], names, hashes, count, cap);
            }
            break;
        case AST_DEFER:
            compiler_collect_alias_names(node->data.defer_stmt.body, names, hashes, count, cap);
            break;
        case AST_ON_CHANGE:
            compiler_collect_alias_names(node->data.on_change_stmt.body, names, hashes, count, cap);
            break;
        case AST_SWITCH:
            compiler_collect_alias_names(node->data.switch_stmt.value, names, hashes, count, cap);
            for (int i = 0; i < node->data.switch_stmt.case_count && *count < cap; i++) {
                compiler_collect_alias_names(node->data.switch_stmt.case_values[i], names, hashes, count, cap);
                compiler_collect_alias_names(node->data.switch_stmt.case_bodies[i], names, hashes, count, cap);
            }
            compiler_collect_alias_names(node->data.switch_stmt.default_body, names, hashes, count, cap);
            break;
        default:
            break;
    }
}

void compiler_preseed_import_aliases(Compiler *compiler) {
    if (!compiler_can_use_fast_locals(compiler) || compiler->function_body == NULL) {
        return;
    }

    const char *names[64];
    unsigned int hashes[64];
    int count = 0;

    compiler_collect_alias_names(compiler->function_body, names, hashes, &count, 64);

    for (int i = 0; i < count; i++) {
        if (!compiler_can_alloc_local(compiler)) {
            break;
        }
        if (!compiler_can_cache_import_alias(compiler, names[i], hashes[i])) {
            continue;
        }
        LocalEntry *entry = compiler_add_local(compiler, names[i], hashes[i]);
        compiler_emit_named_op(compiler, OP_GET_GLOBAL, names[i]);
        chunk_write_byte(compiler->chunk, OP_DEFINE_LOCAL);
        chunk_write_byte(compiler->chunk, (uint8_t)entry->slot);
        chunk_write_byte(compiler->chunk, OP_POP);
    }
}
