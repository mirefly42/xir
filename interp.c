#include "interp.h"
#include <assert.h>

void xirInterpInit(XirInterp *interp, const XirIr *ir, XirInterpCallImportCallback call_import_callback) {
    *interp = (XirInterp){0};
    XIR_DYNARR_EASY_CREATE(&interp->regs);
    XIR_DYNARR_RESULT_CHECK(rawrDynarrCreate(
        XIR_DYNARR_GP(&interp->data_ptrs),
        0,
        ir->datas->h.length,
        rawr_dynarr_default_allocator
    ));

    interp->ir = ir;

    for (size_t i = 0; i < ir->datas->h.length; i++) {
        const XirIrData *data = ir->datas->d + i;
        size_t padding = (data->alignment - interp->data_size % data->alignment) % data->alignment;
        interp->data_size += padding;
        XIR_DYNARR_UNSAFE_PUSH(&interp->data_ptrs, interp->data_size);
        interp->data_size += data->bytes->h.length;
    }

    interp->data = xirCheckedMalloc(interp->data_size);

    for (size_t i = 0; i < ir->datas->h.length; i++) {
        const XirIrData *data = ir->datas->d + i;
        memcpy(interp->data + interp->data_ptrs->d[i], data->bytes->d, data->bytes->h.length);
    }

    // TODO: would be cool to set memory protection flags on read-only data
    //       entries, to make the program safely crash in case an imported
    //       function tries to modify read-only data.

    interp->call_import_callback = call_import_callback;
}

void xirInterpEvalImpl(XirInterp *interp, XirIrFnImplIndex impl_index, size_t regs_base) {
    const XirIrFnImpl *impl = interp->ir->fn_impls->d + impl_index;

    // ops_used_regs->d[i] contains the amount of used registers before execution of operation i
    // relative to regs_base.
    XirSizeTDynarr *ops_used_regs;
    XIR_DYNARR_RESULT_CHECK(rawrDynarrCreate(
        XIR_DYNARR_GP(&ops_used_regs),
        0,
        impl->ops->h.length,
        rawr_dynarr_default_allocator
    ));

    size_t used_regs = interp->ir->fn_types->d[impl->type].input_types->h.length;
    for (size_t i = 0; i < impl->ops->h.length; i++) {
        XIR_DYNARR_UNSAFE_PUSH(&ops_used_regs, used_regs);

        XirIrOp op = impl->ops->d[i];
        switch (op.kind) {
            case XIR_IR_OP_KIND_INT:
            case XIR_IR_OP_KIND_DATA_PTR:
                used_regs++;
                break;
            case XIR_IR_OP_KIND_CALL_FN_IMPL:
            case XIR_IR_OP_KIND_CALL_FN_IMPORT: {
                XirIrFnTypeIndex fn_type_index = xirIrOpCallFnTypeIndex(interp->ir, &op);
                const XirIrFnType *fn_type = interp->ir->fn_types->d + fn_type_index;
                used_regs += fn_type->output_types->h.length;
                break;
            }
            case XIR_IR_OP_KIND_RETURN:
            case XIR_IR_OP_KIND_BRANCH:
                break;
        }
    }
    XIR_DYNARR_UNSAFE_PUSH(&ops_used_regs, used_regs);

    for (size_t i = 0; i < impl->ops->h.length; i++) {
        XirIrOp op = impl->ops->d[i];
        switch (op.kind) {
            case XIR_IR_OP_KIND_INT:
                XIR_DYNARR_UNSAFE_PUSH(&interp->regs, op.u._int);
                break;
            case XIR_IR_OP_KIND_CALL_FN_IMPL:
            case XIR_IR_OP_KIND_CALL_FN_IMPORT: {
                XirIrOpCall call = op.u.call;

                size_t callee_regs_base = interp->regs->h.length;
                for (size_t i = 0; i < call.inputs->h.length; i++) {
                    XIR_DYNARR_UNSAFE_PUSH(&interp->regs, xirInterpGetReg(interp, regs_base, call.inputs->d[i]));
                }

                switch (op.kind) {
                    case XIR_IR_OP_KIND_CALL_FN_IMPL:
                        xirInterpEvalImpl(interp, call.fn, callee_regs_base);
                        break;
                    case XIR_IR_OP_KIND_CALL_FN_IMPORT:
                        interp->call_import_callback(interp, callee_regs_base, call.fn, interp->user_data);
                        break;
                    default:
                        assert(0 && "unreachable");
                }
                break;
            }
            case XIR_IR_OP_KIND_RETURN:
                interp->regs->h.length = regs_base;
                XIR_DYNARR_UNSAFE_PUSH(&interp->regs, xirInterpGetReg(interp, regs_base, op.u._return));
                return;
            case XIR_IR_OP_KIND_DATA_PTR:
                XIR_DYNARR_UNSAFE_PUSH(&interp->regs, (XirInterpReg)(interp->data + interp->data_ptrs->d[op.u.data_ptr]));
                break;
            case XIR_IR_OP_KIND_BRANCH: {
                XirIrOpBranch branch = op.u.branch;
                if (xirInterpGetReg(interp, regs_base, branch.cond)) {
                    XIR_DYNARR_RESULT_CHECK(rawrDynarrResize(XIR_DYNARR_GP(&interp->regs), regs_base + ops_used_regs->d[branch.op]));
                    i = branch.op - 1; // HACK: subtract one to cancel out i++
                }
                break;
            }
        }
    }
}

XirInterpReg xirInterpGetReg(const XirInterp *interp, size_t regs_base, XirIrRegIndex index) {
    return interp->regs->d[regs_base + index];
}

void *xirInterpGetData(XirInterp *interp, size_t offset) {
    assert(offset <= interp->data_size); // <= because we can have data entries of size 0
    return interp->data + offset;
}
