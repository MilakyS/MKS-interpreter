#include "hash.h"

unsigned int get_hash(const char *s) {
    unsigned int hash = 5381;
    int c;

    while ((c = *s++)) {
        hash = ((hash << 5) + hash) + c;
    }

    return hash;
}