#include "ir.h"

void xirIrInit(XirIr *ir) {
    XIR_DYNARR_EASY_CREATE(&ir->fn_types);
    XIR_DYNARR_EASY_CREATE(&ir->fn_impls);
    XIR_DYNARR_EASY_CREATE(&ir->fn_imports);
    XIR_DYNARR_EASY_CREATE(&ir->fn_exports);
    XIR_DYNARR_EASY_CREATE(&ir->datas);
}

XirIrFnTypeIndex xirIrOpCallFnTypeIndex(const XirIr *ir, const XirIrOp *op) {
    switch (op->kind) {
        case XIR_IR_OP_KIND_CALL_FN_IMPL:
            return ir->fn_impls->d[op->u.call.fn].type;
        case XIR_IR_OP_KIND_CALL_FN_IMPORT:
            return ir->fn_imports->d[op->u.call.fn].type;
        default:
            assert(0 && "unreachable");
    }
}
