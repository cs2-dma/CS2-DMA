#pragma once

#include <cstdint>

namespace target::convars
{
    struct Values
    {
        bool resolved = false;
        bool weaponAccuracyNoSpread = false;
        float weaponAccuracyForceSpread = 0.0f;
        float jumpImpulse = 301.993377f;
        float damageScaleCtHead = 1.0f;
        float damageScaleTHead = 1.0f;
        float damageScaleCtBody = 1.0f;
        float damageScaleTBody = 1.0f;
        float clientInterpolation = 0.0f;
        float clientInterpolationRatio = 0.0f;
        int clientUpdateRate = 0;
        bool accuracyValid = false;
        bool jumpValid = false;
        bool damageScaleValid = false;
        bool interpolationValid = false;
        uint64_t updatedAtUs = 0;
    };

    Values Read();
    void Reset();
}
