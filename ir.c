#include "ir.h"

IrFnTypeIndex irOpCallFnTypeIndex(const Ir *ir, const IrOp *op) {
    switch (op->kind) {
        case IR_OP_KIND_CALL_FN_IMPL:
            return ir->fn_impls->d[op->u.call.fn].type;
        case IR_OP_KIND_CALL_FN_IMPORT:
            return ir->fn_imports->d[op->u.call.fn].type;
        default:
            assert(0 && "unreachable");
    }
}
