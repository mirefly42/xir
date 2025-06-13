#ifndef XIR_INTERP_H
#define XIR_INTERP_H

#include "ir.h"
#include "utils.h"

typedef unsigned long long XirInterpReg;
XIR_DYNARR_EASY_GEN(XirInterpReg);

struct XirInterp;
typedef void (*XirInterpImportedFn)(struct XirInterp *interp, size_t regs_base);

XIR_DYNARR_EASY_GEN(XirInterpImportedFn);

typedef RAWR_DYNARR_GEN(size_t, XirSizeTDynarr) XirSizeTDynarr;

typedef struct XirInterp {
    XirInterpRegDynarr *regs;
    XirInterpImportedFnDynarr *imported_fns;
    XirSizeTDynarr *data_ptrs;
    const XirIr *ir;
    unsigned char *data;
    size_t data_size;
    void *user_data;
} XirInterp;

void xirInterpInit(XirInterp *interp, const XirIr *ir);
void xirInterpEvalImpl(XirInterp *interp, XirIrFnImplIndex impl_index, size_t regs_base);
XirInterpReg xirInterpGetReg(const XirInterp *interp, size_t regs_base, XirIrRegIndex index);
void *xirInterpGetData(XirInterp *interp, size_t offset);

#endif
