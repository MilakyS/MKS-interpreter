#include "registry.h"
#include "math.h"
#include "time.h"
#include "random.h"
#include "watch.h"
#include "fs.h"
#include "json.h"
#include "path.h"
#include "process.h"
#include "tty.h"
#include <string.h>

static ModuleDescriptor std_descriptors[] = {
    { "std.math",   MODULE_KIND_NATIVE, std_init_math,   NULL },
    { "std.time",   MODULE_KIND_NATIVE, std_init_time,   NULL },
    { "std.random", MODULE_KIND_NATIVE, std_init_random, NULL },
    { "std.watch",  MODULE_KIND_NATIVE, std_init_watch,  NULL },
    { "std.tty",    MODULE_KIND_NATIVE, std_init_tty,    NULL },
    { "std.fs",     MODULE_KIND_NATIVE, std_init_fs,     NULL },
    { "std.json",   MODULE_KIND_NATIVE, std_init_json,   NULL },
    { "std.path",   MODULE_KIND_NATIVE, std_init_path,   NULL },
    { "std.process",MODULE_KIND_NATIVE, std_init_process,NULL },
    { "std.string", MODULE_KIND_FILE,   NULL, "std/string.mks" },
    { "std.array",  MODULE_KIND_FILE,   NULL, "std/array.mks" },
    { "std",        MODULE_KIND_FILE,   NULL, "std/std.mks" },
    { NULL, MODULE_KIND_NATIVE, NULL, NULL }
};

void std_registry_init(void) {
    /* Mirror descriptors into dynamic registry to avoid lookup issues */
    module_register_native("std.math", std_init_math);
    module_register_native("std.time", std_init_time);
    module_register_native("std.random", std_init_random);
    module_register_native("std.watch", std_init_watch);
    module_register_native("std.fs", std_init_fs);
    module_register_native("std.tty", std_init_tty);
    module_register_native("std.json", std_init_json);
    module_register_native("std.path", std_init_path);
    module_register_native("std.process", std_init_process);
}

const ModuleDescriptor *std_registry_lookup(const char *module_id) {
    for (ModuleDescriptor *d = std_descriptors; d->id != NULL; d++) {
        if (strcmp(d->id, module_id) == 0) return d;
    }
    return NULL;
}
