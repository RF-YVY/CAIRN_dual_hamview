#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
using std::max;
using std::min;
inline size_t strlcpy(char *out, const char *in, size_t size)
{
    const size_t length = strlen(in);
    if (size) {
        memcpy(out, in, min(length, size - 1));
        out[min(length, size - 1)] = 0;
    }
    return length;
}
