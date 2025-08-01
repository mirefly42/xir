#ifndef XIR_IR_H
#define XIR_IR_H

#include "utils.h"

typedef enum {
    XIR_IR_TYPE_INT,
    XIR_IR_TYPE_FLOAT,
    XIR_IR_TYPES_COUNT,
} XirIrType;

XIR_DYNARR_EASY_GEN(XirIrType);

typedef struct XirIrFnType {
    XirIrTypeDynarr *input_types;
    XirIrTypeDynarr *output_types;
} XirIrFnType;

typedef size_t XirIrFnTypeIndex;
XIR_DYNARR_EASY_GEN(XirIrFnType);

typedef enum {
    XIR_IR_OP_KIND_INT,
    XIR_IR_OP_KIND_CALL_FN_IMPL,
    XIR_IR_OP_KIND_CALL_FN_IMPORT,
    XIR_IR_OP_KIND_RETURN,
    XIR_IR_OP_KIND_DATA_PTR,
    XIR_IR_OP_KIND_BRANCH,
} XirIrOpKind;

typedef size_t XirIrRegIndex;
XIR_DYNARR_EASY_GEN(XirIrRegIndex);

typedef unsigned long long XirIrOpInt;

typedef struct XirIrOpCall {
    size_t fn;
    XirIrRegIndexDynarr *inputs;
} XirIrOpCall;

typedef XirIrRegIndex XirIrOpReturn;

typedef size_t XirIrDataIndex;
typedef XirIrDataIndex XirIrOpDataPtr;
typedef size_t XirIrOpIndex;

typedef struct XirIrOpBranch {
    XirIrRegIndex cond;
    XirIrOpIndex op;
} XirIrOpBranch;

typedef struct XirIrOp {
    XirIrOpKind kind;
    union {
        XirIrOpInt _int;
        XirIrOpCall call;
        XirIrOpReturn _return;
        XirIrOpDataPtr data_ptr;
        XirIrOpBranch branch;
    } u;
} XirIrOp;

XIR_DYNARR_EASY_GEN(XirIrOp);

typedef struct XirIrFnImpl {
    XirIrFnTypeIndex type;
    XirIrOpDynarr *ops;
} XirIrFnImpl;

XIR_DYNARR_EASY_GEN(XirIrFnImpl);

typedef struct XirIrFnImport {
    XirIrFnTypeIndex type;
    LisStringView name;
} XirIrFnImport;

typedef size_t XirIrFnImportIndex;

typedef size_t XirIrFnImplIndex;
XIR_DYNARR_EASY_GEN(XirIrFnImport);

typedef struct XirIrFnExport {
    XirIrFnImplIndex impl;
    LisStringView name;
} XirIrFnExport;

XIR_DYNARR_EASY_GEN(XirIrFnExport);

typedef enum {
    XIR_IR_DATA_FLAGS_READ_ONLY = 0x1,
} XirIrDataFlags;

typedef RAWR_DYNARR_GEN(unsigned char, XirUnsignedCharDynarr) XirUnsignedCharDynarr;

typedef struct XirIrData {
    XirIrDataFlags flags;
    unsigned int alignment;
    XirUnsignedCharDynarr *bytes;
} XirIrData;

XIR_DYNARR_EASY_GEN(XirIrData);

typedef struct XirIr {
    XirIrFnTypeDynarr *fn_types;
    XirIrFnImplDynarr *fn_impls;
    XirIrFnImportDynarr *fn_imports;
    XirIrFnExportDynarr *fn_exports;
    XirIrDataDynarr *datas;
} XirIr;

void xirIrInit(XirIr *ir);
XirIrFnTypeIndex xirIrOpCallFnTypeIndex(const XirIr *ir, const XirIrOp *op);

#endif
