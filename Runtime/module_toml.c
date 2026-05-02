#include "module_toml.h"
#include "../Utils/file.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

static int path_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

static int read_quoted_field(const char *line, const char *key, char *out, size_t out_size) {
    size_t key_len = strlen(key);
    if (strncmp(line, key, key_len) != 0) return 0;

    const char *p = line + key_len;
    while (*p == ' ' || *p == '\t') p++;

    if (*p != '=') return 0;
    p++;

    while (*p == ' ' || *p == '\t') p++;

    if (*p != '"') return 0;
    p++;

    const char *end = strchr(p, '"');
    if (end == NULL) return 0;

    size_t len = end - p;
    if (len >= out_size) return 0;

    memcpy(out, p, len);
    out[len] = '\0';
    return 1;
}

static int parse_toml_section(char *source, PackageToml *out) {
    memset(out, 0, sizeof(PackageToml));

    char *cursor = source;
    int in_package_section = 0;

    while (*cursor != '\0') {
        char *line_end = strchr(cursor, '\n');
        if (line_end != NULL) {
            *line_end = '\0';
        }

        while (*cursor == ' ' || *cursor == '\t') cursor++;

        // Skip empty lines and comments
        if (*cursor == '\0' || *cursor == '#') {
            if (line_end != NULL) {
                *line_end = '\n';
                cursor = line_end + 1;
            } else {
                break;
            }
            continue;
        }

        // Check for [package] section
        if (strncmp(cursor, "[package]", 9) == 0) {
            in_package_section = 1;
            if (line_end != NULL) {
                *line_end = '\n';
                cursor = line_end + 1;
            } else {
                break;
            }
            continue;
        }

        // Check for other sections (exit [package])
        if (*cursor == '[') {
            in_package_section = 0;
            if (line_end != NULL) {
                *line_end = '\n';
                cursor = line_end + 1;
            } else {
                break;
            }
            continue;
        }

        // Parse fields in [package] section
        if (in_package_section) {
            read_quoted_field(cursor, "name", out->name, sizeof(out->name));
            read_quoted_field(cursor, "package", out->name, sizeof(out->name));
            read_quoted_field(cursor, "version", out->version, sizeof(out->version));
            read_quoted_field(cursor, "entry", out->entry, sizeof(out->entry));
            read_quoted_field(cursor, "main", out->main, sizeof(out->main));
        }

        if (line_end != NULL) {
            *line_end = '\n';
            cursor = line_end + 1;
        } else {
            break;
        }
    }

    return out->name[0] != '\0';
}

int package_toml_read(const char *root_path, PackageToml *out) {
    char path[512];
    snprintf(path, sizeof(path), "%s/mks.toml", root_path);

    if (!path_exists(path)) {
        return 0;
    }

    char *source = mks_read_file(path);
    if (source == NULL) {
        return 0;
    }

    int result = parse_toml_section(source, out);
    free(source);
    return result;
}

void package_toml_free(PackageToml *toml) {
    (void)toml;
}
