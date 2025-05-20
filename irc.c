#include "ir.h"
#include "nasm.h"
#include <lis/parser.h>
#include <lis/rawr_dynarr.h>
#include <stdio.h>

LisAtom nextAtomOrFail(LisNodesView *view) {
    LisAtom atom;
    if (!lisNodesViewNextAtom(view, &atom)) {
        FATAL_ERROR("expected atom\n");
    }
    return atom;
}

LisList *nextListOrFail(LisNodesView *view) {
    LisList *list;
    if (!lisNodesViewNextList(view, &list)) {
        FATAL_ERROR("expected list\n");
    }
    return list;
}

unsigned long long nextUllOrFail(LisStringView source, LisNodesView *view) {
    LisAtom atom = nextAtomOrFail(view);
    unsigned long long value;
    if (!lisStringViewToUll(lisStringViewSliceWithRange(source, atom.range), &value)) {
        FATAL_ERROR("expected number\n");
    }
    return value;
}

IrTypeDynarr *parseTypesList(LisStringView source, LisNodesView view) {
    IrTypeDynarr *types;
    DYNARR_EASY_CREATE(&types);

    while (!lisNodesViewIsEmpty(&view)) {
        LisAtom type_name_atom = nextAtomOrFail(&view);
        LisStringView type_name = lisStringViewSliceWithRange(source, type_name_atom.range);

        IrType type = 0;
        if (lisStringViewEqual(type_name, SV("int"))) {
            type = IR_TYPE_INT;
        } else if (lisStringViewEqual(type_name, SV("float"))) {
            type = IR_TYPE_FLOAT;
        } else {
            FATAL_ERROR("invalid type %.*s\n", (int)type_name.length, type_name.data);
        }

        DYNARR_UNSAFE_PUSH(&types, type);
    }

    return types;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s source\n", argv[0]);
        return 1;
    }

    LisStringView source = lisStringViewFromCString(argv[1]);

    LisParser parser;
    if (lisParserInit(&parser, source)) {
        FATAL_ERROR("failed to initialize parser\n");
        return 1;
    }

    LisParserResult parser_result = lisParserParseListUntilEof(&parser);
    if (parser_result.kind != LIS_RESULT_KIND_SUCCESS) {
        FATAL_ERROR("parsing has failed\n");
        return 1;
    }

    LisList *root = parser_result.u.success.u.list;
    LisNodesView view = lisNodesViewFromList(root);

    Ir ir = {0};
    DYNARR_EASY_CREATE(&ir.fn_types);
    DYNARR_EASY_CREATE(&ir.fn_impls);
    DYNARR_EASY_CREATE(&ir.fn_imports);
    DYNARR_EASY_CREATE(&ir.fn_exports);
    DYNARR_EASY_CREATE(&ir.datas);

    while (!lisNodesViewIsEmpty(&view)) {
        LisList *list = nextListOrFail(&view);
        LisNodesView view = lisNodesViewFromList(list); // NOTE: shadows top-level view

        LisAtom name_atom = nextAtomOrFail(&view);
        LisStringView name = lisStringViewSliceWithRange(source, name_atom.range);

        if (lisStringViewEqual(name, SV("fn-type"))) {
            LisNodesView input_types_view = lisNodesViewFromList(nextListOrFail(&view));
            IrTypeDynarr *input_types = parseTypesList(source, input_types_view);

            LisNodesView output_types_view = lisNodesViewFromList(nextListOrFail(&view));
            IrTypeDynarr *output_types = parseTypesList(source, output_types_view);

            if (!lisNodesViewIsEmpty(&view)) {
                FATAL_ERROR("fn-type directive expects 2 arguments\n");
            }

            DYNARR_UNSAFE_PUSH(&ir.fn_types, ((IrFnType){input_types, output_types}));
        } else if (lisStringViewEqual(name, SV("fn-impl"))) {
            IrFnTypeIndex fn_type_index = nextUllOrFail(source, &view);;
            IrOpDynarr *ops;
            DYNARR_EASY_CREATE(&ops);

            while (!lisNodesViewIsEmpty(&view)) {
                LisList *op_list = nextListOrFail(&view);
                LisNodesView view = lisNodesViewFromList(op_list); // NOTE: shadow

                LisAtom op_name_atom = nextAtomOrFail(&view);
                LisStringView op_name = lisStringViewSliceWithRange(source, op_name_atom.range);

                IrOp op = {0};
                if (lisStringViewEqual(op_name, SV("int"))) {
                    op = (IrOp){
                        IR_OP_KIND_INT,
                        {._int = nextUllOrFail(source, &view)},
                    };
                } else if (lisStringViewEqual(op_name, SV("call-impl")) || lisStringViewEqual(op_name, SV("call-import"))) {
                    IrOpKind kind = lisStringViewEqual(op_name, SV("call-impl")) ? IR_OP_KIND_CALL_FN_IMPL : IR_OP_KIND_CALL_FN_IMPORT;

                    size_t index = nextUllOrFail(source, &view);
                    IrRegIndexDynarr *inputs;
                    DYNARR_EASY_CREATE(&inputs);

                    while (!lisNodesViewIsEmpty(&view)) {
                        DYNARR_UNSAFE_PUSH(&inputs, nextUllOrFail(source, &view));
                    }

                    op = (IrOp){
                        kind,
                        {.call = {index, inputs}},
                    };
                } else if (lisStringViewEqual(op_name, SV("return"))) {
                    op = (IrOp){
                        IR_OP_KIND_RETURN,
                        {._return = nextUllOrFail(source, &view)},
                    };
                } else if (lisStringViewEqual(op_name, SV("data-ptr"))) {
                    op = (IrOp){
                        IR_OP_KIND_DATA_PTR,
                        {.data_ptr = nextUllOrFail(source, &view)},
                    };
                } else if (lisStringViewEqual(op_name, SV("goto"))) {
                    op = (IrOp){
                        IR_OP_KIND_GOTO,
                        {._goto = nextUllOrFail(source, &view)},
                    };
                } else {
                    FATAL_ERROR("invalid operation %.*s\n", (int)op_name.length, op_name.data);
                }

                if (!lisNodesViewIsEmpty(&view)) {
                    FATAL_ERROR("too much arguments\n");
                }

                DYNARR_UNSAFE_PUSH(&ops, op);
            }

            DYNARR_UNSAFE_PUSH(&ir.fn_impls, ((IrFnImpl){fn_type_index, ops}));
        } else if (lisStringViewEqual(name, SV("fn-import"))) {
            IrFnTypeIndex fn_type_index = nextUllOrFail(source, &view);
            LisAtom fn_name_atom = nextAtomOrFail(&view);
            if (!lisNodesViewIsEmpty(&view)) {
                FATAL_ERROR("fn-import directive expects 2 arguments\n");
            }

            DYNARR_UNSAFE_PUSH(&ir.fn_imports, ((IrFnImport){fn_type_index, lisStringViewSliceWithRange(source, fn_name_atom.range)}));
        } else if (lisStringViewEqual(name, SV("fn-export"))) {
            IrFnImplIndex fn_impl_index = nextUllOrFail(source, &view);
            LisAtom fn_name_atom = nextAtomOrFail(&view);
            if (!lisNodesViewIsEmpty(&view)) {
                FATAL_ERROR("fn-export directive expects 2 arguments\n");
            }

            DYNARR_UNSAFE_PUSH(&ir.fn_exports, ((IrFnExport){fn_impl_index, lisStringViewSliceWithRange(source, fn_name_atom.range)}));
        } else if (lisStringViewEqual(name, SV("data"))) {
            LisList *flags_list = nextListOrFail(&view);
            LisNodesView flags_view = lisNodesViewFromList(flags_list);

            IrData data = {0};
            while (!lisNodesViewIsEmpty(&flags_view)) {
                LisAtom flag_atom = nextAtomOrFail(&flags_view);
                LisStringView flag_name = lisStringViewSliceWithRange(source, flag_atom.range);
                if (lisStringViewEqual(flag_name, SV("read-only"))) {
                    data.flags |= IR_DATA_FLAGS_READ_ONLY;
                } else {
                    FATAL_ERROR("invalid data flag %.*s\n", (int)flag_name.length, flag_name.data);
                }
            }

            data.alignment = nextUllOrFail(source, &view);

            DYNARR_EASY_CREATE(&data.bytes);

            while (!lisNodesViewIsEmpty(&view)) {
                unsigned long long byte = nextUllOrFail(source, &view);
                if (byte > 0xFF) {
                    FATAL_ERROR("invalid byte\n");
                }
                DYNARR_UNSAFE_PUSH(&data.bytes, byte);
            }

            DYNARR_UNSAFE_PUSH(&ir.datas, data);
        } else {
            FATAL_ERROR("invalid directive %.*s\n", (int)name.length, name.data);
        }
    }

    generateNasm(&ir);
}
