#include "utils.h"

void *xirCheckedMalloc(size_t size) {
    void *ptr = malloc(size);
    if (!ptr) {
        XIR_FATAL_ERROR("allocation failed\n");
    }
    return ptr;
}
