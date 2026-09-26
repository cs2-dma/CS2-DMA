#pragma once

#include "Game/Schema/structs.h"
#include <cmath>
#include <cstdint>

namespace esp::data
{
    inline void CommitRecoilSample(Vector3& value, bool& valid, uint64_t& updatedAtUs,
        const Vector3& sample, bool complete, bool reset, uint64_t nowUs) noexcept
    {
        if (reset) {
            value = {};
            valid = false;
            updatedAtUs = 0;
        } else if (complete && std::isfinite(sample.x) && std::isfinite(sample.y) &&
            std::isfinite(sample.z) && std::fabs(sample.x) <= 45.0f &&
            std::fabs(sample.y) <= 45.0f && std::fabs(sample.z) <= 10.0f && nowUs != 0) {
            value = sample;
            valid = true;
            updatedAtUs = nowUs;
        } else if (updatedAtUs == 0 || nowUs < updatedAtUs || nowUs - updatedAtUs > 50000u) {
            value = {};
            valid = false;
            updatedAtUs = 0;
        }
    }
}
