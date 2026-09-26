#pragma once

#include <cstddef>
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace esp::data
{
    inline constexpr uint8_t kInvalidPlayerFlagSample = 0xFFu;
    inline constexpr uint64_t kPlayerFlagReadGapHoldUs = 50000u;
    inline constexpr float kFlashDurationMaxSeconds = 10.0f;
    inline constexpr float kFlashGameTimeMaxSeconds = 100000.0f;
    inline constexpr float kFlashFutureTimeToleranceSeconds = 0.50f;

    struct PlayerFlagFilterState
    {
        bool active = false;
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
               bangTime <= currentGameTime + kFlashDurationMaxSeconds + kFlashFutureTimeToleranceSeconds;
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
        float currentGameTime)
    {
        if (!IsValidFlashDurationSample(duration) ||
            !IsValidFlashBangTimeSample(bangTime, currentGameTime)) {
            return {};
        }
        if (duration == 0.0f) return {true, false, 0.0f};

        const float remainingSeconds = bangTime - currentGameTime;
        if (!std::isfinite(remainingSeconds) ||
            remainingSeconds > duration + kFlashFutureTimeToleranceSeconds)
            return {};

        const bool active = duration > 0.0f && remainingSeconds > 0.0f;
        return {
            true,
            active,
            active ? (std::min)(remainingSeconds, duration) : 0.0f
        };
    }

    inline bool IsPlayerFlagFresh(uint64_t sampledAtUs, uint64_t nowUs)
    {
        return sampledAtUs != 0 && nowUs >= sampledAtUs &&
            nowUs - sampledAtUs <= kPlayerFlagReadGapHoldUs;
    }

    inline float RemainingBlindSeconds(float remaining, uint64_t sampledAtUs, uint64_t nowUs)
    {
        if (!IsValidFlashDurationSample(remaining) || !IsPlayerFlagFresh(sampledAtUs, nowUs))
            return 0.0f;
        const float result = remaining - static_cast<float>(nowUs - sampledAtUs) / 1000000.0f;
        return result > 0.0f ? result : 0.0f;
    }

    struct BlindFlashState
    {
        float remainingSeconds = 0.0f;
        uint64_t sampledAtUs = 0;

        float Update(bool requested, BlindFlashSample sample, uint64_t sampleUs, uint64_t nowUs)
        {
            if (!requested) {
                *this = {};
                return 0.0f;
            }
            if (sample.fresh) {
                remainingSeconds = sample.active ? sample.remainingSeconds : 0.0f;
                sampledAtUs = sampleUs;
            }
            return RemainingBlindSeconds(remainingSeconds, sampledAtUs, nowUs);
        }
    };

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
            if (!IsPlayerFlagFresh(state.lastFreshUs, nowUs))
                ResetPlayerFlagFilter(state);
            return state.active;
        }
        state.lastFreshUs = nowUs;
        state.active = sampleActive;
        return state.active;
    }

    template <typename Player>
    inline void CommitPlayerFlags(Player& player, const PlayerFlagFilterState& scoped,
        const PlayerFlagFilterState& defusing, const BlindFlashState& flash, uint64_t nowUs)
    {
        const bool alive = player.valid && player.pawn != 0 && player.health > 0;
        player.scopedUpdatedUs = alive ? scoped.lastFreshUs : 0;
        player.defusingUpdatedUs = alive ? defusing.lastFreshUs : 0;
        player.scoped = alive && scoped.active && IsPlayerFlagFresh(scoped.lastFreshUs, nowUs);
        player.defusing = alive && defusing.active && IsPlayerFlagFresh(defusing.lastFreshUs, nowUs);
        player.flashUpdatedUs = alive ? flash.sampledAtUs : 0;
        player.flashDuration = alive ? flash.remainingSeconds : 0.0f;
        player.flashed = alive && RemainingBlindSeconds(
            player.flashDuration, player.flashUpdatedUs, nowUs) > 0.0f;
    }
}
