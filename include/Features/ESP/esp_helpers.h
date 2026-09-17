#pragma once

#include <cmath>
#include "Game/Schema/structs.h"

namespace esp {

inline bool IsLikelyViewMatrix(const view_matrix_t& matrix)
{
    float absSum = 0.0f;
    int nonZeroCount = 0;
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            const float value = matrix[row][col];
            if (!std::isfinite(value))
                return false;
            absSum += std::fabs(value);
            if (std::fabs(value) > 0.0001f)
                ++nonZeroCount;
        }
    }
    return nonZeroCount >= 6 && absSum > 1.0f;
}

} 
