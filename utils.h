#ifndef UTILS_H
#define UTILS_H

#include <lis/rawr_dynarr.h>
#include <lis/string_view.h>
#include <stdio.h>
#include <stdlib.h>

#define SV lisStringViewFromCString

#define FATAL_ERROR(...) do { \
        fprintf(stderr, "fatal error in " __FILE__ ":%d: ", __LINE__); \
        fprintf(stderr, __VA_ARGS__); \
        abort(); \
    } while (0)

#define DYNARR_EASY_GEN(type) typedef RAWR_DYNARR_GEN(type, type##Dynarr) type##Dynarr

#define DYNARR_EASY_CREATE(dynarr_pp) do { \
        if (rawrDynarrCreateDefault(RAWR_DYNARR_GENERAL_POINTER(dynarr_pp))) { \
            FATAL_ERROR("allocation failed\n"); \
        } \
    } while (0)

#define DYNARR_UNSAFE_PUSH(dynarr_pp, value) do { \
        if (rawrDynarrExtend(RAWR_DYNARR_GENERAL_POINTER(dynarr_pp), 1)) { \
            FATAL_ERROR("allocation failed\n"); \
        } \
        (**(dynarr_pp)).d[RAWR_DYNARR_LAST_INDEX(*(dynarr_pp))] = (value); \
    } while (0)

#endif
