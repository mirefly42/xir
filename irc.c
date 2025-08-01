#include "foreign_imports.h"
#include "interp.h"
#include "ir.h"
#include "nasm.h"
#include "validation.h"
#include <dlfcn.h>
#include <lis/parser.h>
#include <lis/rawr_dynarr.h>
#include <stdint.h>
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

static LisStringView readEntireFile(const char *path) {
    FILE *stream = fopen(path, "rb");
    if (!stream) {
        perror("fopen");
        abort();
    }

    if (fseek(stream, 0, SEEK_END) < 0) {
        perror("fseek");
        abort();
    }

    long size = ftell(stream);
    if (size < 0) {
        perror("ftell");
        abort();
    }

    if ((unsigned long long)size > SIZE_MAX) {
        fprintf(stderr, "error: input file is too large\n");
        abort();
    }

    if (fseek(stream, 0, SEEK_SET) < 0) {
        perror("fseek");
        abort();
    }

    char *buf = malloc(size);
    if (!buf) {
        perror("malloc");
        abort();
    }

    if (fread(buf, 1, size, stream) != (size_t)size) {
        perror("fread");
        abort();
    }

    fclose(stream);

    return (LisStringView){buf, size};
}

static void printUsage(int argc, char *argv[]) {
    fprintf(stderr, "Usage: %s [-i] [-l library] file\n", argc ? argv[0] : "");
}

typedef void *XirVoidPtr;
XIR_DYNARR_EASY_GEN(XirVoidPtr);

typedef char *XirCString;
XIR_DYNARR_EASY_GEN(XirCString);

int main(int argc, char *argv[]) {
    const char *source_path = NULL;
    bool opt_interp = false;
    XirCStringDynarr *libraries = NULL;
    XIR_DYNARR_EASY_CREATE(&libraries);
    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if (arg[0] == '-') {
            if (strcmp(arg, "-i") == 0) {
                opt_interp = true;
            } else if (strcmp(arg, "-l") == 0) {
                char *library_path = argv[++i];
                if (!library_path) {
                    fprintf(stderr, "error: option '%s' requires an argument\n", arg);
                    printUsage(argc, argv);
                    abort();
                }

                XIR_DYNARR_UNSAFE_PUSH(&libraries, library_path);
            } else {
                fprintf(stderr, "error: invalid flag '%s'\n", arg);
                printUsage(argc, argv);
                abort();
            }
        } else {
            if (!source_path) {
                source_path = arg;
            } else {
                fprintf(stderr, "error: processing multiple input files isn't supported\n");
                printUsage(argc, argv);
                abort();
            }
        }
    }

    if (!source_path) {
        fprintf(stderr, "error: missing input file\n");
        printUsage(argc, argv);
        abort();
    }

    if (libraries->h.length > 0 && !opt_interp) {
        fprintf(stderr, "error: can't load libraries in compilation mode\n");
        abort();
    }

    LisStringView source = readEntireFile(source_path);

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
    xirIrInit(&ir);

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
    if (opt_interp) {
        XirVoidPtrDynarr *library_handles = NULL;
        XIR_DYNARR_RESULT_CHECK(rawrDynarrCreate(XIR_DYNARR_GP(&library_handles), libraries->h.length, libraries->h.length, rawr_dynarr_default_allocator));

        for (size_t i = 0; i < libraries->h.length; i++) {
            void *handle = dlopen(libraries->d[i], RTLD_LAZY | RTLD_LOCAL);
            if (!handle) {
                XIR_FATAL_ERROR("failed to load library '%s': %s\n", libraries->d[i], dlerror());
            }

            library_handles->d[i] = handle;
        }

        XirForeignFnImportsCaller foreign_fn_imports_caller = {0};
        xirForeignFnImportsCallerInit(&foreign_fn_imports_caller);
        xirForeignFnImportsCallerLoadAllForeignFnImportsFromDynamicLibraries(
            &foreign_fn_imports_caller,
            &ir,
            library_handles->d,
            library_handles->h.length
        );

        XirInterp interp = {0};
        xirInterpInit(&interp, &ir, xirForeignFnImportsCallerCallImportCallback);
        interp.user_data = &foreign_fn_imports_caller;

        for (size_t i = 0; i < interp.ir->fn_exports->h.length; i++) {
            const XirIrFnExport *export = interp.ir->fn_exports->d + i;
            if (lisStringViewEqual(export->name, XIR_SV("main"))) {
                xirInterpEvalImpl(&interp, i, 0);
                if (interp.regs->h.length > 0) {
                    return interp.regs->d[RAWR_DYNARR_LAST_INDEX(interp.regs)];
                } else {
                    fprintf(stderr, "error: no registers remains after evaluation of \"main\" function\n");
                    abort();
                }
            }
        }

        fprintf(stderr, "error: ir doesn't export \"main\" function\n");
        abort();
    } else {
        xirGenerateNasm(&ir);
    }
}
