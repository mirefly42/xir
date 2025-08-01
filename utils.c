#include "utils.h"

RawrDynarrResult xirDynarrEnsureLength(RawrDynarrGeneralPointer gp, size_t length) {
    if ((**gp.header_pp).length < length) {
        return rawrDynarrResize(gp, length);
    }
    return RAWR_DYNARR_RESULT_SUCCESS;
}

void *xirCheckedMalloc(size_t size) {
    void *ptr = malloc(size);
    if (!ptr) {
        XIR_FATAL_ERROR("allocation failed\n");
    }
    return ptr;
}
