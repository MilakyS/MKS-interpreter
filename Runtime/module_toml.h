#ifndef MKS_MODULE_TOML_H
#define MKS_MODULE_TOML_H

#include <stddef.h>

typedef struct {
    char name[256];
    char version[64];
    char entry[512];
    char main[512];
} PackageToml;

int package_toml_read(const char *root_path, PackageToml *out);
void package_toml_free(PackageToml *toml);

#endif
