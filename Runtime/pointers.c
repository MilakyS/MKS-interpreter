#include "pointers.h"

#include "errors.h"
#include "../GC/gc.h"

RuntimeValue runtime_address_of_var(Environment *env, const char *name, unsigned int hash) {
    Environment *owner = NULL;
    EnvVar *entry = env_get_entry_with_owner(env, name, hash, &owner);
    if (entry == NULL || owner == NULL) {
        runtime_error("Cannot take address of undefined variable '%s'", name);
    }
    return make_pointer_to_var(owner, entry);
}

RuntimeValue runtime_address_of_index(RuntimeValue container, RuntimeValue index_value) {
    if (container.type != VAL_ARRAY) {
        runtime_error("Can only take address of array elements with []");
    }

    const int index = (int)runtime_value_as_int(index_value);
    ManagedArray *arr = container.data.managed_array;
    if (arr == NULL || index < 0 || index >= arr->count) {
        runtime_error("Array pointer index out of bounds");
    }

    return make_pointer_to_array_elem(arr, index);
}

RuntimeValue runtime_address_of_field(RuntimeValue object, const char *field, unsigned int hash) {
    if (object.type != VAL_OBJECT && object.type != VAL_MODULE) {
        runtime_error("Can only take address of object fields");
    }
    if (object.type == VAL_MODULE) {
        runtime_error("Cannot take address of module export '%s'", field);
    }

    Environment *owner = NULL;
    EnvVar *entry = env_get_entry_with_owner(object.data.obj_env, field, hash, &owner);
    if (entry == NULL || owner == NULL) {
        runtime_error("Cannot take address of missing field '%s'", field);
    }

    return make_pointer_to_object_field(owner, field, hash);
}

RuntimeValue runtime_pointer_read(ManagedPointer *ptr) {
    if (ptr == NULL) {
        runtime_error("Cannot dereference null pointer");
    }

    switch (ptr->kind) {
        case PTR_ENV_VAR:
            if (ptr->as.var.entry == NULL) {
                runtime_error("Pointer target variable no longer exists");
            }
            return ptr->as.var.entry->value;

        case PTR_ARRAY_ELEM: {
            ManagedArray *arr = ptr->as.array_elem.array;
            const int index = ptr->as.array_elem.index;
            if (arr == NULL || index < 0 || index >= arr->count) {
                runtime_error("Array pointer index out of bounds");
            }
            return arr->elements[index];
        }

        case PTR_OBJECT_FIELD: {
            RuntimeValue value;
            if (!env_try_get(ptr->as.object_field.env,
                             ptr->as.object_field.field,
                             ptr->as.object_field.hash,
                             &value)) {
                runtime_error("Pointer target field '%s' no longer exists", ptr->as.object_field.field);
            }
            return value;
        }
    }

    runtime_error("Invalid pointer target");
    return make_null();
}

RuntimeValue runtime_pointer_write(ManagedPointer *ptr, RuntimeValue value) {
    if (ptr == NULL) {
        runtime_error("Cannot assign through null pointer");
    }

    if (value.type == VAL_RETURN) {
        value.type = value.original_type;
    }

    switch (ptr->kind) {
        case PTR_ENV_VAR:
            if (ptr->as.var.entry == NULL) {
                runtime_error("Pointer target variable no longer exists");
            }
            gc_write_barrier((GCObject*)ptr->as.var.env, &value);
            ptr->as.var.entry->value = value;
            return value;

        case PTR_ARRAY_ELEM: {
            ManagedArray *arr = ptr->as.array_elem.array;
            const int index = ptr->as.array_elem.index;
            if (arr == NULL || index < 0 || index >= arr->count) {
                runtime_error("Array pointer index out of bounds");
            }
            gc_write_barrier((GCObject*)arr, &value);
            arr->elements[index] = value;
            return value;
        }

        case PTR_OBJECT_FIELD:
            env_set_fast(ptr->as.object_field.env,
                         ptr->as.object_field.field,
                         ptr->as.object_field.hash,
                         value);
            return value;
    }

    runtime_error("Invalid pointer target");
    return make_null();
}

RuntimeValue runtime_pointer_swap(ManagedPointer *left, ManagedPointer *right) {
    RuntimeValue left_value = runtime_pointer_read(left);
    RuntimeValue right_value = runtime_pointer_read(right);
    (void)runtime_pointer_write(left, right_value);
    (void)runtime_pointer_write(right, left_value);
    return make_null();
}
