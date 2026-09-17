#pragma once

#include <cstdint>

namespace esp::recovery
{
    inline constexpr uint32_t kDmaHardReinitializeFailureThreshold = 5;

    inline bool ShouldHardReinitializeDma(
        uint32_t consecutiveFailures,
        uint32_t threshold = kDmaHardReinitializeFailureThreshold)
    {
        return threshold > 0 && consecutiveFailures >= threshold;
    }
}
