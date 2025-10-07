#include "utils.h"
#include "validation.h"
#include <assert.h>
#include <stdbool.h>

#define VALIDATE(x) \
    do { \
        if (!(x)) { \
            XIR_FATAL_ERROR("validation failed: %s\n", #x); \
        } \
    } while (0)

static void validateIrType(XirIrType type) {
    VALIDATE(type >= 0 && type < XIR_IR_TYPES_COUNT);
}

static void validateIrTypes(const XirIrType *types, size_t length) {
    for (size_t i = 0; i < length; i++) {
        validateIrType(types[i]);
    }
}

static bool isPowerOfTwo(unsigned long long x) {
    return !(x == 0 || x & (x - 1));
}

void xirValidateIr(const XirIr *ir) {
    for (size_t i = 0; i < ir->fn_types->h.length; i++) {
        const XirIrFnType *fn_type = ir->fn_types->d + i;
        validateIrTypes(fn_type->input_types->d, fn_type->input_types->h.length);
        validateIrTypes(fn_type->output_types->d, fn_type->output_types->h.length);
    }

    for (size_t i = 0; i < ir->fn_imports->h.length; i++) {
        VALIDATE(ir->fn_imports->d[i].type < ir->fn_types->h.length);
    }

    for (size_t i = 0; i < ir->fn_exports->h.length; i++) {
        VALIDATE(ir->fn_exports->d[i].impl < ir->fn_impls->h.length);
        // TODO: validate that all exports have unique names
    }

    for (size_t i = 0; i < ir->datas->h.length; i++) {
        const XirIrData *data = ir->datas->d + i;
        VALIDATE(isPowerOfTwo(data->alignment));
    }

    XirIrTypeDynarr *reg_types;
    XIR_DYNARR_EASY_CREATE(&reg_types);
    for (size_t fn_impl_index = 0; fn_impl_index < ir->fn_impls->h.length; fn_impl_index++) {
        const XirIrFnImpl *fn_impl = ir->fn_impls->d + fn_impl_index;

        VALIDATE(fn_impl->type < ir->fn_types->h.length);
        const XirIrFnType *fn_type = ir->fn_types->d + fn_impl->type;

        XIR_DYNARR_RESULT_CHECK(rawrDynarrResize(XIR_DYNARR_GP(&reg_types), fn_type->input_types->h.length));
        memcpy(reg_types->d, fn_type->input_types->d, reg_types->h.length * sizeof(reg_types->d[0]));

        for (size_t op_index = 0; op_index < fn_impl->ops->h.length; op_index++) {
            const XirIrOp *op = fn_impl->ops->d + op_index;

            switch (op->kind) {
                case XIR_IR_OP_KIND_INT:
                    XIR_DYNARR_UNSAFE_PUSH(&reg_types, XIR_IR_TYPE_INT);
                    break;
                case XIR_IR_OP_KIND_CALL_FN_IMPL:
                case XIR_IR_OP_KIND_CALL_FN_IMPORT: {
                    XirIrOpCall call = op->u.call;

                    XirIrFnTypeIndex callee_fn_type_index;
                    switch (op->kind) {
                        case XIR_IR_OP_KIND_CALL_FN_IMPL:
                            VALIDATE(call.fn < ir->fn_impls->h.length);
                            callee_fn_type_index = ir->fn_impls->d[call.fn].type;
                            break;
                        case XIR_IR_OP_KIND_CALL_FN_IMPORT:
                            VALIDATE(call.fn < ir->fn_imports->h.length);
                            callee_fn_type_index = ir->fn_imports->d[call.fn].type;
                            break;
                        default:
                            assert(0 && "unreachable");
                            break;
                    }

                    VALIDATE(callee_fn_type_index < ir->fn_types->h.length);
                    const XirIrFnType *callee_type = ir->fn_types->d + callee_fn_type_index;

                    VALIDATE(call.inputs->h.length == callee_type->input_types->h.length);
                    for (size_t input_index = 0; input_index < call.inputs->h.length; input_index++) {
                        XirIrRegIndex input_reg = call.inputs->d[input_index];
                        VALIDATE(input_reg < reg_types->h.length);

                        XirIrType input_type = reg_types->d[input_reg];
                        XirIrType callee_input_type = callee_type->input_types->d[input_index];
                        VALIDATE(input_type == callee_input_type);
                    }

                    for (size_t i = 0; i < callee_type->output_types->h.length; i++) {
                        XIR_DYNARR_UNSAFE_PUSH(&reg_types, callee_type->output_types->d[i]);
                    }
                    break;
                }
                case XIR_IR_OP_KIND_RETURN: {
                    XirIrOpReturn _return = op->u._return;
                    VALIDATE(fn_type->output_types->h.length == _return.outputs->h.length);

                    for (size_t i = 0; i < _return.outputs->h.length; i++) {
                        XirIrRegIndex return_reg_index = _return.outputs->d[i];
                        VALIDATE(return_reg_index < reg_types->h.length);
                        VALIDATE(fn_type->output_types->d[i] == reg_types->d[return_reg_index]);
                    }
                    break;
                }
                case XIR_IR_OP_KIND_DATA_PTR:
                    VALIDATE(op->u.data_ptr < ir->datas->h.length);
                    XIR_DYNARR_UNSAFE_PUSH(&reg_types, XIR_IR_TYPE_INT);
                    break;
                case XIR_IR_OP_KIND_BRANCH: {
                    XirIrOpBranch branch = op->u.branch;
                    VALIDATE(branch.cond < reg_types->h.length);
                    VALIDATE(branch.op <= fn_impl->ops->h.length); // <= because you can jump to the end of the function
                    break;
                }
                case XIR_IR_OP_KIND_SELECT: {
                    XirIrOpSelect select = op->u.select;
                    VALIDATE(select.cond < reg_types->h.length);
                    VALIDATE(select.true_regs->h.length == select.false_regs->h.length);
                    for (size_t i = 0; i < select.true_regs->h.length; i++) {
                        XirIrRegIndex true_reg = select.true_regs->d[i];
                        XirIrRegIndex false_reg = select.false_regs->d[i];
                        VALIDATE(true_reg < reg_types->h.length);
                        VALIDATE(false_reg < reg_types->h.length);
                        VALIDATE(reg_types->d[true_reg] == reg_types->d[false_reg]);
                    }

                    for (size_t i = 0; i < select.true_regs->h.length; i++) {
                        XIR_DYNARR_UNSAFE_PUSH(&reg_types, reg_types->d[select.true_regs->d[i]]);
                    }
                }
            }
        }
    }
    rawrDynarrDestroy(XIR_DYNARR_GP(&reg_types));
}
