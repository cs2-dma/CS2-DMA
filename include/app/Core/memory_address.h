#pragma once

#include <cstdint>
#include <cstddef>

namespace app::memory_address
{
    inline constexpr uintptr_t kMinimumUserAddress = 0x0000000000010000ull;
    inline constexpr uintptr_t kMaximumUserAddress = 0x0000800000000000ull;

    constexpr bool IsCanonicalUserPointer(uintptr_t value) noexcept
    {
        return value >= kMinimumUserAddress &&
               value < kMaximumUserAddress;
    }

    constexpr bool IsLikelyGamePointer(uintptr_t value) noexcept
    {
        return IsCanonicalUserPointer(value) &&
               (value & (alignof(uintptr_t) - 1u)) == 0u;
    }

    constexpr uintptr_t SanitizeGamePointer(uintptr_t value) noexcept
    {
        return IsLikelyGamePointer(value) ? value : 0;
    }

    // A complete null pointer is authoritative absence. A partial/invalid
    // sample is unknown and must not clear a previously validated identity.
    constexpr bool IsCompleteGamePointerSample(
        uintptr_t value, std::size_t bytesRead) noexcept
    {
        return bytesRead == sizeof(value) &&
               (value == 0 || IsLikelyGamePointer(value));
    }
}
