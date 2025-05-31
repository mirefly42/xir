#include "utils.h"

void *checkedMalloc(size_t size) {
    void *ptr = malloc(size);
    if (!ptr) {
        FATAL_ERROR("allocation failed\n");
    }
    return ptr;
}
