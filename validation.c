#include "utils.h"
#include "validation.h"
#include <assert.h>
#include <stdbool.h>

#define VALIDATE(x) \
    do { \
        if (!(x)) { \
            FATAL_ERROR("validation failed: %s\n", #x); \
        } \
    } while (0)

static void validateIrType(IrType type) {
    VALIDATE(type >= 0 && type < IR_TYPES_COUNT);
}

static void validateIrTypes(const IrType *types, size_t length) {
    for (size_t i = 0; i < length; i++) {
        validateIrType(types[i]);
    }
}

static bool isPowerOfTwo(unsigned long long x) {
    return !(x == 0 || x & (x - 1));
}

void validateIr(const Ir *ir) {
    for (size_t i = 0; i < ir->fn_types->h.length; i++) {
        const IrFnType *fn_type = ir->fn_types->d + i;
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
        const IrData *data = ir->datas->d + i;
        VALIDATE(isPowerOfTwo(data->alignment));
    }

    for (size_t fn_impl_index = 0; fn_impl_index < ir->fn_impls->h.length; fn_impl_index++) {
        IrTypeDynarr *reg_types;
        DYNARR_EASY_CREATE(&reg_types);

        const IrFnImpl *fn_impl = ir->fn_impls->d + fn_impl_index;

        VALIDATE(fn_impl->type < ir->fn_types->h.length);
        const IrFnType *fn_type = ir->fn_types->d + fn_impl->type;

        for (size_t op_index = 0; op_index < fn_impl->ops->h.length; op_index++) {
            const IrOp *op = fn_impl->ops->d + op_index;

            switch (op->kind) {
                case IR_OP_KIND_INT:
                    DYNARR_UNSAFE_PUSH(&reg_types, IR_TYPE_INT);
                    break;
                case IR_OP_KIND_CALL_FN_IMPL:
                case IR_OP_KIND_CALL_FN_IMPORT: {
                    IrOpCall call = op->u.call;

                    IrFnTypeIndex callee_fn_type_index;
                    switch (op->kind) {
                        case IR_OP_KIND_CALL_FN_IMPL:
                            VALIDATE(call.fn < ir->fn_impls->h.length);
                            callee_fn_type_index = ir->fn_impls->d[call.fn].type;
                            break;
                        case IR_OP_KIND_CALL_FN_IMPORT:
                            VALIDATE(call.fn < ir->fn_imports->h.length);
                            callee_fn_type_index = ir->fn_imports->d[call.fn].type;
                            break;
                        default:
                            assert(0 && "unreachable");
                            break;
                    }

                    VALIDATE(callee_fn_type_index < ir->fn_types->h.length);
                    const IrFnType *callee_type = ir->fn_types->d + callee_fn_type_index;

                    VALIDATE(call.inputs->h.length == callee_type->input_types->h.length);
                    for (size_t input_index = 0; input_index < call.inputs->h.length; input_index++) {
                        IrRegIndex input_reg = call.inputs->d[input_index];
                        VALIDATE(input_reg < reg_types->h.length);

                        IrType input_type = reg_types->d[input_reg];
                        IrType callee_input_type = callee_type->input_types->d[input_index];
                        VALIDATE(input_type == callee_input_type);
                    }

                    for (size_t i = 0; i < callee_type->output_types->h.length; i++) {
                        DYNARR_UNSAFE_PUSH(&reg_types, callee_type->output_types->d[i]);
                    }
                    break;
                }
                case IR_OP_KIND_RETURN: {
                    IrRegIndex return_reg_index = op->u._return;
                    VALIDATE(return_reg_index < reg_types->h.length);
                    IrType return_op_type = reg_types->d[return_reg_index];

                    VALIDATE(fn_type->output_types->h.length == 1);
                    VALIDATE(fn_type->output_types->d[0] == return_op_type);
                    DYNARR_UNSAFE_PUSH(&reg_types, return_op_type);
                    break;
                }
                case IR_OP_KIND_DATA_PTR:
                    VALIDATE(op->u.data_ptr < ir->datas->h.length);
                    DYNARR_UNSAFE_PUSH(&reg_types, IR_TYPE_INT);
                    break;
                case IR_OP_KIND_BRANCH: {
                    IrOpBranch branch = op->u.branch;
                    VALIDATE(branch.cond < reg_types->h.length);
                    VALIDATE(branch.op <= fn_impl->ops->h.length); // <= because you can jump to the end of the function
                    break;
                }
            }
        }
    }
}
