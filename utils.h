#ifndef XIR_UTILS_H
#define XIR_UTILS_H

#include <lis/rawr_dynarr.h>
#include <lis/string_view.h>
#include <stdio.h>
#include <stdlib.h>

#define XIR_STATIC_ARRAY_LENGTH(array) (sizeof(array) / sizeof((array)[0]))

#define XIR_SV lisStringViewFromCString

#define XIR_FATAL_ERROR(...) do { \
        fprintf(stderr, "fatal error in " __FILE__ ":%d: ", __LINE__); \
        fprintf(stderr, __VA_ARGS__); \
        abort(); \
    } while (0)

#define XIR_DYNARR_EASY_GEN(type) typedef RAWR_DYNARR_GEN(type, type##Dynarr) type##Dynarr

#define XIR_DYNARR_RESULT_CHECK(result) do { \
        if (result) { \
            XIR_FATAL_ERROR("allocation failed\n"); \
        } \
    } while (0)

#define XIR_DYNARR_GP RAWR_DYNARR_GENERAL_POINTER

#define XIR_DYNARR_EASY_CREATE(dynarr_pp) \
    XIR_DYNARR_RESULT_CHECK(rawrDynarrCreateDefault(XIR_DYNARR_GP(dynarr_pp)))

#define XIR_DYNARR_UNSAFE_PUSH(dynarr_pp, value) do { \
        XIR_DYNARR_RESULT_CHECK(rawrDynarrExtend(XIR_DYNARR_GP(dynarr_pp), 1)); \
        (**(dynarr_pp)).d[RAWR_DYNARR_LAST_INDEX(*(dynarr_pp))] = (value); \
    } while (0)

RawrDynarrResult xirDynarrEnsureLength(RawrDynarrGeneralPointer gp, size_t length);
void *xirCheckedMalloc(size_t size);

#endif
