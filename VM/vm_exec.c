#include "vm.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../Eval/eval.h"
#include "../GC/gc.h"
#include "../Lexer/lexer.h"
#include "../Utils/hash.h"
#include "../Runtime/context.h"
#include "../Runtime/extension.h"
#include "../Runtime/errors.h"
#include "../Runtime/functions.h"
#include "../Runtime/indexing.h"
#include "../Runtime/methods.h"
#include "../Runtime/module.h"
#include "../Runtime/operators.h"
#include "../Runtime/output.h"
#include "../Runtime/pointers.h"
#include "../Runtime/profiler.h"
#include "../std/watch.h"

typedef enum {
    VM_NATIVE_WRITE = 1,
    VM_NATIVE_WRITELN = 2
} VmNativeId;

typedef struct VMChunkRegistry {
    Chunk *chunk;
    int owned;
    struct VMChunkRegistry *next;
} VMChunkRegistry;

static VMChunkRegistry **vm_chunk_registry_slot(void) {
    return (VMChunkRegistry **)&mks_context_current()->vm_chunk_registry;
}

#define vm_chunk_registry (*vm_chunk_registry_slot())

static void vm_call_callable(VM *vm, RuntimeValue callable, const char *name, int arg_count);

static void chunk_pin_constants_recursive(Chunk *chunk) {
    if (chunk == NULL) {
        return;
    }

    for (int i = 0; i < chunk->constants_count; i++) {
        if (gc_value_needs_root(&chunk->constants[i])) {
            gc_pin_root(&chunk->constants[i]);
        }
    }
    for (int i = 0; i < chunk->function_count; i++) {
        chunk_pin_constants_recursive(chunk->functions[i].chunk);
    }
}

static void chunk_unpin_constants_recursive(Chunk *chunk) {
    if (chunk == NULL) {
        return;
    }

    for (int i = 0; i < chunk->function_count; i++) {
        chunk_unpin_constants_recursive(chunk->functions[i].chunk);
    }
    for (int i = 0; i < chunk->constants_count; i++) {
        if (gc_value_needs_root(&chunk->constants[i])) {
            gc_unpin_root(&chunk->constants[i]);
        }
    }
}

static VMChunkRegistry *vm_find_registered_chunk(Chunk *chunk) {
    for (VMChunkRegistry *entry = vm_chunk_registry; entry != NULL; entry = entry->next) {
        if (entry->chunk == chunk) {
            return entry;
        }
    }
    return NULL;
}

static int vm_chunk_is_owned(const Chunk *chunk) {
    VMChunkRegistry *entry = vm_find_registered_chunk((Chunk *)chunk);
    return entry != NULL && entry->owned;
}

static void vm_register_chunk_internal(Chunk *chunk, int owned) {
    if (chunk == NULL) {
        return;
    }

    VMChunkRegistry *existing = vm_find_registered_chunk(chunk);
    if (existing != NULL) {
        if (owned) {
            if (!existing->owned) {
                chunk_pin_constants_recursive(chunk);
            }
            existing->owned = 1;
        }
        return;
    }

    VMChunkRegistry *entry = (VMChunkRegistry *)malloc(sizeof(VMChunkRegistry));
    if (entry == NULL) {
        runtime_error("Out of memory registering VM chunk");
    }
    entry->chunk = chunk;
    entry->owned = owned;
    entry->next = vm_chunk_registry;
    vm_chunk_registry = entry;
    if (owned) {
        chunk_pin_constants_recursive(chunk);
    }
}

static void vm_unregister_chunk_internal(Chunk *chunk) {
    VMChunkRegistry **cursor = &vm_chunk_registry;
    while (*cursor != NULL) {
        if ((*cursor)->chunk == chunk) {
            VMChunkRegistry *victim = *cursor;
            *cursor = victim->next;
            free(victim);
            return;
        }
        cursor = &(*cursor)->next;
    }
}

static VMFunction *chunk_find_function(Chunk *chunk, const ASTNode *decl_node) {
    if (chunk == NULL || decl_node == NULL) {
        return NULL;
    }

    for (int i = 0; i < chunk->function_count; i++) {
        if (chunk->functions[i].decl_node == decl_node) {
            return &chunk->functions[i];
        }
        VMFunction *nested = chunk_find_function(chunk->functions[i].chunk, decl_node);
        if (nested != NULL) {
            return nested;
        }
    }
    return NULL;
}

static int vm_lookup_function(const ASTNode *decl_node, Chunk **root_chunk_out, VMFunction **function_out) {
    if (decl_node == NULL) {
        return 0;
    }

    for (VMChunkRegistry *entry = vm_chunk_registry; entry != NULL; entry = entry->next) {
        VMFunction *function = chunk_find_function(entry->chunk, decl_node);
        if (function != NULL) {
            if (root_chunk_out != NULL) {
                *root_chunk_out = entry->chunk;
            }
            if (function_out != NULL) {
                *function_out = function;
            }
            return 1;
        }
    }

    return 0;
}

static void chunk_push_constant_roots(Chunk *chunk) {
    if (chunk == NULL) {
        return;
    }

    gc_push_root_span(chunk->constants, chunk->constants_count);
    for (int i = 0; i < chunk->function_count; i++) {
        chunk_push_constant_roots(chunk->functions[i].chunk);
    }
}

static void chunk_pop_constant_roots(Chunk *chunk) {
    if (chunk == NULL) {
        return;
    }

    for (int i = chunk->function_count - 1; i >= 0; i--) {
        chunk_pop_constant_roots(chunk->functions[i].chunk);
    }
    if (chunk->constants != NULL && chunk->constants_count > 0) {
        gc_pop_root_span();
    }
}

static VMCallFrame *vm_current_frame(VM *vm) {
    if (vm->frame_count <= 0) {
        runtime_error("VM frame stack underflow");
    }
    return &vm->frames[vm->frame_count - 1];
}

static Environment *vm_frame_env(const VMCallFrame *frame) {
    if (frame->scope_depth > 0) {
        return frame->scope_envs[frame->scope_depth - 1];
    }
    return frame->base_env;
}

static uint16_t vm_read_u16(VMCallFrame *frame) {
    const uint16_t high = *frame->ip++;
    const uint16_t low = *frame->ip++;
    return (uint16_t)((high << 8) | low);
}

static uint8_t vm_read_u8(VMCallFrame *frame) {
    return *frame->ip++;
}

static void vm_stack_push(VM *vm, RuntimeValue value) {
    if ((size_t)(vm->sp - vm->stack) >= (sizeof(vm->stack) / sizeof(vm->stack[0]))) {
        runtime_error("VM stack overflow");
    }
    *vm->sp++ = value;
}

static RuntimeValue vm_stack_pop(VM *vm) {
    if (vm->sp <= vm->stack) {
        runtime_error("VM stack underflow");
    }
    vm->sp--;
    return *vm->sp;
}

static RuntimeValue vm_stack_peek(VM *vm, int distance) {
    if (distance < 0 || vm->sp - distance - 1 < vm->stack) {
        runtime_error("VM stack access out of range");
    }
    return vm->sp[-distance - 1];
}

static RuntimeValue vm_read_name_constant(VMCallFrame *frame) {
    const RuntimeValue value = frame->chunk->constants[vm_read_u16(frame)];
    if (value.type != VAL_STRING || value.data.managed_string == NULL) {
        runtime_error("VM expected string name constant");
    }
    return value;
}

static void vm_export_global(Environment *env, const char *name) {
    RuntimeValue value = env_get_fast(env, name, get_hash(name));
    RuntimeValue exports_val;
    if (!env_try_get(env, "exports", get_hash("exports"), &exports_val)) {
        runtime_error("Internal: exports not found in module env");
    }
    if (exports_val.type != VAL_MODULE) {
        runtime_error("Internal: exports is not module");
    }
    env_set_fast(exports_val.data.obj_env, name, get_hash(name), value);
}

static void vm_run_defer_scope(VMCallFrame *frame) {
    if (frame->defer_scope_depth <= 0) {
        runtime_error("VM defer scope stack underflow");
    }

    const int start = frame->defer_scope_starts[frame->defer_scope_depth - 1];
    for (int i = frame->defer_count - 1; i >= start; i--) {
        (void)vm_run_ast(frame->defer_items[i].env, (ASTNode *)frame->defer_items[i].body);
        gc_unpin_env(frame->defer_items[i].env);
    }
    frame->defer_count = start;
    frame->defer_scope_depth--;
}

static RuntimeValue vm_get_field(RuntimeValue obj, const char *field, unsigned int hash) {
    if (obj.type != VAL_OBJECT && obj.type != VAL_MODULE) {
        runtime_error("Property access on non-object");
    }

    RuntimeValue value;
    if (!env_try_get(obj.data.obj_env, field, hash, &value)) {
        if (obj.type == VAL_MODULE) {
            runtime_error("Module has no exported symbol '%s'", field);
        }
        return make_null();
    }

    return value;
}

static RuntimeValue vm_set_field(RuntimeValue obj, const char *field, unsigned int hash, RuntimeValue value) {
    if (obj.type != VAL_OBJECT && obj.type != VAL_MODULE) {
        runtime_error("Property set on non-object");
    }
    if (obj.type == VAL_MODULE) {
        runtime_error("Cannot assign to module export '%s'", field);
    }

    env_set_fast(obj.data.obj_env, field, hash, value);
    return value;
}

static void vm_unwind_frame(VMCallFrame *frame) {
    while (frame->defer_scope_depth > 0) {
        vm_run_defer_scope(frame);
    }
    while (frame->scope_depth > 0) {
        gc_pop_env();
        frame->scope_depth--;
    }
    if (frame->owns_base_env) {
        gc_pop_env();
    }
    gc_pop_root_span();
}

static void vm_push_frame(VM *vm, Chunk *chunk, Environment *env, int owns_base_env) {
    if (vm->frame_count >= 64) {
        runtime_error("VM call frame overflow");
    }

    VMCallFrame *frame = &vm->frames[vm->frame_count++];
    frame->chunk = chunk;
    frame->ip = chunk->code;
    frame->base_env = env;
    frame->owns_base_env = owns_base_env;
    for (size_t i = 0; i < sizeof(frame->locals) / sizeof(frame->locals[0]); i++) {
        frame->locals[i] = make_null();
    }
    frame->scope_depth = 0;
    frame->defer_scope_depth = 0;
    frame->defer_count = 0;
    gc_push_root_span(frame->locals, (int)(sizeof(frame->locals) / sizeof(frame->locals[0])));
}

static void vm_drop_bootstrap_frame(VM *vm) {
    if (vm == NULL || vm->frame_count != 1) {
        runtime_error("VM bootstrap frame state is invalid");
    }

    VMCallFrame *frame = &vm->frames[0];
    if (frame->owns_base_env || frame->scope_depth != 0 || frame->defer_scope_depth != 0 || frame->defer_count != 0) {
        runtime_error("VM bootstrap frame is not empty");
    }

    gc_pop_root_span();
    vm->frame_count = 0;
}

static void vm_push_scope_env(VMCallFrame *frame) {
    if (frame->scope_depth >= 64) {
        runtime_error("VM scope depth overflow");
    }

    Environment *child = env_create_child(vm_frame_env(frame));
    gc_push_env(child);
    frame->scope_envs[frame->scope_depth++] = child;
}

static void vm_pop_scope_env(VMCallFrame *frame) {
    if (frame->scope_depth <= 0) {
        runtime_error("VM scope stack underflow");
    }

    frame->scope_depth--;
    gc_pop_env();
}

static RuntimeValue vm_native_call(uint8_t native_id, const RuntimeValue *args, int arg_count) {
    switch (native_id) {
        case VM_NATIVE_WRITE:
            return runtime_write_values(args, arg_count, false);
        case VM_NATIVE_WRITELN:
            return runtime_write_values(args, arg_count, true);
        default:
            runtime_error("Unsupported VM native call");
    }
    return make_null();
}

static void vm_handle_get_field(VM *vm, VMCallFrame *frame) {
    RuntimeValue name = vm_read_name_constant(frame);
    RuntimeValue obj = vm_stack_pop(vm);
    PROFILER_ON_VM_HOTSPOT(VM_HOT_FIELD_READ);
    vm_stack_push(vm, vm_get_field(obj, name.data.managed_string->data, name.data.managed_string->hash));
}

static void vm_handle_set_field(VM *vm, VMCallFrame *frame) {
    RuntimeValue name = vm_read_name_constant(frame);
    RuntimeValue value = vm_stack_pop(vm);
    RuntimeValue obj = vm_stack_pop(vm);
    PROFILER_ON_VM_HOTSPOT(VM_HOT_FIELD_WRITE);
    vm_stack_push(vm, vm_set_field(obj, name.data.managed_string->data, name.data.managed_string->hash, value));
}

static void vm_handle_import(VM *vm, VMCallFrame *frame) {
    const uint16_t spec_index = vm_read_u16(frame);
    const uint16_t alias_index = vm_read_u16(frame);
    const int is_legacy_path = *frame->ip++;
    const int star_import = *frame->ip++;
    RuntimeValue spec = frame->chunk->constants[spec_index];
    const char *alias = NULL;
    if (alias_index != 0xffff) {
        RuntimeValue alias_value = frame->chunk->constants[alias_index];
        alias = alias_value.data.managed_string->data;
    }

    const char *path = spec.data.managed_string->data;
    if (is_legacy_path && path != NULL) {
        /* Warn about bare imports like "foo" (not "./foo", "file.mks", or "std.foo") */
        if (path[0] != '.' && path[0] != '/' &&
            strncmp(path, "std.", 4) != 0 &&
            strncmp(path, "pkg.", 4) != 0 &&
            strncmp(path, "native.", 7) != 0 &&
            strchr(path, '.') == NULL) { /* No extension, not a file path */
            fprintf(stderr, "[MKS Warning] bare import \"%s\" is ambiguous\n", path);
            fprintf(stderr, "  Use: std.%s, pkg.%s, or ./%s\n", path, path, path);
            fprintf(stderr, "  This will be required in MKS 0.4\n");
            fflush(stderr);
        }
    }

    PROFILER_ON_VM_HOTSPOT(VM_HOT_IMPORT);
    vm_stack_push(vm,
                  module_import(path,
                                alias,
                                is_legacy_path != 0,
                                star_import != 0,
                                vm_frame_env(frame)));
}

static void vm_handle_named_call(VM *vm, VMCallFrame *frame) {
    RuntimeValue name = vm_read_name_constant(frame);
    const int arg_count = (int)(*frame->ip++);
    const char *text = name.data.managed_string->data;
    PROFILER_ON_VM_HOTSPOT(VM_HOT_NAMED_CALL);
    RuntimeValue callable = env_get_fast(vm_frame_env(frame), text, name.data.managed_string->hash);
    vm_call_callable(vm, callable, text, arg_count);
}

static void vm_handle_method_call(VM *vm, VMCallFrame *frame) {
    RuntimeValue name = vm_read_name_constant(frame);
    const int arg_count = (int)(*frame->ip++);
    RuntimeValue *args = vm->sp - arg_count;
    RuntimeValue target = *(args - 1);
    PROFILER_ON_VM_HOTSPOT(VM_HOT_METHOD_CALL);
    RuntimeValue result = runtime_call_method(target,
                                              name.data.managed_string->data,
                                              get_hash(name.data.managed_string->data),
                                              args,
                                              arg_count,
                                              vm_frame_env(frame));
    vm->sp -= (arg_count + 1);
    vm_stack_push(vm, result);
}

static void vm_handle_native_call(VM *vm, VMCallFrame *frame) {
    const uint8_t native_id = *frame->ip++;
    const int arg_count = (int)(*frame->ip++);
    PROFILER_ON_VM_HOTSPOT(VM_HOT_NATIVE_CALL);
    RuntimeValue result = vm_native_call(native_id, vm->sp - arg_count, arg_count);
    vm->sp -= arg_count;
    vm_stack_push(vm, result);
}

static void vm_push_function_frame(VM *vm,
                                   RuntimeValue callable,
                                   const char *name,
                                   const RuntimeValue *args,
                                   int arg_count,
                                   const RuntimeValue *self_value) {
    if (callable.type != VAL_FUNC) {
        runtime_error("'%s' is not a function.", name);
    }

    const ASTNode *decl = callable.data.func.node;
    const int param_count = decl->data.func_decl.param_count;
    if (arg_count != param_count) {
        runtime_error("Function '%s' expects %d arguments, got %d", name, param_count, arg_count);
    }

    Chunk *root_chunk = NULL;
    VMFunction *function = NULL;
    if (!vm_lookup_function(decl, &root_chunk, &function) || function == NULL || root_chunk == NULL) {
        runtime_error("VM function '%s' is not compiled", name);
    }

    Environment *local_env = env_create_child(callable.data.func.closure_env);
    gc_push_env(local_env);
    if (self_value != NULL && function->self_local_slot < 0) {
        env_set(local_env, "self", *self_value);
    }

    if (vm->root_chunk == NULL) {
        vm->root_chunk = root_chunk;
    }
    vm_push_frame(vm, function->chunk, local_env, 1);

    VMCallFrame *frame = vm_current_frame(vm);
    if (function->self_local_slot >= 0) {
        RuntimeValue self_local = make_null();
        if (self_value != NULL) {
            self_local = *self_value;
        } else {
            RuntimeValue resolved_self;
            if (env_try_get(callable.data.func.closure_env, "self", get_hash("self"), &resolved_self)) {
                self_local = resolved_self;
            }
        }
        frame->locals[function->self_local_slot] = self_local;
    }
    for (int i = 0; i < param_count; i++) {
        const int slot = function->param_local_slots[i];
        if (slot >= 0 && slot < (int)(sizeof(frame->locals) / sizeof(frame->locals[0]))) {
            frame->locals[slot] = args[i];
        } else {
            env_set(local_env, decl->data.func_decl.params[i], args[i]);
        }
    }
}

static RuntimeValue vm_apply_binary(OpCode opcode, RuntimeValue left, RuntimeValue right) {
    if (left.type == VAL_INT && right.type == VAL_INT) {
        const int64_t li = left.data.int_value;
        const int64_t ri = right.data.int_value;

        switch (opcode) {
            case OP_ADD: return make_int(li + ri);
            case OP_SUB: return make_int(li - ri);
            case OP_MUL: return make_int(li * ri);
            case OP_DIV:
                if (ri == 0) {
                    runtime_error("division by zero");
                }
                return make_float((double)li / (double)ri);
            case OP_MOD:
                if (ri == 0) {
                    runtime_error("modulo by zero");
                }
                return make_int(li % ri);
            case OP_EQ: return make_bool(li == ri);
            case OP_NEQ: return make_bool(li != ri);
            case OP_LT: return make_bool(li < ri);
            case OP_GT: return make_bool(li > ri);
            case OP_LE: return make_bool(li <= ri);
            case OP_GE: return make_bool(li >= ri);
            default:
                break;
        }
    }

    if ((left.type == VAL_INT || left.type == VAL_FLOAT) &&
        (right.type == VAL_INT || right.type == VAL_FLOAT)) {
        const double l = left.type == VAL_INT ? (double)left.data.int_value : left.data.float_value;
        const double r = right.type == VAL_INT ? (double)right.data.int_value : right.data.float_value;

        switch (opcode) {
            case OP_ADD: return make_float(l + r);
            case OP_SUB: return make_float(l - r);
            case OP_MUL: return make_float(l * r);
            case OP_DIV:
                if (r == 0.0) {
                    runtime_error("division by zero");
                }
                return make_float(l / r);
            case OP_MOD:
                if (r == 0.0) {
                    runtime_error("modulo by zero");
                }
                return make_float(fmod(l, r));
            case OP_EQ: return make_bool(l == r);
            case OP_NEQ: return make_bool(l != r);
            case OP_LT: return make_bool(l < r);
            case OP_GT: return make_bool(l > r);
            case OP_LE: return make_bool(l <= r);
            case OP_GE: return make_bool(l >= r);
            default:
                break;
        }
    }

    switch (opcode) {
        case OP_ADD: return runtime_apply_binop(TOKEN_PLUS, left, right);
        case OP_SUB: return runtime_apply_binop(TOKEN_MINUS, left, right);
        case OP_MUL: return runtime_apply_binop(TOKEN_STAR, left, right);
        case OP_DIV: return runtime_apply_binop(TOKEN_SLASH, left, right);
        case OP_MOD: return runtime_apply_binop(TOKEN_MOD, left, right);
        case OP_EQ: return runtime_apply_binop(TOKEN_EQ, left, right);
        case OP_NEQ: return runtime_apply_binop(TOKEN_NOT_EQ, left, right);
        case OP_LT: return runtime_apply_binop(TOKEN_LESS, left, right);
        case OP_GT: return runtime_apply_binop(TOKEN_GREATER, left, right);
        case OP_LE: return runtime_apply_binop(TOKEN_LESS_EQUAL, left, right);
        case OP_GE: return runtime_apply_binop(TOKEN_GREATER_EQUAL, left, right);
        default:
            runtime_error("Unsupported VM binary opcode");
    }
    return make_null();
}

static void vm_call_callable(VM *vm, RuntimeValue callable, const char *name, int arg_count) {
    RuntimeValue *args = vm->sp - arg_count;
    if (args < vm->stack) {
        runtime_error("VM call stack underflow");
    }

    if (callable.type == VAL_NATIVE_FUNC) {
        RuntimeValue result = callable.data.native.fn(mks_context_current(), args, arg_count);
        vm->sp -= arg_count;
        vm_stack_push(vm, result);
        return;
    }

    if (callable.type == VAL_BLUEPRINT) {
        RuntimeValue result = eval_blueprint_construct(callable, args, arg_count, vm_frame_env(vm_current_frame(vm)));
        vm->sp -= arg_count;
        vm_stack_push(vm, result);
        return;
    }

    vm->sp -= arg_count;
    vm_push_function_frame(vm, callable, name, args, arg_count, NULL);
}

void vm_init(VM *vm, Chunk *root_chunk, Environment *env) {
    if (vm == NULL) {
        return;
    }

    memset(vm, 0, sizeof(*vm));
    vm->sp = vm->stack;
    vm->root_chunk = root_chunk;
    vm->global_env = env;
    vm->gc_tick = 4096;
    vm_push_frame(vm, root_chunk, env, 0);
}

RuntimeValue vm_run(VM *vm) {
    if (vm == NULL || vm->root_chunk == NULL) {
        return make_null();
    }

    const int root_already_registered = vm_find_registered_chunk(vm->root_chunk) != NULL;
    if (!root_already_registered) {
        vm_register_chunk_internal(vm->root_chunk, 0);
    }
    for (size_t i = 0; i < sizeof(vm->stack) / sizeof(vm->stack[0]); i++) {
        vm->stack[i] = make_null();
    }

    gc_push_root_span(vm->stack, (int)(sizeof(vm->stack) / sizeof(vm->stack[0])));
    const int need_chunk_spans = !vm_chunk_is_owned(vm->root_chunk);
    if (need_chunk_spans) {
        chunk_push_constant_roots(vm->root_chunk);
    }

    while (vm->frame_count > 0) {
        VMCallFrame *frame = vm_current_frame(vm);
        if (--vm->gc_tick <= 0) {
            vm->gc_tick = 4096;
            gc_check(vm_frame_env(frame));
        }

        const OpCode opcode = (OpCode)(*frame->ip++);
        PROFILER_ON_VM_OPCODE(opcode);
        switch (opcode) {
            case OP_CONSTANT:
                vm_stack_push(vm, frame->chunk->constants[vm_read_u16(frame)]);
                break;

            case OP_NULL:
                vm_stack_push(vm, make_null());
                break;

            case OP_DEFINE_FUNCTION: {
                const uint16_t function_index = vm_read_u16(frame);
                RuntimeValue name = vm_read_name_constant(frame);
                VMFunction *function = &frame->chunk->functions[function_index];
                RuntimeValue value;
                value.type = VAL_FUNC;
                value.original_type = VAL_FUNC;
                value.data.func.node = (ASTNode *)function->decl_node;
                value.data.func.closure_env = vm_frame_env(frame);
                env_set_fast(vm_frame_env(frame),
                             name.data.managed_string->data,
                             get_hash(name.data.managed_string->data),
                             value);
                break;
            }

            case OP_DEFINE_BLUEPRINT: {
                const uint16_t ast_ref = vm_read_u16(frame);
                RuntimeValue name = vm_read_name_constant(frame);
                RuntimeValue value = make_blueprint(frame->chunk->ast_refs[ast_ref], vm_frame_env(frame));
                env_set_fast(vm_frame_env(frame),
                             name.data.managed_string->data,
                             get_hash(name.data.managed_string->data),
                             value);
                vm_stack_push(vm, value);
                break;
            }

            case OP_REGISTER_EXTENSION: {
                const uint16_t ast_ref = vm_read_u16(frame);
                register_extension(frame->chunk->ast_refs[ast_ref], vm_frame_env(frame));
                vm_stack_push(vm, make_null());
                break;
            }

            case OP_DEFINE_GLOBAL: {
                RuntimeValue name = vm_read_name_constant(frame);
                const char *text = name.data.managed_string->data;
                PROFILER_ON_VM_HOTSPOT(VM_HOT_GLOBAL_WRITE);
                env_set_fast(vm_frame_env(frame), text, name.data.managed_string->hash, vm_stack_peek(vm, 0));
                break;
            }

            case OP_DEFINE_LOCAL: {
                const uint8_t slot = vm_read_u8(frame);
                frame->locals[slot] = vm_stack_peek(vm, 0);
                break;
            }

            case OP_EXPORT_GLOBAL: {
                RuntimeValue name = vm_read_name_constant(frame);
                vm_export_global(vm_frame_env(frame), name.data.managed_string->data);
                break;
            }

            case OP_GET_GLOBAL: {
                RuntimeValue name = vm_read_name_constant(frame);
                const char *text = name.data.managed_string->data;
                PROFILER_ON_VM_HOTSPOT(VM_HOT_GLOBAL_READ);
                vm_stack_push(vm, env_get_fast(vm_frame_env(frame), text, name.data.managed_string->hash));
                break;
            }

            case OP_GET_LOCAL: {
                const uint8_t slot = vm_read_u8(frame);
                vm_stack_push(vm, frame->locals[slot]);
                break;
            }

            case OP_SET_GLOBAL: {
                RuntimeValue name = vm_read_name_constant(frame);
                const char *text = name.data.managed_string->data;
                PROFILER_ON_VM_HOTSPOT(VM_HOT_GLOBAL_WRITE);
                env_update_fast(vm_frame_env(frame), text, name.data.managed_string->hash, vm_stack_peek(vm, 0));
                break;
            }

            case OP_SET_LOCAL: {
                const uint8_t slot = vm_read_u8(frame);
                frame->locals[slot] = vm_stack_peek(vm, 0);
                break;
            }

            case OP_INC_LOCAL: {
                const uint8_t slot = vm_read_u8(frame);
                RuntimeValue *v = &frame->locals[slot];
                if (v->type == VAL_INT) {
                    v->data.int_value += 1;
                } else if (v->type == VAL_FLOAT) {
                    v->data.float_value += 1.0;
                } else {
                    runtime_error("OP_INC_LOCAL expects number");
                }
                break;
            }

            case OP_ADD_LOCAL_CONST: {
                const uint8_t slot = vm_read_u8(frame);
                const uint16_t const_idx = vm_read_u16(frame);
                RuntimeValue *v = &frame->locals[slot];
                const RuntimeValue *c = &frame->chunk->constants[const_idx];
                if (v->type == VAL_INT && c->type == VAL_INT) {
                    v->data.int_value += c->data.int_value;
                } else if (v->type == VAL_INT && c->type == VAL_FLOAT) {
                    v->type = VAL_FLOAT;
                    v->data.float_value = (double)v->data.int_value + c->data.float_value;
                } else if (v->type == VAL_FLOAT && c->type == VAL_INT) {
                    v->data.float_value += (double)c->data.int_value;
                } else if (v->type == VAL_FLOAT && c->type == VAL_FLOAT) {
                    v->data.float_value += c->data.float_value;
                } else {
                    runtime_error("OP_ADD_LOCAL_CONST expects number");
                }
                break;
            }
            case OP_ADD_LOCAL_LOCAL: {
                const uint8_t dst = vm_read_u8(frame);
                const uint8_t src = vm_read_u8(frame);

                RuntimeValue *a = &frame->locals[dst];
                const RuntimeValue *b = &frame->locals[src];

                if (a->type == VAL_INT && b->type == VAL_INT) {
                    a->data.int_value += b->data.int_value;
                } else if (a->type == VAL_INT && b->type == VAL_FLOAT) {
                    const double left = (double)a->data.int_value;
                    a->type = VAL_FLOAT;
                    a->original_type = VAL_FLOAT;
                    a->data.float_value = left + b->data.float_value;
                } else if (a->type == VAL_FLOAT && b->type == VAL_INT) {
                    a->data.float_value += (double)b->data.int_value;
                } else if (a->type == VAL_FLOAT && b->type == VAL_FLOAT) {
                    a->data.float_value += b->data.float_value;
                } else {
                    RuntimeValue result = vm_apply_binary(OP_ADD, *a, *b);
                    *a = result;
                }

                break;
            }

            case OP_SUB_LOCAL_CONST: {
                const uint8_t slot = vm_read_u8(frame);
                const uint16_t const_idx = vm_read_u16(frame);
                RuntimeValue *v = &frame->locals[slot];
                const RuntimeValue *c = &frame->chunk->constants[const_idx];
                if (v->type == VAL_INT && c->type == VAL_INT) {
                    v->data.int_value -= c->data.int_value;
                } else if (v->type == VAL_INT && c->type == VAL_FLOAT) {
                    v->type = VAL_FLOAT;
                    v->data.float_value = (double)v->data.int_value - c->data.float_value;
                } else if (v->type == VAL_FLOAT && c->type == VAL_INT) {
                    v->data.float_value -= (double)c->data.int_value;
                } else if (v->type == VAL_FLOAT && c->type == VAL_FLOAT) {
                    v->data.float_value -= c->data.float_value;
                } else {
                    runtime_error("OP_SUB_LOCAL_CONST expects number");
                }
                break;
            }

            case OP_MUL_LOCAL_CONST: {
                const uint8_t slot = vm_read_u8(frame);
                const uint16_t const_idx = vm_read_u16(frame);
                RuntimeValue *v = &frame->locals[slot];
                const RuntimeValue *c = &frame->chunk->constants[const_idx];
                if (v->type == VAL_INT && c->type == VAL_INT) {
                    v->data.int_value *= c->data.int_value;
                } else if (v->type == VAL_INT && c->type == VAL_FLOAT) {
                    v->type = VAL_FLOAT;
                    v->data.float_value = (double)v->data.int_value * c->data.float_value;
                } else if (v->type == VAL_FLOAT && c->type == VAL_INT) {
                    v->data.float_value *= (double)c->data.int_value;
                } else if (v->type == VAL_FLOAT && c->type == VAL_FLOAT) {
                    v->data.float_value *= c->data.float_value;
                } else {
                    runtime_error("OP_MUL_LOCAL_CONST expects number");
                }
                break;
            }
            case OP_DIV_LOCAL_CONST: {
                const uint8_t slot = vm_read_u8(frame);
                const uint16_t const_idx = vm_read_u16(frame);
                RuntimeValue *v = &frame->locals[slot];
                const RuntimeValue *c = &frame->chunk->constants[const_idx];

                if ((c->type == VAL_INT && c->data.int_value == 0) ||
                    (c->type == VAL_FLOAT && c->data.float_value == 0.0)) {
                    runtime_error("division by zero");
                    }

                if (v->type == VAL_INT && c->type == VAL_INT) {
                    v->type = VAL_FLOAT;
                    v->data.float_value = (double)v->data.int_value / (double)c->data.int_value;
                } else if (v->type == VAL_INT && c->type == VAL_FLOAT) {
                    v->type = VAL_FLOAT;
                    v->data.float_value = (double)v->data.int_value / c->data.float_value;
                } else if (v->type == VAL_FLOAT && c->type == VAL_INT) {
                    v->data.float_value /= (double)c->data.int_value;
                } else if (v->type == VAL_FLOAT && c->type == VAL_FLOAT) {
                    v->data.float_value /= c->data.float_value;
                } else {
                    runtime_error("OP_DIV_LOCAL_CONST expects number");
                }
                break;
            }

            case OP_STRING_APPEND_LOCAL_CONST: {
                const uint8_t slot = vm_read_u8(frame);
                const uint16_t const_idx = vm_read_u16(frame);
                RuntimeValue *v = &frame->locals[slot];
                const RuntimeValue *c = &frame->chunk->constants[const_idx];
                if (v->type != VAL_STRING) {
                    runtime_error("OP_STRING_APPEND_LOCAL_CONST expects string");
                }
                if (c->type != VAL_STRING) {
                    runtime_error("OP_STRING_APPEND_LOCAL_CONST expects string constant");
                }
                const char *left_str = v->data.managed_string->data;
                const char *right_str = c->data.managed_string->data;
                const size_t left_len = v->data.managed_string->len;
                const size_t right_len = c->data.managed_string->len;
                char *result = malloc(left_len + right_len + 1);
                if (result == NULL) {
                    runtime_error("Out of memory in string concatenation");
                }
                memcpy(result, left_str, left_len);
                memcpy(result + left_len, right_str, right_len);
                result[left_len + right_len] = '\0';
                *v = make_string_owned(result, left_len + right_len);
                break;
            }

            case OP_BUILDER_START_LOCAL: {
                const uint8_t slot = vm_read_u8(frame);
                frame->locals[slot] = sb_make();
                break;
            }

            case OP_BUILDER_APPEND_LOCAL_CONST: {
                const uint8_t slot = vm_read_u8(frame);
                const uint16_t const_idx = vm_read_u16(frame);
                RuntimeValue *builder = &frame->locals[slot];
                const RuntimeValue *c = &frame->chunk->constants[const_idx];
                if (builder->type != VAL_STRING_BUILDER) {
                    runtime_error("OP_BUILDER_APPEND_LOCAL_CONST expects string builder");
                }
                sb_append_value(builder, c);
                break;
            }

            case OP_BUILDER_APPEND_LOCAL_VALUE: {
                const uint8_t builder_slot = vm_read_u8(frame);
                const uint8_t value_slot = vm_read_u8(frame);
                RuntimeValue *builder = &frame->locals[builder_slot];
                const RuntimeValue *value = &frame->locals[value_slot];
                if (builder->type != VAL_STRING_BUILDER) {
                    runtime_error("OP_BUILDER_APPEND_LOCAL_VALUE expects string builder");
                }
                sb_append_value(builder, value);
                break;
            }

            case OP_BUILDER_FINISH_LOCAL: {
                const uint8_t slot = vm_read_u8(frame);
                RuntimeValue *builder = &frame->locals[slot];
                if (builder->type != VAL_STRING_BUILDER) {
                    runtime_error("OP_BUILDER_FINISH_LOCAL expects string builder");
                }
                *builder = sb_to_string(builder);
                break;
            }

            case OP_JUMP_IF_LOCAL_LT_CONST_FALSE: {
                const uint8_t slot = vm_read_u8(frame);
                const uint16_t const_idx = vm_read_u16(frame);
                const uint16_t offset = vm_read_u16(frame);
                RuntimeValue *v = &frame->locals[slot];
                const RuntimeValue *c = &frame->chunk->constants[const_idx];
                bool lt = false;
                if (v->type == VAL_INT && c->type == VAL_INT) {
                    lt = v->data.int_value < c->data.int_value;
                } else if (v->type == VAL_INT && c->type == VAL_FLOAT) {
                    lt = (double)v->data.int_value < c->data.float_value;
                } else if (v->type == VAL_FLOAT && c->type == VAL_INT) {
                    lt = v->data.float_value < (double)c->data.int_value;
                } else if (v->type == VAL_FLOAT && c->type == VAL_FLOAT) {
                    lt = v->data.float_value < c->data.float_value;
                } else {
                    runtime_error("OP_JUMP_IF_LOCAL_LT_CONST_FALSE expects number");
                }
                if (!lt) {
                    frame->ip += offset;
                }
                break;
            }

            case OP_ADD:
            case OP_SUB:
            case OP_MUL:
            case OP_DIV:
            case OP_MOD:
            case OP_EQ:
            case OP_NEQ:
            case OP_LT:
            case OP_GT:
            case OP_LE:
            case OP_GE: {
                RuntimeValue right = vm_stack_pop(vm);
                RuntimeValue left = vm_stack_pop(vm);
                vm_stack_push(vm, vm_apply_binary(opcode, left, right));
                break;
            }

            case OP_JUMP:
                frame->ip += vm_read_u16(frame);
                break;

            case OP_JUMP_IF_FALSE: {
                const uint16_t offset = vm_read_u16(frame);
                RuntimeValue condition = vm_stack_pop(vm);
                if (!runtime_value_is_truthy(condition)) {
                    frame->ip += offset;
                }
                break;
            }

            case OP_JUMP_IF_TRUE: {
                const uint16_t offset = vm_read_u16(frame);
                RuntimeValue condition = vm_stack_pop(vm);
                if (runtime_value_is_truthy(condition)) {
                    frame->ip += offset;
                }
                break;
            }

            case OP_LOOP:
                frame->ip -= vm_read_u16(frame);
                break;

            case OP_PUSH_ENV:
                vm_push_scope_env(frame);
                break;

            case OP_POP_ENV:
                vm_pop_scope_env(frame);
                break;

            case OP_PUSH_DEFER_SCOPE:
                if (frame->defer_scope_depth >= 64) {
                    runtime_error("VM defer scope overflow");
                }
                frame->defer_scope_starts[frame->defer_scope_depth++] = frame->defer_count;
                break;

            case OP_ADD_DEFER: {
                const uint16_t ast_ref = vm_read_u16(frame);
                if (frame->defer_count >= 256) {
                    runtime_error("VM defer stack overflow");
                }
                frame->defer_items[frame->defer_count].body = frame->chunk->ast_refs[ast_ref];
                frame->defer_items[frame->defer_count].env = vm_frame_env(frame);
                gc_pin_env(frame->defer_items[frame->defer_count].env);
                frame->defer_count++;
                break;
            }

            case OP_POP_DEFER_SCOPE:
                vm_run_defer_scope(frame);
                break;

            case OP_ARRAY_NEW: {
                const int count = (int)vm_read_u16(frame);
                RuntimeValue array = make_array(count);
                const int rooted = gc_push_root_if_needed(&array);

                for (int i = 0; i < count; i++) {
                    array.data.managed_array->elements[count - i - 1] = vm_stack_pop(vm);
                }
                array.data.managed_array->count = count;

                if (rooted) {
                    gc_pop_root();
                }
                vm_stack_push(vm, array);
                break;
            }

            case OP_GET_INDEX: {
                RuntimeValue index = vm_stack_pop(vm);
                RuntimeValue target = vm_stack_pop(vm);
                vm_stack_push(vm, runtime_get_index(target, index));
                break;
            }

            case OP_SET_INDEX: {
                RuntimeValue value = vm_stack_pop(vm);
                RuntimeValue index = vm_stack_pop(vm);
                RuntimeValue target = vm_stack_pop(vm);
                vm_stack_push(vm, runtime_set_index(target, index, value));
                break;
            }

            case OP_GET_FIELD: {
                vm_handle_get_field(vm, frame);
                break;
            }

            case OP_SET_FIELD: {
                vm_handle_set_field(vm, frame);
                break;
            }

            case OP_TRUTHY: {
                RuntimeValue value = vm_stack_pop(vm);
                vm_stack_push(vm, make_bool(runtime_value_is_truthy(value)));
                break;
            }

            case OP_WATCH: {
                RuntimeValue name = vm_read_name_constant(frame);
                watch_register(name.data.managed_string->data,
                               get_hash(name.data.managed_string->data));
                vm_stack_push(vm, make_null());
                break;
            }

            case OP_WATCH_HANDLER: {
                RuntimeValue name = vm_read_name_constant(frame);
                const uint16_t ast_ref = vm_read_u16(frame);
                watch_register_handler(name.data.managed_string->data,
                                       get_hash(name.data.managed_string->data),
                                       frame->chunk->ast_refs[ast_ref],
                                       vm_frame_env(frame));
                vm_stack_push(vm, make_null());
                break;
            }

            case OP_ADDRESS_OF_VAR: {
                RuntimeValue name = vm_read_name_constant(frame);
                vm_stack_push(vm, runtime_address_of_var(vm_frame_env(frame),
                                                        name.data.managed_string->data,
                                                        get_hash(name.data.managed_string->data)));
                break;
            }

            case OP_ADDRESS_OF_INDEX: {
                RuntimeValue index = vm_stack_pop(vm);
                RuntimeValue container = vm_stack_pop(vm);
                vm_stack_push(vm, runtime_address_of_index(container, index));
                break;
            }

            case OP_ADDRESS_OF_FIELD: {
                RuntimeValue name = vm_read_name_constant(frame);
                RuntimeValue object = vm_stack_pop(vm);
                vm_stack_push(vm, runtime_address_of_field(object,
                                                          name.data.managed_string->data,
                                                          get_hash(name.data.managed_string->data)));
                break;
            }

            case OP_DEREF: {
                RuntimeValue pointer = vm_stack_pop(vm);
                if (pointer.type != VAL_POINTER) {
                    runtime_error("Cannot dereference non-pointer");
                }
                vm_stack_push(vm, runtime_pointer_read(pointer.data.managed_pointer));
                break;
            }

            case OP_DEREF_ASSIGN: {
                RuntimeValue value = vm_stack_pop(vm);
                RuntimeValue pointer = vm_stack_pop(vm);
                if (pointer.type != VAL_POINTER) {
                    runtime_error("Cannot assign through non-pointer");
                }
                vm_stack_push(vm, runtime_pointer_write(pointer.data.managed_pointer, value));
                break;
            }

            case OP_SWAP_PTRS: {
                RuntimeValue right = vm_stack_pop(vm);
                RuntimeValue left = vm_stack_pop(vm);
                if (left.type != VAL_POINTER || right.type != VAL_POINTER) {
                    runtime_error("swap requires assignable lvalues");
                }
                vm_stack_push(vm, runtime_pointer_swap(left.data.managed_pointer, right.data.managed_pointer));
                break;
            }

            case OP_IMPORT: {
                vm_handle_import(vm, frame);
                break;
            }

            case OP_CALL: {
                vm_handle_named_call(vm, frame);
                break;
            }

            case OP_CALL_METHOD: {
                vm_handle_method_call(vm, frame);
                break;
            }

            case OP_CALL_NATIVE: {
                vm_handle_native_call(vm, frame);
                break;
            }

            case OP_TEST_PASS: {
                RuntimeValue name = vm_read_name_constant(frame);
                printf("[PASS] %s\n", name.data.managed_string->data);
                break;
            }

            case OP_POP:
                (void)vm_stack_pop(vm);
                break;

            case OP_RETURN: {
                RuntimeValue result = vm->sp > vm->stack ? vm_stack_pop(vm) : make_null();
                vm_unwind_frame(frame);
                vm->frame_count--;
                if (vm->frame_count == 0) {
                    if (need_chunk_spans) {
                        chunk_pop_constant_roots(vm->root_chunk);
                    }
                    gc_pop_root_span();
                    if (!root_already_registered) {
                        vm_unregister_chunk_internal(vm->root_chunk);
                    }
                    return result;
                }
                vm_stack_push(vm, result);
                break;
            }
        }
    }

    if (need_chunk_spans) {
        chunk_pop_constant_roots(vm->root_chunk);
    }
    gc_pop_root_span();
    if (!root_already_registered) {
        vm_unregister_chunk_internal(vm->root_chunk);
    }
    return make_null();
}

RuntimeValue vm_call_named(Chunk *root_chunk,
                           Environment *env,
                           const char *name,
                           const RuntimeValue *args,
                           int arg_count) {
    RuntimeValue callable = env_get_fast(env, name, get_hash(name));
    if (callable.type != VAL_FUNC && callable.type != VAL_NATIVE_FUNC) {
        runtime_error("'%s' is not a function.", name);
    }

    const int root_already_registered = vm_find_registered_chunk(root_chunk) != NULL;
    if (!root_already_registered) {
        vm_register_chunk_internal(root_chunk, 0);
    }

    VM vm;
    vm_init(&vm, root_chunk, env);
    vm_drop_bootstrap_frame(&vm);
    for (int i = 0; i < arg_count; i++) {
        vm_stack_push(&vm, args[i]);
    }
    vm_call_callable(&vm, callable, name, arg_count);
    RuntimeValue result = vm_run(&vm);
    if (!root_already_registered) {
        vm_unregister_chunk_internal(root_chunk);
    }
    return result;
}

int vm_has_compiled_function(const ASTNode *decl) {
    return vm_lookup_function(decl, NULL, NULL);
}

RuntimeValue vm_call_function_value(RuntimeValue callable,
                                    const RuntimeValue *args,
                                    int arg_count,
                                    const RuntimeValue *self_value) {
    if (callable.type != VAL_FUNC) {
        runtime_error("Value is not a function");
    }

    Chunk *root_chunk = NULL;
    if (!vm_lookup_function(callable.data.func.node, &root_chunk, NULL) || root_chunk == NULL) {
        runtime_error("VM function is not compiled");
    }

    VM vm;
    vm_init(&vm, root_chunk, callable.data.func.closure_env);
    vm_drop_bootstrap_frame(&vm);
    vm_push_function_frame(&vm,
                           callable,
                           callable.data.func.node->data.func_decl.name,
                           args,
                           arg_count,
                           self_value);
    return vm_run(&vm);
}

RuntimeValue vm_run_ast(Environment *env, ASTNode *node) {
    Chunk chunk;
    if (!compile_program(node, &chunk)) {
        runtime_error("VM could not compile runtime AST node");
    }

    VM vm;
    vm_init(&vm, &chunk, env);
    RuntimeValue result = vm_run(&vm);
    chunk_free(&chunk);
    return result;
}

RuntimeValue vm_run_compiled_ast_body(Environment *env, ASTNode *node) {
    Chunk *root_chunk = NULL;
    VMFunction *function = NULL;
    if (!vm_lookup_function(node, &root_chunk, &function) || root_chunk == NULL || function == NULL) {
        runtime_error("VM body is not compiled");
    }

    VM vm;
    vm_init(&vm, root_chunk, env);
    vm_drop_bootstrap_frame(&vm);
    vm_push_frame(&vm, function->chunk, env, 0);
    if (function->self_local_slot >= 0) {
        VMCallFrame *frame = vm_current_frame(&vm);
        RuntimeValue resolved_self;
        if (env_try_get(env, "self", get_hash("self"), &resolved_self)) {
            frame->locals[function->self_local_slot] = resolved_self;
        }
    }
    return vm_run(&vm);
}

void vm_register_owned_chunk(Chunk *chunk) {
    vm_register_chunk_internal(chunk, 1);
}

void vm_free_all(void) {
    VMChunkRegistry *entry = vm_chunk_registry;
    while (entry != NULL) {
        VMChunkRegistry *next = entry->next;
        if (entry->owned && entry->chunk != NULL) {
            chunk_unpin_constants_recursive(entry->chunk);
            chunk_free(entry->chunk);
            free(entry->chunk);
        }
        free(entry);
        entry = next;
    }
    vm_chunk_registry = NULL;
}
