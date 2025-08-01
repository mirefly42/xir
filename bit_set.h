#ifndef XIR_BIT_SET_H
#define XIR_BIT_SET_H

#include <limits.h>
#include <stdbool.h>

// warning: this macro evaluates its argument twice
#define XIR_BIT_SET_CALCULATE_SIZE(bits_count) ((bits_count) / CHAR_BIT + ((bits_count) % CHAR_BIT > 0))

typedef struct XirBitSet {
    unsigned char *data;
} XirBitSet;

bool xirBitSetBitGet(XirBitSet bit_set, unsigned long long index);
void xirBitSetBitSetOn(XirBitSet bit_set, unsigned long long index);

#endif
