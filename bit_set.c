#include "bit_set.h"

bool xirBitSetBitGet(XirBitSet bit_set, unsigned long long index) {
    return (bit_set.data[index / CHAR_BIT] >> (index % CHAR_BIT)) & 1;
}

void xirBitSetBitSetOn(XirBitSet bit_set, unsigned long long index) {
    bit_set.data[index / CHAR_BIT] |= 1u << (index % CHAR_BIT);
}
