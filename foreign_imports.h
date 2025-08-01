#ifndef XIR_FOREIGN_IMPORTS_H
#define XIR_FOREIGN_IMPORTS_H

#include "bit_set.h"
#include "interp.h"
#include "utils.h"
#include <ffi.h>

typedef void (*XirFnPtr)(void);

XIR_DYNARR_EASY_GEN(XirFnPtr);

typedef RAWR_DYNARR_GEN(ffi_cif, XirFfiCifDynarr) XirFfiCifDynarr;

typedef struct XirForeignFnImportsCaller {
    XirFfiCifDynarr *cifs;
    XirFnPtrDynarr *fn_ptrs;
} XirForeignFnImportsCaller;

void xirForeignFnImportsCallerInit(XirForeignFnImportsCaller *caller);
void xirForeignFnImportsCallerCallImportCallback(XirInterp *interp, size_t regs_base, XirIrFnImportIndex import_index, void *user_data);
void xirPrepareFfiCifFromFnType(const XirIrFnType *fn_type, ffi_cif *cif);

void xirForeignFnImportsCallerLoadForeignFnImportFromDynamicLibrary(
    XirForeignFnImportsCaller *caller,
    const XirIr *ir,
    XirBitSet prepared_cifs,
    void **library_handles,
    size_t library_handles_length,
    XirIrFnImportIndex fn_import_index
);

void xirForeignFnImportsCallerLoadAllForeignFnImportsFromDynamicLibraries(
    XirForeignFnImportsCaller *caller,
    const XirIr *ir,
    void **library_handles,
    size_t library_handles_length
);

#endif
