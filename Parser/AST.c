#include "AST.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static ASTNode *create_ast(ASTNodeType type, int line, int column) {
    ASTNode *node = malloc(sizeof(ASTNode));
    if (node == NULL) {
        fprintf(stderr, "[MKS AST Error] Out of memory while creating AST node\n");
        exit(1);
    }

    memset(node, 0, sizeof(ASTNode));
    node->type = type;
    node->line = line;
    node->column = column;
    return node;
}

ASTNode *create_ast_ident(char *name, unsigned int id_hash, int line, int col) {
    ASTNode *node = create_ast(AST_IDENTIFIER, line, col);
    node->data.identifier.name = name;
    node->data.identifier.id_hash = id_hash;
    node->data.identifier.cached_entry = NULL;
    node->data.identifier.cached_env = NULL;
    node->data.identifier.cached_env_version = 0;
    return node;
}

ASTNode *create_ast_var_decl(ASTNode *value, int line, int col, char *name, unsigned int id_hash) {
    ASTNode *node = create_ast(AST_VAR_DECL, line, col);
    node->data.var_decl.name = name;
    node->data.var_decl.id_hash = id_hash;
    node->data.var_decl.value = value;
    return node;
}

ASTNode *create_ast_assign(char *name, unsigned int id_hash, ASTNode *value, int line, int col) {
    ASTNode *node = create_ast(AST_ASSIGN, line, col);
    node->data.assign.name = name;
    node->data.assign.id_hash = id_hash;
    node->data.assign.value = value;
    node->data.assign.cached_entry = NULL;
    node->data.assign.cached_env = NULL;
    node->data.assign.cached_env_version = 0;
    return node;
}

ASTNode *create_ast_num(double val, int line, int col) {
    return create_ast_float(val, line, col);
}

ASTNode *create_ast_int(const int64_t val, int line, int col) {
    ASTNode *node = create_ast(AST_NUMBER, line, col);
    node->data.number.kind = NUMBER_INT;
    node->data.number.int_value = val;
    node->data.number.float_value = (double)val;
    return node;
}

ASTNode *create_ast_float(const double val, int line, int col) {
    ASTNode *node = create_ast(AST_NUMBER, line, col);
    node->data.number.kind = NUMBER_FLOAT;
    node->data.number.int_value = (int64_t)val;
    node->data.number.float_value = val;
    return node;
}

ASTNode *create_ast_string(char *val, int line, int col) {
    ASTNode *node = create_ast(AST_STRING, line, col);
    node->data.string_value = val;
    return node;
}

ASTNode *create_ast_null(int line, int col) {
    return create_ast(AST_NULL, line, col);
}

ASTNode *create_ast_bool(bool value, int line, int col) {
    ASTNode *node = create_ast(AST_BOOL, line, col);
    node->data.bool_value = value;
    return node;
}

ASTNode *create_ast_array(ASTNode **elements, int count, int line, int col) {
    ASTNode *node = create_ast(AST_ARRAY, line, col);
    node->data.array.items = elements;
    node->data.array.item_count = count;
    return node;
}

ASTNode *create_ast_binop(ASTNode *left, ASTNode *right, int op, int line, int col) {
    ASTNode *node = create_ast(AST_BINOP, line, col);
    node->data.binop.left = left;
    node->data.binop.right = right;
    node->data.binop.op = op;
    return node;
}

ASTNode *create_ast_index(ASTNode *target, ASTNode *index, int line, int col) {
    ASTNode *node = create_ast(AST_INDEX, line, col);
    node->data.index.target = target;
    node->data.index.index = index;
    return node;
}

ASTNode *create_ast_index_assign(ASTNode *left, ASTNode *right, int line, int col) {
    ASTNode *node = create_ast(AST_INDEX_ASSIGN, line, col);
    node->data.index_assign.left = left;
    node->data.index_assign.right = right;
    return node;
}

ASTNode *create_ast_address_of(ASTNode *target, int line, int col) {
    ASTNode *node = create_ast(AST_ADDRESS_OF, line, col);
    node->data.address_of.target = target;
    return node;
}

ASTNode *create_ast_deref(ASTNode *target, int line, int col) {
    ASTNode *node = create_ast(AST_DEREF, line, col);
    node->data.deref.target = target;
    return node;
}

ASTNode *create_ast_deref_assign(ASTNode *target, ASTNode *value, int line, int col) {
    ASTNode *node = create_ast(AST_DEREF_ASSIGN, line, col);
    node->data.deref_assign.target = target;
    node->data.deref_assign.value = value;
    return node;
}

ASTNode *create_ast_inc_op(ASTNode *target, int is_dec, int line, int col) {
    ASTNode *node = create_ast(AST_INC_OP, line, col);
    node->data.inc_op.target = target;
    node->data.inc_op.is_dec = is_dec;
    return node;
}

ASTNode *create_ast_swap(ASTNode *l, ASTNode *r, int line, int col) {
    ASTNode *node = create_ast(AST_SWAP, line, col);
    node->data.swap_stmt.left = l;
    node->data.swap_stmt.right = r;
    return node;
}

ASTNode *create_ast_test(char *name, ASTNode *body, int line, int col) {
    ASTNode *node = create_ast(AST_TEST, line, col);
    node->data.test_block.name = name;
    node->data.test_block.body = body;
    return node;
}

ASTNode *create_ast_obj_get(ASTNode *object, char *field, unsigned int hash, int line, int col) {
    ASTNode *node = create_ast(AST_OBJ_GET, line, col);
    node->data.obj_get.object = object;
    node->data.obj_get.field = field;
    node->data.obj_get.field_hash = hash;
    return node;
}

ASTNode *create_ast_obj_set(ASTNode *object, char *field, unsigned int hash, ASTNode *value, int line, int col) {
    ASTNode *node = create_ast(AST_OBJ_SET, line, col);
    node->data.obj_set.object = object;
    node->data.obj_set.field = field;
    node->data.obj_set.field_hash = hash;
    node->data.obj_set.value = value;
    return node;
}

ASTNode *create_ast_block(ASTNode **items, int count, int line, int col) {
    ASTNode *node = create_ast(AST_BLOCK, line, col);
    node->data.block.items = items;
    node->data.block.count = count;
    return node;
}

ASTNode *create_if_block(ASTNode *condition, ASTNode *body, ASTNode *else_block, int line, int col) {
    ASTNode *node = create_ast(AST_IF_BLOCK, line, col);
    node->data.if_block.condition = condition;
    node->data.if_block.body = body;
    node->data.if_block.else_body = else_block;
    return node;
}

ASTNode *create_while_block(ASTNode *condition, ASTNode *body, int line, int col) {
    ASTNode *node = create_ast(AST_WHILE, line, col);
    node->data.while_block.condition = condition;
    node->data.while_block.body = body;
    return node;
}

ASTNode *create_ast_for(ASTNode *init, ASTNode *condition, ASTNode *step, ASTNode *body, int line, int col) {
    ASTNode *node = create_ast(AST_FOR, line, col);
    node->data.for_block.init = init;
    node->data.for_block.condition = condition;
    node->data.for_block.step = step;
    node->data.for_block.body = body;
    return node;
}

ASTNode *create_ast_return(ASTNode *value, int line, int col) {
    ASTNode *node = create_ast(AST_RETURN, line, col);
    node->data.return_stmt.value = value;
    return node;
}

ASTNode *create_ast_func_decl(char *name, unsigned int name_hash, char **params, unsigned int *param_hashes, int param_count, ASTNode *body, int line, int col) {
    ASTNode *node = create_ast(AST_FUNC_DECL, line, col);
    node->data.func_decl.name = name;
    node->data.func_decl.name_hash = name_hash;
    node->data.func_decl.params = params;
    node->data.func_decl.param_hashes = param_hashes;
    node->data.func_decl.param_count = param_count;
    node->data.func_decl.body = body;
    return node;
}

ASTNode *create_ast_func_call(char *name, unsigned int id_hash, ASTNode **args, int arg_count, int line, int col) {
    ASTNode *node = create_ast(AST_FUNC_CALL, line, col);
    node->data.func_call.name = name;
    node->data.func_call.id_hash = id_hash;
    node->data.func_call.args = args;
    node->data.func_call.arg_count = arg_count;
    return node;
}

ASTNode *create_ast_method_call(ASTNode *target, char *name, unsigned int id_hash, ASTNode **args, int arg_count, int line, int col) {
    ASTNode *node = create_ast(AST_METHOD_CALL, line, col);
    node->data.method_call.target = target;
    node->data.method_call.method_name = name;
    node->data.method_call.method_hash = id_hash;
    node->data.method_call.args = args;
    node->data.method_call.arg_count = arg_count;
    return node;
}

ASTNode *create_ast_entity(char *name, unsigned int hash, char **params, unsigned int *param_hashes, int param_count, ASTNode *init_body, ASTNode **methods, int method_count, int line, int col) {
    ASTNode *node = create_ast(AST_ENTITY, line, col);
    node->data.entity.name = name;
    node->data.entity.name_hash = hash;
    node->data.entity.params = params;
    node->data.entity.param_hashes = param_hashes;
    node->data.entity.param_count = param_count;
    node->data.entity.init_body = init_body;
    node->data.entity.methods = methods;
    node->data.entity.method_count = method_count;
    return node;
}

ASTNode *create_ast_extend(int target_type, ASTNode **methods, int method_count, int line, int col) {
    ASTNode *node = create_ast(AST_EXTEND, line, col);
    node->data.extend.target_type = target_type;
    node->data.extend.methods = methods;
    node->data.extend.method_count = method_count;
    return node;
}

ASTNode *create_ast_defer(ASTNode *body, int line, int col) {
    ASTNode *node = create_ast(AST_DEFER, line, col);
    node->data.defer_stmt.body = body;
    return node;
}

ASTNode *create_ast_watch(char *name, unsigned int hash, int line, int col) {
    ASTNode *node = create_ast(AST_WATCH, line, col);
    node->data.watch_stmt.name = name;
    node->data.watch_stmt.hash = hash;
    return node;
}

ASTNode *create_ast_on_change(char *name, unsigned int hash, ASTNode *body, int line, int col) {
    ASTNode *node = create_ast(AST_ON_CHANGE, line, col);
    node->data.on_change_stmt.name = name;
    node->data.on_change_stmt.hash = hash;
    node->data.on_change_stmt.body = body;
    return node;
}

ASTNode *create_ast_using(char *path, char *alias, bool is_legacy_path, bool star_import, int line, int col) {
    ASTNode *node = create_ast(AST_USING, line, col);
    node->data.using_stmt.path = path;
    node->data.using_stmt.alias = alias;
    node->data.using_stmt.is_legacy_path = is_legacy_path;
    node->data.using_stmt.star_import = star_import;
    return node;
}

ASTNode *create_ast_export(ASTNode *decl, char *name_override, int line, int col) {
    ASTNode *node = create_ast(AST_EXPORT, line, col);
    node->data.export_stmt.decl = decl;
    node->data.export_stmt.name_override = name_override;
    return node;
}

ASTNode *create_ast_break(int line, int col) {
    return create_ast(AST_BREAK, line, col);
}

ASTNode *create_ast_continue(int line, int col) {
    return create_ast(AST_CONTINUE, line, col);
}

ASTNode *create_ast_repeat(bool has_iter, char *iter, unsigned int iter_hash, ASTNode *count_expr, ASTNode *body, int line, int col) {
    ASTNode *node = create_ast(AST_REPEAT, line, col);
    node->data.repeat_stmt.has_iterator = has_iter;
    node->data.repeat_stmt.iter_name = iter;
    node->data.repeat_stmt.iter_hash = iter_hash;
    node->data.repeat_stmt.count_expr = count_expr;
    node->data.repeat_stmt.body = body;
    return node;
}

ASTNode *create_ast_switch(ASTNode *value,
                           ASTNode **case_values,
                           ASTNode **case_bodies,
                           int case_count,
                           ASTNode *default_body,
                           int line,
                           int col) {
    ASTNode *node = create_ast(AST_SWITCH, line, col);
    node->data.switch_stmt.value = value;
    node->data.switch_stmt.case_values = case_values;
    node->data.switch_stmt.case_bodies = case_bodies;
    node->data.switch_stmt.case_count = case_count;
    node->data.switch_stmt.default_body = default_body;
    return node;
}

ASTNode *create_ast_output(ASTNode **args, int count, bool is_newline, int line, int col) {
    ASTNode *node = create_ast(AST_OUTPUT, line, col);
    node->data.output.args = args;
    node->data.output.arg_count = count;
    node->data.output.is_newline = is_newline;
    return node;
}

static void free_string_array(char **items, int count) {
    if (items != NULL) {
        for (int i = 0; i < count; i++) {
            free(items[i]);
        }
        free(items);
    }
}

typedef struct {
    ASTNode **items;
    size_t count;
    size_t capacity;
} ASTDeleteStack;

static void ast_delete_stack_push(ASTDeleteStack *stack, ASTNode *node) {
    if (node == NULL) {
        return;
    }

    if (stack->count >= stack->capacity) {
        size_t new_capacity = stack->capacity == 0 ? 64 : stack->capacity * 2;
        ASTNode **new_items = (ASTNode **)realloc(stack->items, sizeof(ASTNode *) * new_capacity);
        if (new_items == NULL) {
            fprintf(stderr, "[MKS AST Error] Out of memory while growing delete stack\n");
            exit(1);
        }
        stack->items = new_items;
        stack->capacity = new_capacity;
    }

    stack->items[stack->count++] = node;
}

static void ast_delete_stack_free(ASTDeleteStack *stack) {
    free(stack->items);
    stack->items = NULL;
    stack->count = 0;
    stack->capacity = 0;
}

void delete_ast_node(ASTNode *root) {
    if (root == NULL) {
        return;
    }

    ASTDeleteStack stack = {0};
    ast_delete_stack_push(&stack, root);

    while (stack.count > 0) {
        ASTNode *node = stack.items[--stack.count];
        if (node == NULL) {
            continue;
        }

        switch (node->type) {
            case AST_BLOCK:
                for (int i = node->data.block.count - 1; i >= 0; i--) {
                    ast_delete_stack_push(&stack, node->data.block.items[i]);
                }
                free(node->data.block.items);
                break;

            case AST_IF_BLOCK:
                ast_delete_stack_push(&stack, node->data.if_block.else_body);
                ast_delete_stack_push(&stack, node->data.if_block.body);
                ast_delete_stack_push(&stack, node->data.if_block.condition);
                break;

            case AST_WHILE:
                ast_delete_stack_push(&stack, node->data.while_block.body);
                ast_delete_stack_push(&stack, node->data.while_block.condition);
                break;

            case AST_BINOP:
                ast_delete_stack_push(&stack, node->data.binop.right);
                ast_delete_stack_push(&stack, node->data.binop.left);
                break;

            case AST_VAR_DECL:
                free(node->data.var_decl.name);
                ast_delete_stack_push(&stack, node->data.var_decl.value);
                break;

            case AST_ASSIGN:
                free(node->data.assign.name);
                ast_delete_stack_push(&stack, node->data.assign.value);
                break;

            case AST_IDENTIFIER:
                free(node->data.identifier.name);
                break;

            case AST_STRING:
                free(node->data.string_value);
                break;

            case AST_NULL:
            case AST_BOOL:
            case AST_NUMBER:
                break;

            case AST_OUTPUT:
                for (int i = node->data.output.arg_count - 1; i >= 0; i--) {
                    ast_delete_stack_push(&stack, node->data.output.args[i]);
                }
                free(node->data.output.args);
                break;

            case AST_RETURN:
                ast_delete_stack_push(&stack, node->data.return_stmt.value);
                break;

            case AST_FUNC_DECL:
                free(node->data.func_decl.name);
                free_string_array(node->data.func_decl.params, node->data.func_decl.param_count);
                free(node->data.func_decl.param_hashes);
                ast_delete_stack_push(&stack, node->data.func_decl.body);
                break;

            case AST_FUNC_CALL:
                free(node->data.func_call.name);
                for (int i = node->data.func_call.arg_count - 1; i >= 0; i--) {
                    ast_delete_stack_push(&stack, node->data.func_call.args[i]);
                }
                free(node->data.func_call.args);
                break;

            case AST_FOR:
                ast_delete_stack_push(&stack, node->data.for_block.body);
                ast_delete_stack_push(&stack, node->data.for_block.step);
                ast_delete_stack_push(&stack, node->data.for_block.condition);
                ast_delete_stack_push(&stack, node->data.for_block.init);
                break;

            case AST_INDEX:
                ast_delete_stack_push(&stack, node->data.index.index);
                ast_delete_stack_push(&stack, node->data.index.target);
                break;

            case AST_ARRAY:
                for (int i = node->data.array.item_count - 1; i >= 0; i--) {
                    ast_delete_stack_push(&stack, node->data.array.items[i]);
                }
                free(node->data.array.items);
                break;

            case AST_METHOD_CALL:
                free(node->data.method_call.method_name);
                for (int i = node->data.method_call.arg_count - 1; i >= 0; i--) {
                    ast_delete_stack_push(&stack, node->data.method_call.args[i]);
                }
                free(node->data.method_call.args);
                ast_delete_stack_push(&stack, node->data.method_call.target);
                break;

            case AST_OBJ_GET:
                free(node->data.obj_get.field);
                ast_delete_stack_push(&stack, node->data.obj_get.object);
                break;

            case AST_OBJ_SET:
                free(node->data.obj_set.field);
                ast_delete_stack_push(&stack, node->data.obj_set.value);
                ast_delete_stack_push(&stack, node->data.obj_set.object);
                break;

            case AST_SWAP:
                ast_delete_stack_push(&stack, node->data.swap_stmt.right);
                ast_delete_stack_push(&stack, node->data.swap_stmt.left);
                break;

            case AST_TEST:
                free(node->data.test_block.name);
                ast_delete_stack_push(&stack, node->data.test_block.body);
                break;

            case AST_ENTITY:
                free(node->data.entity.name);
                free_string_array(node->data.entity.params, node->data.entity.param_count);
                free(node->data.entity.param_hashes);
                ast_delete_stack_push(&stack, node->data.entity.init_body);
                for (int i = node->data.entity.method_count - 1; i >= 0; i--) {
                    ast_delete_stack_push(&stack, node->data.entity.methods[i]);
                }
                free(node->data.entity.methods);
                break;

            case AST_EXTEND:
                for (int i = node->data.extend.method_count - 1; i >= 0; i--) {
                    ast_delete_stack_push(&stack, node->data.extend.methods[i]);
                }
                free(node->data.extend.methods);
                break;

            case AST_USING:
                free(node->data.using_stmt.path);
                free(node->data.using_stmt.alias);
                break;

            case AST_EXPORT:
                free(node->data.export_stmt.name_override);
                ast_delete_stack_push(&stack, node->data.export_stmt.decl);
                break;

            case AST_DEFER:
                ast_delete_stack_push(&stack, node->data.defer_stmt.body);
                break;

            case AST_WATCH:
                free(node->data.watch_stmt.name);
                break;

            case AST_ON_CHANGE:
                free(node->data.on_change_stmt.name);
                ast_delete_stack_push(&stack, node->data.on_change_stmt.body);
                break;

            case AST_REPEAT:
                free(node->data.repeat_stmt.iter_name);
                ast_delete_stack_push(&stack, node->data.repeat_stmt.body);
                ast_delete_stack_push(&stack, node->data.repeat_stmt.count_expr);
                break;

            case AST_SWITCH:
                ast_delete_stack_push(&stack, node->data.switch_stmt.default_body);
                for (int i = node->data.switch_stmt.case_count - 1; i >= 0; i--) {
                    ast_delete_stack_push(&stack, node->data.switch_stmt.case_bodies[i]);
                    ast_delete_stack_push(&stack, node->data.switch_stmt.case_values[i]);
                }
                free(node->data.switch_stmt.case_values);
                free(node->data.switch_stmt.case_bodies);
                break;

            case AST_INDEX_ASSIGN:
                ast_delete_stack_push(&stack, node->data.index_assign.right);
                ast_delete_stack_push(&stack, node->data.index_assign.left);
                break;

            case AST_ADDRESS_OF:
                ast_delete_stack_push(&stack, node->data.address_of.target);
                break;

            case AST_DEREF:
                ast_delete_stack_push(&stack, node->data.deref.target);
                break;

            case AST_DEREF_ASSIGN:
                ast_delete_stack_push(&stack, node->data.deref_assign.value);
                ast_delete_stack_push(&stack, node->data.deref_assign.target);
                break;

            case AST_INC_OP:
                ast_delete_stack_push(&stack, node->data.inc_op.target);
                break;

            default:
                break;
        }

        free(node);
    }

    ast_delete_stack_free(&stack);
}
