#include "interp.h"
#include <assert.h>

void xirInterpInit(XirInterp *interp, const XirIr *ir, XirInterpCallImportCallback call_import_callback, XirInterpReg *stack, size_t stack_capacity) {
    *interp = (XirInterp){0};
    XIR_DYNARR_EASY_CREATE(&interp->regs);
    XIR_DYNARR_RESULT_CHECK(rawrDynarrCreate(
        XIR_DYNARR_GP(&interp->data_ptrs),
        0,
        ir->datas->h.length,
        rawr_dynarr_default_allocator
    ));

    interp->stack = stack;
    interp->stack_capacity = stack_capacity;

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

static void duplicateRegs(XirInterp *interp, size_t regs_base, const XirIrRegIndexDynarr *indices) {
    for (size_t i = 0; i < indices->h.length; i++) {
        XIR_DYNARR_UNSAFE_PUSH(&interp->regs, xirInterpGetReg(interp, regs_base, indices->d[i]));
    }
}

void xirInterpEvalImpl(XirInterp *interp, XirIrFnImplIndex impl_index, size_t regs_base) {
    size_t stack_base = interp->stack_length;
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
            case XIR_IR_OP_KIND_SELECT:
                used_regs += op.u.select.true_regs->h.length;
                break;
            case XIR_IR_OP_KIND_ALLOCA:
                used_regs++;
                break;
            case XIR_IR_OP_KIND_STORE:
                break;
            case XIR_IR_OP_KIND_LOAD:
                used_regs++;
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
            case XIR_IR_OP_KIND_RETURN: {
                XirIrOpReturn _return = op.u._return;
                XirInterpReg *return_regs_buf = xirCheckedMalloc(_return.outputs->h.length * sizeof(return_regs_buf[0]));

                for (size_t i = 0; i < _return.outputs->h.length; i++) {
                    return_regs_buf[i] = xirInterpGetReg(interp, regs_base, _return.outputs->d[i]);
                }

                XIR_DYNARR_RESULT_CHECK(rawrDynarrResize(XIR_DYNARR_GP(&interp->regs), regs_base + _return.outputs->h.length));
                memcpy(interp->regs->d + regs_base, return_regs_buf, _return.outputs->h.length * sizeof(return_regs_buf[0]));
                free(return_regs_buf);
                goto cleanup;
            }
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
            case XIR_IR_OP_KIND_SELECT: {
                XirIrOpSelect select = op.u.select;
                if (xirInterpGetReg(interp, regs_base, select.cond)) {
                    duplicateRegs(interp, regs_base, select.true_regs);
                } else {
                    duplicateRegs(interp, regs_base, select.false_regs);
                }
                break;
            }
            case XIR_IR_OP_KIND_ALLOCA:
                if (interp->stack_length >= interp->stack_capacity) {
                    fprintf(stderr, "interpreter stack overflow\n");
                    abort();
                }
                XirInterpReg ptr = (XirInterpReg)(interp->stack + interp->stack_length++);
                XIR_DYNARR_UNSAFE_PUSH(&interp->regs, ptr);
                break;
            case XIR_IR_OP_KIND_STORE: {
                XirIrOpStore store = op.u.store;
                XirInterpReg *ptr = (XirInterpReg *)xirInterpGetReg(interp, regs_base, store.ptr_reg_index);
                *ptr = xirInterpGetReg(interp, regs_base, store.value_reg_index);
                break;
            }
            case XIR_IR_OP_KIND_LOAD: {
                XirInterpReg value = *(XirInterpReg *)xirInterpGetReg(interp, regs_base, op.u.load.ptr_reg_index);
                XIR_DYNARR_UNSAFE_PUSH(&interp->regs, value);
                break;
            }
        }
    }

    cleanup:
    rawrDynarrDestroy(XIR_DYNARR_GP(&ops_used_regs));
    interp->stack_length = stack_base;
}

XirInterpReg xirInterpGetReg(const XirInterp *interp, size_t regs_base, XirIrRegIndex index) {
    return interp->regs->d[regs_base + index];
}

void *xirInterpGetData(XirInterp *interp, size_t offset) {
    assert(offset <= interp->data_size); // <= because we can have data entries of size 0
    return interp->data + offset;
}
