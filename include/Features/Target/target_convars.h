#pragma once

#include <cstdint>

namespace target::convars
{
    namespace policy
    {
        inline constexpr uint64_t kResolveRetryUs = 5000000u;
        inline constexpr uint8_t kMaximumOptionalResolveAttempts = 3;
        inline constexpr uint8_t kReadFailuresBeforeResolve = 3;

        inline bool ShouldResolve(uint64_t nowUs, uint64_t lastAttemptUs,
            bool instanceValid, bool requiredMissing, bool optionalMissing,
            uint8_t optionalAttempts, bool repeatedReadFailure) noexcept
        {
            if (lastAttemptUs != 0 && nowUs >= lastAttemptUs &&
                nowUs - lastAttemptUs < kResolveRetryUs)
                return false;
            return !instanceValid || requiredMissing || repeatedReadFailure ||
                (optionalMissing && optionalAttempts < kMaximumOptionalResolveAttempts);
        }

        inline uint8_t NextReadFailureCount(uint8_t previous, bool succeeded) noexcept
        {
            return succeeded ? 0 : previous < kReadFailuresBeforeResolve
                ? static_cast<uint8_t>(previous + 1) : kReadFailuresBeforeResolve;
        }
    }

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
        float recoilScale = 2.0f;
        bool recoilScaleValid = false;
    };

    void Start();
    Values Read(uint64_t sceneSerial = 0);
    void Reset();
}
