#ifndef IR_H
#define IR_H

#include "utils.h"

typedef enum {
    IR_TYPE_INT,
    IR_TYPE_FLOAT,
} IrType;

DYNARR_EASY_GEN(IrType);

typedef struct IrFnType {
    IrTypeDynarr *input_types;
    IrTypeDynarr *output_types;
} IrFnType;

typedef size_t IrFnTypeIndex;
DYNARR_EASY_GEN(IrFnType);

typedef enum {
    IR_OP_KIND_INT,
    IR_OP_KIND_CALL_FN_IMPL,
    IR_OP_KIND_CALL_FN_IMPORT,
    IR_OP_KIND_RETURN,
} IrOpKind;

typedef size_t IrRegIndex;
DYNARR_EASY_GEN(IrRegIndex);

typedef unsigned long long IrOpInt;

typedef struct IrOpCall {
    size_t fn;
    IrRegIndexDynarr *inputs;
} IrOpCall;

typedef IrRegIndex IrOpReturn;

typedef struct IrOp {
    IrOpKind kind;
    union {
        IrOpInt _int;
        IrOpCall call;
        IrOpReturn _return;
    } u;
} IrOp;

DYNARR_EASY_GEN(IrOp);

typedef struct IrFnImpl {
    IrFnTypeIndex type;
    IrOpDynarr *ops;
} IrFnImpl;

DYNARR_EASY_GEN(IrFnImpl);

typedef struct IrFnImport {
    IrFnTypeIndex type;
    LisStringView name;
} IrFnImport;

typedef size_t IrFnImplIndex;
DYNARR_EASY_GEN(IrFnImport);

typedef struct IrFnExport {
    IrFnImplIndex impl;
    LisStringView name;
} IrFnExport;

DYNARR_EASY_GEN(IrFnExport);

typedef struct Ir {
    IrFnTypeDynarr *fn_types;
    IrFnImplDynarr *fn_impls;
    IrFnImportDynarr *fn_imports;
    IrFnExportDynarr *fn_exports;
} Ir;

#endif
