#ifndef MKS_POINTERS_H
#define MKS_POINTERS_H

#include "value.h"
#include "../env/env.h"

RuntimeValue runtime_address_of_var(Environment *env, const char *name, unsigned int hash);
RuntimeValue runtime_address_of_index(RuntimeValue container, RuntimeValue index_value);
RuntimeValue runtime_address_of_field(RuntimeValue object, const char *field, unsigned int hash);
RuntimeValue runtime_pointer_read(ManagedPointer *ptr);
RuntimeValue runtime_pointer_write(ManagedPointer *ptr, RuntimeValue value);
RuntimeValue runtime_pointer_swap(ManagedPointer *left, ManagedPointer *right);

#endif
