#include "nasm.h"

#define STACK_ALIGNMENT 16

static const char *input_int_regs[] = {
    "rdi",
    "rsi",
    "rdx",
    "rcx",
    "r8",
    "r9",
};

static const char *output_int_regs[] = {
    "rax",
    "rdx",
};

typedef enum {
    REG_KIND_IMM_INT,
    REG_KIND_STACK,
} RegKind;

typedef struct RegStack {
    unsigned long long offset;
} RegStack;

typedef struct Reg {
    RegKind kind;
    union {
        unsigned long long imm_int;
        RegStack stack;
    } u;
} Reg;

XIR_DYNARR_EASY_GEN(Reg);

static unsigned long long xirIrTypeSize(XirIrType type) {
    switch (type) {
        case XIR_IR_TYPE_INT: return 8;
        case XIR_IR_TYPE_FLOAT: return 16;
    }
}

void emitReg(const RegDynarr *regs, XirIrRegIndex index) {
    assert(index <= regs->h.length);
    Reg reg = regs->d[index];
    switch (reg.kind) {
        case REG_KIND_IMM_INT:
            printf("%llu", reg.u.imm_int);
            break;
        case REG_KIND_STACK:
            if (reg.u.stack.offset >> 63) {
                printf("[rbp+%llu]", - reg.u.stack.offset);
            } else {
                printf("[rbp-%llu]", reg.u.stack.offset);
            }
            break;
    }
}

typedef struct XirFnTypeAnalysis {
    size_t inputs_count_table[XIR_IR_TYPES_COUNT];
    size_t outputs_count_table[XIR_IR_TYPES_COUNT];
} XirFnTypeAnalysis;

static XirFnTypeAnalysis xirAnalyzeFnType(XirIrFnType fn_type) {
    XirFnTypeAnalysis result = {0};

    for (size_t i = 0; i < fn_type.input_types->h.length; i++) {
        result.inputs_count_table[fn_type.input_types->d[i]]++;
    }

    for (size_t i = 0; i < fn_type.output_types->h.length; i++) {
        result.outputs_count_table[fn_type.output_types->d[i]]++;
    }

    return result;
}

static void duplicateReg(const RegDynarr *regs, size_t reg_index, size_t *used_ir_regs) {
        printf("    mov rax,");
        emitReg(regs, reg_index);
        printf("\n");
        printf("    mov ");
        emitReg(regs, (*used_ir_regs)++);
        printf(",rax\n");
}

static void duplicateRegs(const RegDynarr *regs, const XirIrRegIndexDynarr *indices, size_t *used_ir_regs) {
    for (size_t i = 0; i < indices->h.length; i++) {
        duplicateReg(regs, indices->d[i], used_ir_regs);
    }
}

void xirGenerateNasm(const XirIr *ir) {
    printf("default rel\n");

    RegDynarr *regs;
    XIR_DYNARR_EASY_CREATE(&regs);

    for (size_t i = 0; i < ir->fn_imports->h.length; i++) {
        XirIrFnImport import = ir->fn_imports->d[i];
        printf("extern %.*s\n", (int)import.name.length, import.name.data);
    }

    for (size_t i = 0; i < ir->fn_exports->h.length; i++) {
        XirIrFnExport export = ir->fn_exports->d[i];
        printf("global %.*s\n", (int)export.name.length, export.name.data);
    }

    printf("section .text\n");

    for (size_t i = 0; i < ir->fn_impls->h.length; i++) {
        regs->h.length = 0;

        XirIrFnImpl impl = ir->fn_impls->d[i];
        printf("impl_%zu:\n", i);
        for (size_t export_i = 0; export_i < ir->fn_exports->h.length; export_i++) {
            XirIrFnExport export = ir->fn_exports->d[export_i];
            if (export.impl == i) {
                printf("%.*s:\n", (int)export.name.length, export.name.data);
            }
        }

        printf("    push rbp\n");
        printf("    mov rbp,rsp\n");

        unsigned long long stack_top = 0;
        XirFnTypeAnalysis impl_fn_type_analysis = xirAnalyzeFnType(ir->fn_types->d[impl.type]);
        if (impl_fn_type_analysis.inputs_count_table[XIR_IR_TYPE_FLOAT] > 0 || impl_fn_type_analysis.outputs_count_table[XIR_IR_TYPE_FLOAT] > 0) {
            XIR_FATAL_ERROR("floats aren't implemented\n");
        }

        bool impl_return_on_memory = impl_fn_type_analysis.outputs_count_table[XIR_IR_TYPE_INT] > XIR_STATIC_ARRAY_LENGTH(output_int_regs);
        for (size_t i = 0; i < impl_fn_type_analysis.inputs_count_table[XIR_IR_TYPE_INT]; i++) {
            size_t input_int_reg_i = impl_return_on_memory + i;
            if (input_int_reg_i < XIR_STATIC_ARRAY_LENGTH(input_int_regs)) {
                printf("    push %s\n", input_int_regs[input_int_reg_i]);
                stack_top += xirIrTypeSize(XIR_IR_TYPE_INT);
                XIR_DYNARR_UNSAFE_PUSH(&regs, ((Reg){REG_KIND_STACK, {.stack = {stack_top}}}));
            } else {
                XIR_DYNARR_UNSAFE_PUSH(&regs, ((Reg){REG_KIND_STACK, {.stack = {
                    - (2 + input_int_reg_i - XIR_STATIC_ARRAY_LENGTH(input_int_regs)) * xirIrTypeSize(XIR_IR_TYPE_INT)
                }}}));
            }
        }

        unsigned long long stack_pre_alloc_base = stack_top;
        for (size_t i = 0; i < impl.ops->h.length; i++) {
            XirIrOp op = impl.ops->d[i];
            switch (op.kind) {
                case XIR_IR_OP_KIND_INT:
                    XIR_DYNARR_UNSAFE_PUSH(&regs, ((Reg){REG_KIND_IMM_INT, {.imm_int = op.u._int}}));
                    break;
                case XIR_IR_OP_KIND_CALL_FN_IMPL:
                case XIR_IR_OP_KIND_CALL_FN_IMPORT: {
                    XirIrFnType callee_fn_type = ir->fn_types->d[xirIrOpCallFnTypeIndex(ir, &op)];
                    XirFnTypeAnalysis callee_fn_type_analysis = xirAnalyzeFnType(callee_fn_type);
                    if (callee_fn_type_analysis.inputs_count_table[XIR_IR_TYPE_FLOAT] > 0 || callee_fn_type_analysis.outputs_count_table[XIR_IR_TYPE_FLOAT] > 0) {
                        XIR_FATAL_ERROR("floats aren't implemented\n");
                    }

                    stack_top += callee_fn_type_analysis.outputs_count_table[XIR_IR_TYPE_INT] * xirIrTypeSize(XIR_IR_TYPE_INT);
                    for (size_t i = 0; i < callee_fn_type_analysis.outputs_count_table[XIR_IR_TYPE_INT]; i++) {
                        XIR_DYNARR_UNSAFE_PUSH(&regs, ((Reg){REG_KIND_STACK, {.stack = {stack_top - i * xirIrTypeSize(XIR_IR_TYPE_INT)}}}));
                    }
                    break;
                }
                case XIR_IR_OP_KIND_RETURN:
                    break;
                case XIR_IR_OP_KIND_DATA_PTR:
                    stack_top += xirIrTypeSize(XIR_IR_TYPE_INT);
                    XIR_DYNARR_UNSAFE_PUSH(&regs, ((Reg){REG_KIND_STACK, {.stack = {stack_top}}}));
                    break;
                case XIR_IR_OP_KIND_BRANCH:
                    break;
                case XIR_IR_OP_KIND_SELECT: {
                    XirIrOpSelect select = op.u.select;
                    for (size_t i = 0; i < select.true_regs->h.length; i++) {
                        stack_top += xirIrTypeSize(XIR_IR_TYPE_INT);
                        XIR_DYNARR_UNSAFE_PUSH(&regs, ((Reg){REG_KIND_STACK, {.stack = {stack_top}}}));
                    }
                    break;
                }
            }
        }

        unsigned long long stack_padding = (STACK_ALIGNMENT - (stack_top % STACK_ALIGNMENT)) % STACK_ALIGNMENT;
        stack_top += stack_padding;

        printf("    sub rsp,%llu\n", stack_top - stack_pre_alloc_base);
        size_t used_ir_regs = impl_fn_type_analysis.inputs_count_table[XIR_IR_TYPE_INT];

        for (size_t i = 0; i < impl.ops->h.length; i++) {
            XirIrOp op = impl.ops->d[i];
            printf("    .op_%zu:\n", i);
            switch (op.kind) {
                case XIR_IR_OP_KIND_INT:
                    used_ir_regs++;
                    break;
                case XIR_IR_OP_KIND_CALL_FN_IMPL:
                case XIR_IR_OP_KIND_CALL_FN_IMPORT: {
                    if (impl_return_on_memory) {
                        printf("    push rdi\n");
                        stack_top += 8;
                    }

                    XirIrOpCall call = op.u.call;
                    XirIrFnTypeIndex callee_fn_type_index = xirIrOpCallFnTypeIndex(ir, &op);
                    XirIrFnType callee_fn_type = ir->fn_types->d[callee_fn_type_index];
                    XirFnTypeAnalysis callee_fn_type_analysis = xirAnalyzeFnType(callee_fn_type);
                    bool callee_return_on_memory = callee_fn_type_analysis.outputs_count_table[XIR_IR_TYPE_INT] > XIR_STATIC_ARRAY_LENGTH(output_int_regs);

                    unsigned long long stack_pre_alloc_base = stack_top;
                    if (callee_return_on_memory) {
                        Reg reg = regs->d[used_ir_regs];
                        assert(reg.kind == REG_KIND_STACK);
                        used_ir_regs += callee_fn_type_analysis.outputs_count_table[XIR_IR_TYPE_INT];
                        printf("    lea %s,[rbp-%llu]\n", input_int_regs[0], reg.u.stack.offset);
                    }

                    if (callee_fn_type_analysis.inputs_count_table[XIR_IR_TYPE_INT] > XIR_STATIC_ARRAY_LENGTH(input_int_regs)) {
                        stack_top += (callee_fn_type_analysis.inputs_count_table[XIR_IR_TYPE_INT] - XIR_STATIC_ARRAY_LENGTH(input_int_regs)) * xirIrTypeSize(XIR_IR_TYPE_INT);
                    }

                    unsigned long long stack_padding = (STACK_ALIGNMENT - (stack_top % STACK_ALIGNMENT)) % STACK_ALIGNMENT;
                    stack_top += stack_padding;

                    printf("    sub rsp,%llu\n", stack_top - stack_pre_alloc_base);

                    for (size_t i = 0; i < callee_fn_type_analysis.inputs_count_table[XIR_IR_TYPE_INT]; i++) {
                        size_t input_int_reg_i = callee_return_on_memory + i;
                        if (input_int_reg_i < XIR_STATIC_ARRAY_LENGTH(input_int_regs)) {
                            printf("    mov %s,", input_int_regs[input_int_reg_i]);
                            emitReg(regs, call.inputs->d[i]);
                            printf("\n");
                        } else {
                            printf("    mov rax,");
                            emitReg(regs, call.inputs->d[i]);
                            printf("\n");
                            printf("    mov [rsp+%llu],rax\n", (input_int_reg_i - XIR_STATIC_ARRAY_LENGTH(input_int_regs)) * xirIrTypeSize(XIR_IR_TYPE_INT));
                        }
                    }

                    switch (op.kind) {
                        case XIR_IR_OP_KIND_CALL_FN_IMPL:
                            printf("    call impl_%zu\n", call.fn);
                            break;
                        case XIR_IR_OP_KIND_CALL_FN_IMPORT: {
                            LisStringView name = ir->fn_imports->d[call.fn].name;
                            printf("    call %.*s wrt ..plt\n", (int)name.length, name.data);
                            break;
                        }
                        default:
                            assert(0 && "unreachable");
                            break;
                    }

                    if (!callee_return_on_memory) {
                        for (size_t i = 0; i < callee_fn_type_analysis.outputs_count_table[XIR_IR_TYPE_INT]; i++) {
                            printf("    mov ");
                            emitReg(regs, used_ir_regs++);
                            printf(",%s\n", output_int_regs[i]);
                        }
                    }

                    printf("    add rsp,%llu\n", stack_top - stack_pre_alloc_base);
                    stack_top = stack_pre_alloc_base;

                    if (impl_return_on_memory) {
                        printf("    pop rdi\n");
                        stack_top -= 8;
                    }
                    break;
                }
                case XIR_IR_OP_KIND_RETURN: {
                    XirIrOpReturn _return = op.u._return;
                    if (!impl_return_on_memory) {
                        for (size_t i = 0; i < _return.outputs->h.length; i++) {
                            printf("    mov %s,", output_int_regs[i]);
                            emitReg(regs, _return.outputs->d[i]);
                            printf("\n");
                        }
                    } else {
                        for (size_t i = 0; i < _return.outputs->h.length; i++) {
                            printf("    mov rax,");
                            emitReg(regs, _return.outputs->d[i]);
                            printf("\n");
                            printf("    mov [%s+%llu],rax\n", input_int_regs[0], i * xirIrTypeSize(XIR_IR_TYPE_INT));
                        }
                    }

                    printf("    mov rsp,rbp\n");
                    printf("    pop rbp\n");
                    printf("    ret\n");
                    break;
                }
                case XIR_IR_OP_KIND_DATA_PTR:
                    printf("    lea rax,[data_%zu]\n", op.u.data_ptr);
                    printf("    mov ");
                    emitReg(regs, used_ir_regs++);
                    printf(",rax\n");
                    break;
                case XIR_IR_OP_KIND_BRANCH: {
                    XirIrOpBranch branch = op.u.branch;
                    const Reg *cond_reg = regs->d + branch.cond;
                    if (cond_reg->kind == REG_KIND_IMM_INT) {
                        if (cond_reg->u.imm_int) {
                            printf("    jmp .op_%zu\n", branch.op);
                        }
                        break;
                    }

                    printf("    cmp qword ");
                    emitReg(regs, branch.cond);
                    printf(",0\n");
                    printf("    jnz .op_%zu\n", branch.op);
                    break;
                }
                case XIR_IR_OP_KIND_SELECT: {
                    XirIrOpSelect select = op.u.select;
                    size_t op_index = i;
                    const Reg *cond_reg = regs->d + select.cond;
                    if (cond_reg->kind == REG_KIND_IMM_INT) {
                        duplicateRegs(regs, cond_reg->u.imm_int ? select.true_regs : select.false_regs, &used_ir_regs);
                    } else {
                        printf("    cmp qword ");
                        emitReg(regs, select.cond);
                        printf(",0\n");
                        printf("    jz .select%zu_false\n", op_index);

                        size_t prev_used_ir_regs = used_ir_regs;
                        duplicateRegs(regs, select.true_regs, &used_ir_regs);
                        used_ir_regs = prev_used_ir_regs;
                        printf("    jmp .select%zu_end\n", op_index);

                        printf("    .select%zu_false:\n", op_index);
                        duplicateRegs(regs, select.false_regs, &used_ir_regs);
                        printf("    .select%zu_end:\n", op_index);
                    }
                }
            }
        }
        printf("    .op_%zu:\n", impl.ops->h.length);
    }

    for (size_t i = 0; i < ir->datas->h.length; i++) {
        const XirIrData *data = ir->datas->d + i;
        printf("section ");
        if (data->flags & XIR_IR_DATA_FLAGS_READ_ONLY) {
            printf(".rodata");
        } else {
            printf(".data");
        }
        printf("\n");

        printf("    align %u,db 0\n", data->alignment);
        printf("    data_%zu:", i);
        if (data->bytes->h.length > 0) {
            printf(" db");
        }

        for (size_t i = 0; i < data->bytes->h.length; i++) {
            if (i > 0) {
                printf(",");
            }
            printf(" 0x%X", data->bytes->d[i]);
        }
        printf("\n");
    }

    printf("section .note.GNU-stack\n");
    rawrDynarrDestroy(RAWR_DYNARR_GENERAL_POINTER(&regs));
}
