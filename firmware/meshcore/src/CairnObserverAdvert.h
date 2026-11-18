#pragma once
#include <cstddef>
#include <cstdint>

namespace CairnObserver
{
constexpr bool isOwnAdvertisement(unsigned type, const uint8_t *payload, size_t length, const uint8_t *key)
{
    if (type != 4 || !payload || !key || length < 101)
        return false;
    for (size_t i = 0; i < 32; ++i)
        if (payload[i] != key[i])
            return false;
    return true;
}
} // namespace CairnObserver
