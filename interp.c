#include "interp.h"
#include <assert.h>

void interpInit(Interp *interp, const Ir *ir) {
    *interp = (Interp){0};
    DYNARR_EASY_CREATE(&interp->regs);
    DYNARR_EASY_CREATE(&interp->imported_fns);
    DYNARR_RESULT_CHECK(rawrDynarrCreate(
        DYNARR_GP(&interp->data_ptrs),
        0,
        ir->datas->h.length,
        rawr_dynarr_default_allocator
    ));

    interp->ir = ir;

    for (size_t i = 0; i < ir->datas->h.length; i++) {
        const IrData *data = ir->datas->d + i;
        size_t padding = (data->alignment - interp->data_size % data->alignment) % data->alignment;
        interp->data_size += padding;
        DYNARR_UNSAFE_PUSH(&interp->data_ptrs, interp->data_size);
        interp->data_size += data->bytes->h.length;
    }

    interp->data = checkedMalloc(interp->data_size);

    for (size_t i = 0; i < ir->datas->h.length; i++) {
        const IrData *data = ir->datas->d + i;
        memcpy(interp->data + interp->data_ptrs->d[i], data->bytes->d, data->bytes->h.length);
    }

    // TODO: would be cool to set memory protection flags on read-only data
    //       entries, to make the program safely crash in case an imported
    //       function tries to modify read-only data.
}

void interpEvalImpl(Interp *interp, IrFnImplIndex impl_index, size_t regs_base) {
    const IrFnImpl *impl = interp->ir->fn_impls->d + impl_index;

    // ops_used_regs->d[i] contains the amount of used registers before execution of operation i
    SizeTDynarr *ops_used_regs;
    DYNARR_RESULT_CHECK(rawrDynarrCreate(
        DYNARR_GP(&ops_used_regs),
        0,
        impl->ops->h.length,
        rawr_dynarr_default_allocator
    ));

    size_t used_regs = interp->ir->fn_types->d[impl->type].input_types->h.length;
    for (size_t i = 0; i < impl->ops->h.length; i++) {
        DYNARR_UNSAFE_PUSH(&ops_used_regs, used_regs);

        IrOp op = impl->ops->d[i];
        switch (op.kind) {
            case IR_OP_KIND_INT:
            case IR_OP_KIND_DATA_PTR:
                used_regs++;
                break;
            case IR_OP_KIND_CALL_FN_IMPL:
            case IR_OP_KIND_CALL_FN_IMPORT: {
                IrFnTypeIndex fn_type_index = irOpCallFnTypeIndex(interp->ir, &op);
                const IrFnType *fn_type = interp->ir->fn_types->d + fn_type_index;
                used_regs += fn_type->output_types->h.length;
                break;
            }
            case IR_OP_KIND_RETURN:
            case IR_OP_KIND_GOTO:
            case IR_OP_KIND_BRANCH:
                break;
        }
    }
    DYNARR_UNSAFE_PUSH(&ops_used_regs, used_regs);

    for (size_t i = 0; i < impl->ops->h.length; i++) {
        IrOp op = impl->ops->d[i];
        switch (op.kind) {
            case IR_OP_KIND_INT:
                DYNARR_UNSAFE_PUSH(&interp->regs, op.u._int);
                break;
            case IR_OP_KIND_CALL_FN_IMPL:
            case IR_OP_KIND_CALL_FN_IMPORT: {
                IrOpCall call = op.u.call;

                size_t callee_regs_base = interp->regs->h.length;
                for (size_t i = 0; i < call.inputs->h.length; i++) {
                    DYNARR_UNSAFE_PUSH(&interp->regs, interpGetReg(interp, regs_base, call.inputs->d[i]));
                }

                switch (op.kind) {
                    case IR_OP_KIND_CALL_FN_IMPL:
                        interpEvalImpl(interp, call.fn, callee_regs_base);
                        break;
                    case IR_OP_KIND_CALL_FN_IMPORT:
                        interp->imported_fns->d[call.fn](interp, callee_regs_base);
                        break;
                    default:
                        assert(0 && "unreachable");
                }
                break;
            }
            case IR_OP_KIND_RETURN:
                interp->regs->h.length = regs_base;
                DYNARR_UNSAFE_PUSH(&interp->regs, interpGetReg(interp, regs_base, op.u._return));
                return;
            case IR_OP_KIND_DATA_PTR:
                DYNARR_UNSAFE_PUSH(&interp->regs, interp->data_ptrs->d[op.u.data_ptr]);
                break;
            case IR_OP_KIND_GOTO:
                interp->regs->h.length = ops_used_regs->d[op.u._goto];
                i = op.u._goto - 1; // HACK: subtract one to cancel out i++
                break;
            case IR_OP_KIND_BRANCH: {
                IrOpBranch branch = op.u.branch;
                if (interpGetReg(interp, regs_base, branch.cond)) {
                    interp->regs->h.length = ops_used_regs->d[branch.op];
                    i = branch.op - 1; // HACK: subtract one to cancel out i++
                }
                break;
            }
        }
    }
}

InterpReg interpGetReg(const Interp *interp, size_t regs_base, IrRegIndex index) {
    return interp->regs->d[regs_base + index];
}

void *interpGetData(Interp *interp, size_t offset) {
    assert(offset <= interp->data_size); // <= because we can have data entries of size 0
    return interp->data + offset;
}
