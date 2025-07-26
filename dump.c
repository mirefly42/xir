#include "dump.h"
#include <assert.h>
#include <stdio.h>

static void xirDumpTypes(const XirIrType *types, size_t length) {
    printf("(");
    for (size_t i = 0; i < length; i++) {
        if (i > 0) {
            printf(" ");
        }

        switch (types[i]) {
            case XIR_IR_TYPE_INT:
                printf("int");
                break;
            case XIR_IR_TYPE_FLOAT:
                printf("float");
                break;
            default:
                assert(0 && "unreachable");
        }
    }
    printf(")");
}

static void xirDumpStringView(LisStringView sv) {
    fwrite(sv.data, 1, sv.length, stdout);
}

static void xirDumpRegsRange(XirIrRegIndex start, size_t length) {
    printf("(#");
    for (XirIrRegIndex reg = start; reg < start + length; reg++) {
        printf(" %zu", reg);
    }
    printf(" =)");
}

void xirDumpIr(const XirIr *ir) {
    for (size_t i = 0; i < ir->fn_types->h.length; i++) {
        const XirIrFnType *fn_type = ir->fn_types->d + i;
        printf("(# %zu) (fn-type ", i);
        xirDumpTypes(fn_type->input_types->d, fn_type->input_types->h.length);
        printf(" ");
        xirDumpTypes(fn_type->output_types->d, fn_type->output_types->h.length);
        printf(")\n");
    }
    printf("\n");

    for (size_t i = 0; i < ir->fn_imports->h.length; i++) {
        const XirIrFnImport *fn_import = ir->fn_imports->d + i;
        printf("(# %zu) (fn-import %zu ", i, fn_import->type);
        xirDumpStringView(fn_import->name);
        printf(")\n");
    }
    printf("\n");

    for (size_t i = 0; i < ir->fn_exports->h.length; i++) {
        const XirIrFnExport *fn_export = ir->fn_exports->d + i;
        printf("(# %zu) (fn-export %zu ", i, fn_export->impl);
        xirDumpStringView(fn_export->name);
        printf(")\n");
    }
    printf("\n");

    for (size_t i = 0; i < ir->datas->h.length; i++) {
        const XirIrData *data = ir->datas->d + i;
        printf("(# %zu) (data (", i);
        if (data->flags & XIR_IR_DATA_FLAGS_READ_ONLY) {
            printf("read-only");
        }
        printf(") %u", data->alignment);
        for (size_t i = 0; i < data->bytes->h.length; i++) {
            printf(" %u", data->bytes->d[i]);
        }
        printf(")\n");
    }
    printf("\n");

    for (size_t i = 0; i < ir->fn_impls->h.length; i++) {
        const XirIrFnImpl *fn_impl = ir->fn_impls->d + i;
        printf("(# %zu) (fn-impl %zu", i, fn_impl->type);
        size_t used_regs = ir->fn_types->d[fn_impl->type].input_types->h.length;
        for (size_t i = 0; i < fn_impl->ops->h.length; i++) {
            const XirIrOp *op = fn_impl->ops->d + i;
            printf("\n    (# %zu) (", i);
            switch (op->kind) {
                case XIR_IR_OP_KIND_INT:
                    xirDumpRegsRange(used_regs++, 1);
                    printf(" int %llu", op->u._int);
                    break;
                case XIR_IR_OP_KIND_CALL_FN_IMPL:
                case XIR_IR_OP_KIND_CALL_FN_IMPORT: {
                    const XirIrOpCall *call = &op->u.call;
                    const XirIrFnType *fn_type = ir->fn_types->d + xirIrOpCallFnTypeIndex(ir, op);
                    xirDumpRegsRange(used_regs, fn_type->output_types->h.length);
                    used_regs += fn_type->output_types->h.length;

                    switch (op->kind) {
                        case XIR_IR_OP_KIND_CALL_FN_IMPL:
                            printf(" call-impl");
                            break;
                        case XIR_IR_OP_KIND_CALL_FN_IMPORT:
                            printf(" call-import");
                            break;
                        default:
                            assert(0 && "unreachable");
                    }

                    printf(" %zu", call->fn);
                    for (size_t i = 0; i < call->inputs->h.length; i++) {
                        printf(" %zu", call->inputs->d[i]);
                    }
                    break;
                }
                case XIR_IR_OP_KIND_RETURN:
                    printf("return %zu", op->u._return);
                    break;
                case XIR_IR_OP_KIND_DATA_PTR:
                    xirDumpRegsRange(used_regs++, 1);
                    printf(" data-ptr %zu", op->u.data_ptr);
                    break;
                case XIR_IR_OP_KIND_BRANCH:
                    printf("branch %zu %zu", op->u.branch.cond, op->u.branch.op);
                    break;
            }
            printf(")");
        }
        printf(")\n");
    }
}
