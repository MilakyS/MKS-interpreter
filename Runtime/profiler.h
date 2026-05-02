#ifndef MKS_PROFILER_H
#define MKS_PROFILER_H

#include "../Parser/AST.h"

typedef enum {
    VM_HOT_GLOBAL_READ,
    VM_HOT_GLOBAL_WRITE,
    VM_HOT_FIELD_READ,
    VM_HOT_FIELD_WRITE,
    VM_HOT_NAMED_CALL,
    VM_HOT_METHOD_CALL,
    VM_HOT_NATIVE_CALL,
    VM_HOT_IMPORT,
    VM_HOT_MAX
} VMProfileHotspot;

void profiler_enable(void);
int  profiler_is_enabled(void);
void profiler_on_eval(ASTNodeType type);
void profiler_on_vm_opcode(int opcode);
void profiler_on_vm_hotspot(VMProfileHotspot hotspot);
void profiler_report(void);

#define PROFILER_ON_EVAL(type) \
    do { \
        if (profiler_is_enabled()) { \
            profiler_on_eval(type); \
        } \
    } while (0)

#define PROFILER_ON_VM_OPCODE(opcode) \
    do { \
        if (profiler_is_enabled()) { \
            profiler_on_vm_opcode((int)(opcode)); \
        } \
    } while (0)

#define PROFILER_ON_VM_HOTSPOT(hotspot) \
    do { \
        if (profiler_is_enabled()) { \
            profiler_on_vm_hotspot((hotspot)); \
        } \
    } while (0)

#endif
