#ifndef INTERP_H
#define INTERP_H

#include "ir.h"
#include "utils.h"

typedef unsigned long long InterpReg;
DYNARR_EASY_GEN(InterpReg);

struct Interp;
typedef void (*InterpImportedFn)(struct Interp *interp, size_t regs_base);

DYNARR_EASY_GEN(InterpImportedFn);

typedef RAWR_DYNARR_GEN(size_t, SizeTDynarr) SizeTDynarr;

typedef struct Interp {
    InterpRegDynarr *regs;
    InterpImportedFnDynarr *imported_fns;
    SizeTDynarr *data_ptrs;
    const Ir *ir;
    unsigned char *data;
    size_t data_size;
    void *user_data;
} Interp;

void interpInit(Interp *interp, const Ir *ir);
void interpEvalImpl(Interp *interp, IrFnImplIndex impl_index, size_t regs_base);
InterpReg interpGetReg(const Interp *interp, size_t regs_base, IrRegIndex index);
void *interpGetData(Interp *interp, size_t offset);

#endif
