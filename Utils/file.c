#include "file.h"
#include <stdio.h>
#include <stdlib.h>

char *mks_read_file(const char *filename) {
    FILE *file = fopen(filename, "rb");
    if (!file) {
        fprintf(stderr, "Error: Could not open file '%s'\n", filename);
        return NULL;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        fprintf(stderr, "Error: Could not seek file '%s'\n", filename);
        return NULL;
    }

    const long length = ftell(file);
    if (length < 0) {
        fclose(file);
        fprintf(stderr, "Error: Could not get size of file '%s'\n", filename);
        return NULL;
    }

    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        fprintf(stderr, "Error: Could not rewind file '%s'\n", filename);
        return NULL;
    }

    char *buffer = (char *)malloc((size_t)length + 1);
    if (!buffer) {
        fclose(file);
        fprintf(stderr, "Error: Out of memory while reading '%s'\n", filename);
        return NULL;
    }

    const size_t read_size = fread(buffer, 1, (size_t)length, file);
    if (ferror(file)) {
        fclose(file);
        free(buffer);
        fprintf(stderr, "Error: Failed to read file '%s'\n", filename);
        return NULL;
    }

    buffer[read_size] = '\0';
    fclose(file);
    return buffer;
}
