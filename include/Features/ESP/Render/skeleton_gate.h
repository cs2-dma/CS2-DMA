#pragma once

namespace esp::render
{
    inline bool ShouldDrawSkeleton(bool reliableBones, int projectedSegments) noexcept
    {
        return reliableBones && projectedSegments > 0;
    }
}
