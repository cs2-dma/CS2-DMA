#pragma once

#include <cstddef>
#include <cmath>
#include <cstdint>

namespace esp::data
{
    inline constexpr uint8_t kInvalidPlayerFlagSample = 0xFFu;
    inline constexpr uint8_t kPlayerFlagPositiveConfirmSamples = 2u;
    inline constexpr uint64_t kPlayerFlagReadGapHoldUs = 50000u;
    inline constexpr float kFlashFlagActivateSeconds = 0.20f;
    inline constexpr float kFlashFlagReleaseSeconds = 0.08f;
    inline constexpr float kFlashDurationMaxSeconds = 10.0f;
    inline constexpr float kFlashGameTimeMaxSeconds = 100000.0f;
    inline constexpr float kFlashFutureTimeToleranceSeconds = 0.50f;

    struct PlayerFlagFilterState
    {
        bool active = false;
        uint8_t positiveSamples = 0;
        uint64_t lastFreshUs = 0;
    };

    inline bool IsValidPlayerFlagSample(uint8_t sample)
    {
        return sample <= 1u;
    }

    inline bool IsBinaryPlayerFlagReadComplete(
        bool requested,
        std::size_t bytesRead,
        uint8_t sample)
    {
        return requested &&
               bytesRead == sizeof(uint8_t) &&
               IsValidPlayerFlagSample(sample);
    }

    inline bool IsValidFlashDurationSample(float duration)
    {
        return std::isfinite(duration) &&
               duration >= 0.0f &&
               duration <= kFlashDurationMaxSeconds;
    }

    inline bool IsValidFlashBangTimeSample(float bangTime, float currentGameTime)
    {
        return std::isfinite(bangTime) &&
               std::isfinite(currentGameTime) &&
               bangTime >= 0.0f &&
               currentGameTime >= 1.0f &&
               bangTime <= kFlashGameTimeMaxSeconds &&
               currentGameTime <= kFlashGameTimeMaxSeconds &&
               bangTime <= currentGameTime + kFlashFutureTimeToleranceSeconds;
    }

    inline bool IsBlindFlashReadComplete(
        bool requested,
        bool gameTimeFresh,
        std::size_t bangTimeBytesRead,
        std::size_t durationBytesRead,
        float bangTime,
        float duration,
        float currentGameTime)
    {
        return requested &&
               gameTimeFresh &&
               bangTimeBytesRead == sizeof(float) &&
               durationBytesRead == sizeof(float) &&
               IsValidFlashBangTimeSample(bangTime, currentGameTime) &&
               IsValidFlashDurationSample(duration);
    }

    struct BlindFlashSample
    {
        bool fresh = false;
        bool active = false;
        float remainingSeconds = 0.0f;
    };

    inline BlindFlashSample EvaluateBlindFlashSample(
        float bangTime,
        float duration,
        float currentGameTime,
        bool currentlyActive)
    {
        if (!IsValidFlashDurationSample(duration) ||
            !IsValidFlashBangTimeSample(bangTime, currentGameTime)) {
            return {};
        }

        const float remainingSeconds = bangTime + duration - currentGameTime;
        if (!std::isfinite(remainingSeconds))
            return {};

        const bool active = currentlyActive
            ? remainingSeconds > kFlashFlagReleaseSeconds
            : remainingSeconds >= kFlashFlagActivateSeconds;
        return {
            true,
            active,
            active ? remainingSeconds : 0.0f
        };
    }

    inline void ResetPlayerFlagFilter(PlayerFlagFilterState& state)
    {
        state = {};
    }

    inline bool UpdatePlayerFlagFilter(
        PlayerFlagFilterState& state,
        bool requested,
        bool sampleFresh,
        bool sampleActive,
        uint64_t nowUs)
    {
        if (!requested) {
            ResetPlayerFlagFilter(state);
            return false;
        }

        if (!sampleFresh) {
            const bool gapWithinHold =
                (state.active || state.positiveSamples != 0u) &&
                state.lastFreshUs > 0 &&
                nowUs >= state.lastFreshUs &&
                (nowUs - state.lastFreshUs) <= kPlayerFlagReadGapHoldUs;
            if (!gapWithinHold) {
                ResetPlayerFlagFilter(state);
                return false;
            }
            return state.active;
        }

        state.lastFreshUs = nowUs;
        if (!sampleActive) {
            state.active = false;
            state.positiveSamples = 0;
            return false;
        }

        if (state.active)
            return true;

        if (state.positiveSamples < kPlayerFlagPositiveConfirmSamples)
            ++state.positiveSamples;
        if (state.positiveSamples >= kPlayerFlagPositiveConfirmSamples) {
            state.active = true;
            state.positiveSamples = kPlayerFlagPositiveConfirmSamples;
        }
        return state.active;
    }
}
