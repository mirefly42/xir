#include "nasm.h"

#define STATIC_ARRAY_LENGTH(array) (sizeof(array) / sizeof((array)[0]))

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

DYNARR_EASY_GEN(Reg);

static unsigned long long irTypeSize(IrType type) {
    switch (type) {
        case IR_TYPE_INT: return 8;
        case IR_TYPE_FLOAT: return 16;
    }
}

void emitReg(const RegDynarr *regs, IrRegIndex index) {
    assert(index <= regs->h.length);
    Reg reg = regs->d[index];
    switch (reg.kind) {
        case REG_KIND_IMM_INT:
            printf("%llu", reg.u.imm_int);
            break;
        case REG_KIND_STACK:
            printf("[rbp-%llu]", reg.u.stack.offset);
            break;
    }
}

void generateNasm(const Ir *ir) {
    printf("default rel\n");

    RegDynarr *regs;
    DYNARR_EASY_CREATE(&regs);

    for (size_t i = 0; i < ir->fn_imports->h.length; i++) {
        IrFnImport import = ir->fn_imports->d[i];
        printf("extern %.*s\n", (int)import.name.length, import.name.data);
    }

    for (size_t i = 0; i < ir->fn_exports->h.length; i++) {
        IrFnExport export = ir->fn_exports->d[i];
        printf("global %.*s\n", (int)export.name.length, export.name.data);
    }

    printf("section .text\n");

    for (size_t i = 0; i < ir->fn_impls->h.length; i++) {
        regs->h.length = 0;

        IrFnImpl impl = ir->fn_impls->d[i];
        printf("impl_%zu:\n", i);
        for (size_t export_i = 0; export_i < ir->fn_exports->h.length; export_i++) {
            IrFnExport export = ir->fn_exports->d[export_i];
            if (export.impl == i) {
                printf("%.*s:\n", (int)export.name.length, export.name.data);
            }
        }

        printf("    push rbp\n");
        printf("    mov rbp,rsp\n");

        const IrTypeDynarr *input_types = ir->fn_types->d[impl.type].input_types;

        size_t used_ints = 0;
        unsigned long long stack_top = 0;
        for (size_t i = 0; i < input_types->h.length; i++) {
            IrType type = input_types->d[i];
            switch (type) {
                case IR_TYPE_INT:
                    if (used_ints >= STATIC_ARRAY_LENGTH(input_int_regs)) {
                        FATAL_ERROR("support for function impls with more than %zu integer inputs isn't implemented\n", STATIC_ARRAY_LENGTH(input_int_regs));
                    }
                    stack_top += irTypeSize(type);
                    printf("    push %s\n", input_int_regs[used_ints]);
                    used_ints++;
                    DYNARR_UNSAFE_PUSH(&regs, ((Reg){REG_KIND_STACK, {.stack = {stack_top}}}));
                    break;
                case IR_TYPE_FLOAT:
                    FATAL_ERROR("support for function impls with floating point inputs isn't implemented\n");
                    break;
            }
        }

        unsigned long long stack_pre_alloc_base = stack_top;

        for (size_t i = 0; i < impl.ops->h.length; i++) {
            IrOp op = impl.ops->d[i];
            switch (op.kind) {
                case IR_OP_KIND_INT:
                    DYNARR_UNSAFE_PUSH(&regs, ((Reg){REG_KIND_IMM_INT, {.imm_int = op.u._int}}));
                    break;
                case IR_OP_KIND_CALL_FN_IMPL:
                case IR_OP_KIND_CALL_FN_IMPORT: {
                    IrFnTypeIndex fn_type_index = irOpCallFnTypeIndex(ir, &op);
                    const IrTypeDynarr *output_types = ir->fn_types->d[fn_type_index].output_types;
                    for (size_t i = 0; i < output_types->h.length; i++) {
                        unsigned long long output_type_size = irTypeSize(output_types->d[i]);
                        stack_top += output_type_size;
                        DYNARR_UNSAFE_PUSH(&regs, ((Reg){REG_KIND_STACK, {.stack = {stack_top}}}));
                    }
                    break;
                }
                case IR_OP_KIND_DATA_PTR:
                    stack_top += irTypeSize(IR_TYPE_INT);
                    DYNARR_UNSAFE_PUSH(&regs, ((Reg){REG_KIND_STACK, {.stack = {stack_top}}}));
                    break;
                case IR_OP_KIND_RETURN:
                case IR_OP_KIND_BRANCH:
                    break;
            }
        }

        unsigned long long stack_padding = (STACK_ALIGNMENT - (stack_top % STACK_ALIGNMENT)) % STACK_ALIGNMENT;
        stack_top += stack_padding;

        printf("    sub rsp,%llu\n", stack_top - stack_pre_alloc_base);
        size_t used_ir_regs = input_types->h.length;

        for (size_t i = 0; i < impl.ops->h.length; i++) {
            IrOp op = impl.ops->d[i];
            printf("    .op_%zu:\n", i);
            switch (op.kind) {
                case IR_OP_KIND_INT:
                    used_ir_regs++;
                    break;
                case IR_OP_KIND_CALL_FN_IMPL:
                case IR_OP_KIND_CALL_FN_IMPORT: {
                    IrOpCall call = op.u.call;
                    IrFnTypeIndex callee_type_index = irOpCallFnTypeIndex(ir, &op);
                    IrFnType callee_type = ir->fn_types->d[callee_type_index];

                    size_t used_ints = 0;
                    for (size_t i = 0; i < callee_type.input_types->h.length; i++) {
                        IrType input_type = callee_type.input_types->d[i];
                        switch (input_type) {
                            case IR_TYPE_INT:
                                if (used_ints >= STATIC_ARRAY_LENGTH(input_int_regs)) {
                                    FATAL_ERROR("support for function calls with more than %zu integer inputs isn't implemented\n", STATIC_ARRAY_LENGTH(input_int_regs));
                                }
                                printf("    mov %s,", input_int_regs[used_ints]);
                                emitReg(regs, call.inputs->d[i]);
                                printf("\n");
                                used_ints++;
                                break;
                            case IR_TYPE_FLOAT:
                                FATAL_ERROR("support for function calls with floating point inputs isn't implemented\n");
                                break;
                        }
                    }


                    switch (op.kind) {
                        case IR_OP_KIND_CALL_FN_IMPL:
                            printf("    call impl_%zu\n", call.fn);
                            break;
                        case IR_OP_KIND_CALL_FN_IMPORT: {
                            LisStringView name = ir->fn_imports->d[call.fn].name;
                            printf("    call %.*s wrt ..plt\n", (int)name.length, name.data);
                            break;
                        }
                        default:
                            assert(0 && "unreachable");
                            break;
                    }

                    used_ints = 0;
                    for (size_t i = 0; i < callee_type.output_types->h.length; i++) {
                        IrType output_type = callee_type.output_types->d[i];
                        switch (output_type) {
                            case IR_TYPE_INT:
                                if (used_ints >= STATIC_ARRAY_LENGTH(output_int_regs)) {
                                    FATAL_ERROR("support for function calls with more than %zu integer outputs isn't implemented\n", STATIC_ARRAY_LENGTH(output_int_regs));
                                }
                                printf("    mov ");
                                emitReg(regs, used_ir_regs++);
                                printf(",%s\n", output_int_regs[used_ints]);
                                used_ints++;
                                break;
                            case IR_TYPE_FLOAT:
                                FATAL_ERROR("support for function calls with floating point outputs isn't implemented\n");
                                break;
                        }
                    }
                    break;
                }
                case IR_OP_KIND_RETURN:
                    printf("    mov rax,");
                    emitReg(regs, op.u._return);
                    printf("\n");

                    printf("    mov rsp,rbp\n");
                    printf("    pop rbp\n");
                    printf("    ret\n");
                    break;
                case IR_OP_KIND_DATA_PTR:
                    printf("    lea rax,[data_%zu]\n", op.u.data_ptr);
                    printf("    mov ");
                    emitReg(regs, used_ir_regs++);
                    printf(",rax\n");
                    break;
                case IR_OP_KIND_BRANCH: {
                    IrOpBranch branch = op.u.branch;
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
            }
        }
        printf("    .op_%zu:\n", impl.ops->h.length);
    }

    for (size_t i = 0; i < ir->datas->h.length; i++) {
        const IrData *data = ir->datas->d + i;
        printf("section ");
        if (data->flags & IR_DATA_FLAGS_READ_ONLY) {
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
