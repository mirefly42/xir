#include "ir.h"
#include "nasm.h"
#include "validation.h"
#include <lis/parser.h>
#include <lis/rawr_dynarr.h>
#include <stdio.h>

static void skipComments(LisStringView source, LisNodesView *view) {
    LisList *list;
    while (lisNodesViewPeekList(view, &list) &&
            list->h.length > 0 &&
            lisNodeIsAtom(list->d[0]) &&
            lisStringViewEqual(lisStringViewSliceWithRange(source, list->d[0].u.atom.range), XIR_SV("#"))) {
        lisNodesViewSkip(view);
    }
}

LisAtom nextAtomOrFail(LisStringView source, LisNodesView *view) {
    skipComments(source, view);
    LisAtom atom;
    if (!lisNodesViewNextAtom(view, &atom)) {
        XIR_FATAL_ERROR("expected atom\n");
    }
    return atom;
}

LisList *nextListOrFail(LisStringView source, LisNodesView *view) {
    skipComments(source, view);
    LisList *list;
    if (!lisNodesViewNextList(view, &list)) {
        XIR_FATAL_ERROR("expected list\n");
    }
    return list;
}

unsigned long long nextUllOrFail(LisStringView source, LisNodesView *view) {
    LisAtom atom = nextAtomOrFail(source, view);
    unsigned long long value;
    if (!lisStringViewToUll(lisStringViewSliceWithRange(source, atom.range), &value)) {
        XIR_FATAL_ERROR("expected number\n");
    }
    return value;
}

XirIrTypeDynarr *parseTypesList(LisStringView source, LisNodesView view) {
    XirIrTypeDynarr *types;
    XIR_DYNARR_EASY_CREATE(&types);

    while (!lisNodesViewIsEmpty(&view)) {
        LisAtom type_name_atom = nextAtomOrFail(source, &view);
        LisStringView type_name = lisStringViewSliceWithRange(source, type_name_atom.range);

        XirIrType type = 0;
        if (lisStringViewEqual(type_name, XIR_SV("int"))) {
            type = XIR_IR_TYPE_INT;
        } else if (lisStringViewEqual(type_name, XIR_SV("float"))) {
            type = XIR_IR_TYPE_FLOAT;
        } else {
            XIR_FATAL_ERROR("invalid type %.*s\n", (int)type_name.length, type_name.data);
        }

        XIR_DYNARR_UNSAFE_PUSH(&types, type);
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
        XIR_FATAL_ERROR("failed to initialize parser\n");
        return 1;
    }

    LisParserResult parser_result = lisParserParseListUntilEof(&parser);
    if (parser_result.kind != LIS_RESULT_KIND_SUCCESS) {
        XIR_FATAL_ERROR("parsing has failed\n");
        return 1;
    }

    LisList *root = parser_result.u.success.u.list;
    LisNodesView view = lisNodesViewFromList(root);

    XirIr ir = {0};
    XIR_DYNARR_EASY_CREATE(&ir.fn_types);
    XIR_DYNARR_EASY_CREATE(&ir.fn_impls);
    XIR_DYNARR_EASY_CREATE(&ir.fn_imports);
    XIR_DYNARR_EASY_CREATE(&ir.fn_exports);
    XIR_DYNARR_EASY_CREATE(&ir.datas);

    while (!lisNodesViewIsEmpty(&view)) {
        LisList *list = nextListOrFail(source, &view);
        LisNodesView view = lisNodesViewFromList(list); // NOTE: shadows top-level view

        LisAtom name_atom = nextAtomOrFail(source, &view);
        LisStringView name = lisStringViewSliceWithRange(source, name_atom.range);

        if (lisStringViewEqual(name, XIR_SV("fn-type"))) {
            LisNodesView input_types_view = lisNodesViewFromList(nextListOrFail(source, &view));
            XirIrTypeDynarr *input_types = parseTypesList(source, input_types_view);

            LisNodesView output_types_view = lisNodesViewFromList(nextListOrFail(source, &view));
            XirIrTypeDynarr *output_types = parseTypesList(source, output_types_view);

            if (!lisNodesViewIsEmpty(&view)) {
                XIR_FATAL_ERROR("fn-type directive expects 2 arguments\n");
            }

            XIR_DYNARR_UNSAFE_PUSH(&ir.fn_types, ((XirIrFnType){input_types, output_types}));
        } else if (lisStringViewEqual(name, XIR_SV("fn-impl"))) {
            XirIrFnTypeIndex fn_type_index = nextUllOrFail(source, &view);;
            XirIrOpDynarr *ops;
            XIR_DYNARR_EASY_CREATE(&ops);

            while (!lisNodesViewIsEmpty(&view)) {
                LisList *op_list = nextListOrFail(source, &view);
                LisNodesView view = lisNodesViewFromList(op_list); // NOTE: shadow

                LisAtom op_name_atom = nextAtomOrFail(source, &view);
                LisStringView op_name = lisStringViewSliceWithRange(source, op_name_atom.range);

                XirIrOp op = {0};
                if (lisStringViewEqual(op_name, XIR_SV("int"))) {
                    op = (XirIrOp){
                        XIR_IR_OP_KIND_INT,
                        {._int = nextUllOrFail(source, &view)},
                    };
                } else if (lisStringViewEqual(op_name, XIR_SV("call-impl")) || lisStringViewEqual(op_name, XIR_SV("call-import"))) {
                    XirIrOpKind kind = lisStringViewEqual(op_name, XIR_SV("call-impl")) ? XIR_IR_OP_KIND_CALL_FN_IMPL : XIR_IR_OP_KIND_CALL_FN_IMPORT;

                    size_t index = nextUllOrFail(source, &view);
                    XirIrRegIndexDynarr *inputs;
                    XIR_DYNARR_EASY_CREATE(&inputs);

                    while (!lisNodesViewIsEmpty(&view)) {
                        XIR_DYNARR_UNSAFE_PUSH(&inputs, nextUllOrFail(source, &view));
                    }

                    op = (XirIrOp){
                        kind,
                        {.call = {index, inputs}},
                    };
                } else if (lisStringViewEqual(op_name, XIR_SV("return"))) {
                    op = (XirIrOp){
                        XIR_IR_OP_KIND_RETURN,
                        {._return = nextUllOrFail(source, &view)},
                    };
                } else if (lisStringViewEqual(op_name, XIR_SV("data-ptr"))) {
                    op = (XirIrOp){
                        XIR_IR_OP_KIND_DATA_PTR,
                        {.data_ptr = nextUllOrFail(source, &view)},
                    };
                } else if (lisStringViewEqual(op_name, XIR_SV("branch"))) {
                    op.kind = XIR_IR_OP_KIND_BRANCH;
                    op.u.branch.cond = nextUllOrFail(source, &view);
                    op.u.branch.op = nextUllOrFail(source, &view);
                } else {
                    XIR_FATAL_ERROR("invalid operation %.*s\n", (int)op_name.length, op_name.data);
                }

                if (!lisNodesViewIsEmpty(&view)) {
                    XIR_FATAL_ERROR("too much arguments\n");
                }

                XIR_DYNARR_UNSAFE_PUSH(&ops, op);
            }

            XIR_DYNARR_UNSAFE_PUSH(&ir.fn_impls, ((XirIrFnImpl){fn_type_index, ops}));
        } else if (lisStringViewEqual(name, XIR_SV("fn-import"))) {
            XirIrFnTypeIndex fn_type_index = nextUllOrFail(source, &view);
            LisAtom fn_name_atom = nextAtomOrFail(source, &view);
            if (!lisNodesViewIsEmpty(&view)) {
                XIR_FATAL_ERROR("fn-import directive expects 2 arguments\n");
            }

            XIR_DYNARR_UNSAFE_PUSH(&ir.fn_imports, ((XirIrFnImport){fn_type_index, lisStringViewSliceWithRange(source, fn_name_atom.range)}));
        } else if (lisStringViewEqual(name, XIR_SV("fn-export"))) {
            XirIrFnImplIndex fn_impl_index = nextUllOrFail(source, &view);
            LisAtom fn_name_atom = nextAtomOrFail(source, &view);
            if (!lisNodesViewIsEmpty(&view)) {
                XIR_FATAL_ERROR("fn-export directive expects 2 arguments\n");
            }

            XIR_DYNARR_UNSAFE_PUSH(&ir.fn_exports, ((XirIrFnExport){fn_impl_index, lisStringViewSliceWithRange(source, fn_name_atom.range)}));
        } else if (lisStringViewEqual(name, XIR_SV("data"))) {
            LisList *flags_list = nextListOrFail(source, &view);
            LisNodesView flags_view = lisNodesViewFromList(flags_list);

            XirIrData data = {0};
            while (!lisNodesViewIsEmpty(&flags_view)) {
                LisAtom flag_atom = nextAtomOrFail(source, &flags_view);
                LisStringView flag_name = lisStringViewSliceWithRange(source, flag_atom.range);
                if (lisStringViewEqual(flag_name, XIR_SV("read-only"))) {
                    data.flags |= XIR_IR_DATA_FLAGS_READ_ONLY;
                } else {
                    XIR_FATAL_ERROR("invalid data flag %.*s\n", (int)flag_name.length, flag_name.data);
                }
            }

            data.alignment = nextUllOrFail(source, &view);

            XIR_DYNARR_EASY_CREATE(&data.bytes);

            while (!lisNodesViewIsEmpty(&view)) {
                unsigned long long byte = nextUllOrFail(source, &view);
                if (byte > 0xFF) {
                    XIR_FATAL_ERROR("invalid byte\n");
                }
                XIR_DYNARR_UNSAFE_PUSH(&data.bytes, byte);
            }

            XIR_DYNARR_UNSAFE_PUSH(&ir.datas, data);
        } else {
            XIR_FATAL_ERROR("invalid directive %.*s\n", (int)name.length, name.data);
        }
    }

    xirValidateIr(&ir);
    xirGenerateNasm(&ir);
}
