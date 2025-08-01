#include "foreign_imports.h"
#include "ir.h"
#include <dlfcn.h>

void xirForeignFnImportsCallerInit(XirForeignFnImportsCaller *caller) {
    XIR_DYNARR_EASY_CREATE(&caller->cifs);
    XIR_DYNARR_EASY_CREATE(&caller->fn_ptrs);
}

void xirForeignFnImportsCallerCallImportCallback(XirInterp *interp, size_t regs_base, XirIrFnImportIndex import_index, void *user_data) {
    const XirIrFnImport *fn_import = interp->ir->fn_imports->d + import_index;
    const XirIrFnType *fn_type = interp->ir->fn_types->d + fn_import->type;

    XirForeignFnImportsCaller *caller = user_data;
    assert(fn_import->type < caller->cifs->h.length);
    ffi_cif *cif = caller->cifs->d + fn_import->type;
    assert(import_index < caller->fn_ptrs->h.length);
    XirFnPtr fn_ptr = caller->fn_ptrs->d[import_index];

    void *args[32];
    if (fn_type->input_types->h.length > XIR_STATIC_ARRAY_LENGTH(args)) {
        XIR_FATAL_ERROR("calling foreign function imports with more than %zu inputs isn't supported\n", XIR_STATIC_ARRAY_LENGTH(args));
    }

    for (size_t i = 0; i < fn_type->input_types->h.length; i++) {
        if (fn_type->input_types->d[i] != XIR_IR_TYPE_INT) {
            XIR_FATAL_ERROR("support for calling foreign function imports with non-integer inputs isn't implemented\n");
        }
        args[i] = interp->regs->d + regs_base + i;
    }

    XirInterpReg result;
    ffi_call(cif, fn_ptr, &result, args);
    interp->regs->h.length = regs_base;
    if (fn_type->output_types->h.length > 0) {
        assert(fn_type->output_types->h.length == 1);
        if (fn_type->output_types->d[0] != XIR_IR_TYPE_INT) {
            XIR_FATAL_ERROR("support for calling foreign function imports with non-integer outputs isn't implemented\n");
        }
        XIR_DYNARR_UNSAFE_PUSH(&interp->regs, result);
    }
}

static ffi_type *xirFfiTypeFromIrType(XirIrType type) {
    switch (type) {
        case XIR_IR_TYPE_INT:
            return &ffi_type_uint64;
        case XIR_IR_TYPE_FLOAT:
            return &ffi_type_double;
    }

    assert(0 && "unreachable");
}

void xirPrepareFfiCifFromFnType(const XirIrFnType *fn_type, ffi_cif *cif) {
    ffi_type **arg_types = xirCheckedMalloc(sizeof(arg_types[0]) * fn_type->input_types->h.length);

    for (size_t i = 0; i < fn_type->input_types->h.length; i++) {
        arg_types[i] = xirFfiTypeFromIrType(fn_type->input_types->d[i]);
    }

    ffi_type *return_type = &ffi_type_void;
    if (fn_type->output_types->h.length > 0) {
        if (fn_type->output_types->h.length > 1) {
            XIR_FATAL_ERROR("support for foreign function imports with more than 1 output isn't implemented\n");
        }

        return_type = xirFfiTypeFromIrType(fn_type->output_types->d[0]);
    }

    ffi_status status = ffi_prep_cif(cif, FFI_DEFAULT_ABI, fn_type->input_types->h.length, return_type, arg_types);
    assert(status == FFI_OK);
}

void xirForeignFnImportsCallerLoadForeignFnImportFromDynamicLibrary(
    XirForeignFnImportsCaller *caller,
    const XirIr *ir,
    XirBitSet prepared_cifs,
    void **library_handles,
    size_t library_handles_length,
    XirIrFnImportIndex fn_import_index
) {
    const XirIrFnImport *fn_import = ir->fn_imports->d + fn_import_index;

    if (!xirBitSetBitGet(prepared_cifs, fn_import->type)) {
        XIR_DYNARR_RESULT_CHECK(xirDynarrEnsureLength(XIR_DYNARR_GP(&caller->cifs), fn_import->type + 1));
        xirPrepareFfiCifFromFnType(
            ir->fn_types->d + fn_import->type,
            caller->cifs->d + fn_import->type
        );
        xirBitSetBitSetOn(prepared_cifs, fn_import->type);
    }

    char name[256];
    const size_t max_name_length = sizeof(name) - 1;
    if (fn_import->name.length > max_name_length) {
        XIR_FATAL_ERROR("dynamic loading of foreign function imports with names longer than %zu bytes isn't supported\n", max_name_length);
    }
    memcpy(name, fn_import->name.data, fn_import->name.length);
    name[fn_import->name.length] = '\0';

    for (size_t i = 0; i < library_handles_length; i++) {
        void *fn_ptr = dlsym(library_handles[i], name);
        if (fn_ptr) {
            XIR_DYNARR_RESULT_CHECK(xirDynarrEnsureLength(
                XIR_DYNARR_GP(&caller->fn_ptrs),
                fn_import_index + 1
            ));
            caller->fn_ptrs->d[fn_import_index] = (XirFnPtr)fn_ptr;
            return;
        }
    }

    XIR_FATAL_ERROR("failed to load foreign function import \"%s\" from any of the specified libraries\n", name);
}

void xirForeignFnImportsCallerLoadAllForeignFnImportsFromDynamicLibraries(
    XirForeignFnImportsCaller *caller,
    const XirIr *ir,
    void **library_handles,
    size_t library_handles_length
) {
    const size_t prepared_cifs_data_size = XIR_BIT_SET_CALCULATE_SIZE(ir->fn_types->h.length);
    XirBitSet prepared_cifs = {xirCheckedMalloc(prepared_cifs_data_size)};
    memset(prepared_cifs.data, 0, prepared_cifs_data_size);

    for (XirIrFnImportIndex i = 0; i < ir->fn_imports->h.length; i++) {
        xirForeignFnImportsCallerLoadForeignFnImportFromDynamicLibrary(caller, ir, prepared_cifs, library_handles, library_handles_length, i);
    }

    free(prepared_cifs.data);
}
