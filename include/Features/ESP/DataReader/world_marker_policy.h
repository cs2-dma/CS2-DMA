#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace esp::data
{
    inline constexpr int kMaxDroppedWeaponMarkers = 384;
    inline constexpr int kMaxUtilityEffectMarkers = 128;
    inline constexpr int kMaxProjectileMarkers = 64;
    inline constexpr int kMaxDroppedBombMarkers = 8;
    inline constexpr int kWorldMarkerScratchCapacity = kMaxDroppedWeaponMarkers +
        kMaxUtilityEffectMarkers + kMaxProjectileMarkers + kMaxDroppedBombMarkers;
    inline constexpr uint64_t kWorldMarkerReadGapHoldUs = 350000u;

    inline bool IsWorldMarkerSourceFresh(uint64_t sampleUs, uint64_t nowUs) noexcept
    {
        return sampleUs != 0 && nowUs >= sampleUs &&
            nowUs - sampleUs <= kWorldMarkerReadGapHoldUs;
    }

    inline bool ShouldProcessDroppedItem(bool heavyCadence, bool discoveryShardActive,
                                        bool previouslyTracked) noexcept
    {
        return previouslyTracked || !heavyCadence || discoveryShardActive;
    }

    inline int ClampWorldMarkerCount(int count, size_t capacity) noexcept
    {
        return count <= 0 ? 0 : static_cast<int>(
            (std::min)(static_cast<size_t>(count), capacity));
    }

    // Stable, allocation-free priority selection. Source capacity and published
    // capacity are independent; never expose a count larger than the destination.
    template <typename Marker, size_t N, typename Priority>
    int PublishBoundedWorldMarkers(
        const Marker* source, size_t sourceCapacity, int sourceCount,
        Marker (&destination)[N], uint64_t nowUs, Priority priority)
    {
        int written = 0;
        const int available = source ? ClampWorldMarkerCount(sourceCount, sourceCapacity) : 0;
        for (int tier = 0; tier < 4 && written < static_cast<int>(N); ++tier) {
            for (int i = 0; i < available && written < static_cast<int>(N); ++i) {
                const auto& marker = source[i];
                if (!marker.valid || priority(marker) != tier ||
                    (marker.expiresUs != 0 && marker.expiresUs <= nowUs))
                    continue;
                destination[written++] = marker;
            }
        }
        for (size_t i = static_cast<size_t>(written); i < N; ++i)
            destination[i] = {};
        return written;
    }

    enum class WorldMarkerDomain : uint8_t
    {
        DroppedItem,
        DroppedBomb,
        ActiveUtility,
    };

    inline bool ShouldPreserveWorldMarker(
        WorldMarkerDomain domain,
        bool bombRefreshDue,
        bool droppedItemsRefreshDue,
        bool activeUtilityRefreshDue) noexcept
    {
        switch (domain) {
        case WorldMarkerDomain::DroppedItem:
            return !droppedItemsRefreshDue;
        case WorldMarkerDomain::DroppedBomb:
            return !droppedItemsRefreshDue && !bombRefreshDue;
        case WorldMarkerDomain::ActiveUtility:
            return !activeUtilityRefreshDue;
        }
        return false;
    }

    inline bool ShouldHoldWorldMarkerOnReadGap(
        uintptr_t sourceEntity, bool slotRequested, bool entityReadComplete,
        uintptr_t currentEntity, bool detailsComplete,
        uint64_t sourceSampleUs, uint64_t nowUs) noexcept
    {
        if (sourceEntity == 0 || !IsWorldMarkerSourceFresh(sourceSampleUs, nowUs))
            return false;
        if (slotRequested && entityReadComplete) {
            if (currentEntity != sourceEntity)
                return false; // Includes an authoritative null/deleted entity.
            if (detailsComplete)
                return false; // Current processing owns the result, including termination.
        }
        return true;
    }

    inline constexpr float kUtilityRemainingMaxSec = 60.0f;
    inline constexpr float kUtilityTickSkewToleranceSec = 2.0f;
    inline constexpr float kSmokeDurationSec = 18.0f;
    inline constexpr float kMolotovDurationSec = 7.0f;
    inline float ResolveInfernoDuration(float lifetime, bool available) noexcept
    {
        return available && std::isfinite(lifetime) && lifetime >= 0.5f && lifetime <= 30.0f
            ? lifetime : kMolotovDurationSec;
    }

    inline bool HasSmokeActivation(bool activeAvailable, uint8_t active,
        bool spawnedAvailable, uint8_t spawned) noexcept
    {
        // Volume receipt and a stationary projectile do not prove detonation.
        return (activeAvailable && active == 1u) ||
            (spawnedAvailable && spawned == 1u);
    }
    inline constexpr float kDecoyDurationSec = 15.0f;
    inline constexpr float kExplosiveDurationSec = 0.1f;
    inline constexpr uint64_t kSmokeFallbackUs = 18000000u;
    inline constexpr uint64_t kMolotovFallbackUs = 7000000u;
    inline constexpr uint64_t kDecoyFallbackUs = 15000000u;
    inline constexpr uint64_t kExplosiveFallbackUs = 100000u;
    inline constexpr uint64_t kProjectileMarkerHoldUs = 300000u;
    inline constexpr uint64_t kUtilityStationaryMinUs = 40000u;
    inline constexpr uint64_t kUtilityStationaryMaxSampleGapUs = 350000u;
    inline constexpr uint8_t kUtilityStationaryMinSamples = 2u;
    inline constexpr uint64_t kUtilityFieldHoldUs = 350000u;

    enum class UtilityField : size_t {
        SmokeTick, SmokeActive, SmokeVolume, SmokeSpawned,
        InfernoTick, InfernoLife, InfernoFireCount, InfernoPostEffect,
        DecoyTick, DecoyClientTick, ExplodeTick, Velocity, Count
    };
    using UtilityFieldTimes = std::array<uint64_t, static_cast<size_t>(UtilityField::Count)>;

    inline bool UpdateUtilityFieldFreshness(
        UtilityFieldTimes& times, UtilityField field, bool complete,
        uint64_t nowUs) noexcept
    {
        auto& stamp = times[static_cast<size_t>(field)];
        if (complete)
            stamp = nowUs;
        return stamp != 0 && nowUs >= stamp && nowUs - stamp <= kUtilityFieldHoldUs;
    }

    enum class UtilityTimerSource : uint8_t
    {
        None = 0,
        GameTick,
        EffectState,
        StationaryFallback,
    };

    struct UtilityTimerDecision
    {
        bool active = false;
        bool terminal = false;
        float remainingSec = -1.0f;
        uint64_t fallbackStartUs = 0;
        UtilityTimerSource source = UtilityTimerSource::None;
    };

    inline bool IsUtilityRemainingUnknown(float remainingSec)
    {
        return !std::isfinite(remainingSec) ||
               remainingSec < 0.0f ||
               remainingSec > kUtilityRemainingMaxSec;
    }

    inline float ClampUtilityRemainingSeconds(float remainingSec)
    {
        if (!std::isfinite(remainingSec))
            return -1.0f;
        if (remainingSec < 0.0f)
            return 0.0f;
        if (remainingSec > kUtilityRemainingMaxSec)
            return -1.0f;
        return remainingSec;
    }

    inline float CalculateUtilityRemainingFromTick(
        int tickStart,
        float durationSec,
        float intervalPerTick,
        float currentGameTime)
    {
        if (intervalPerTick <= 0.0f ||
            tickStart <= 0 ||
            durationSec <= 0.0f ||
            currentGameTime <= 0.0f) {
            return -1.0f;
        }

        const float startSec = static_cast<float>(tickStart) * intervalPerTick;
        const float elapsedSec = currentGameTime - startSec;
        if (elapsedSec < -kUtilityTickSkewToleranceSec ||
            elapsedSec > durationSec + kUtilityTickSkewToleranceSec) {
            return -1.0f;
        }

        return ClampUtilityRemainingSeconds(durationSec - elapsedSec);
    }

    inline bool IsUtilityTickExpired(
        int tickStart,
        float durationSec,
        float intervalPerTick,
        float currentGameTime) noexcept
    {
        if (!std::isfinite(intervalPerTick) || !std::isfinite(durationSec) ||
            !std::isfinite(currentGameTime) || intervalPerTick <= 0.0f ||
            tickStart <= 0 ||
            durationSec <= 0.0f ||
            currentGameTime <= 0.0f) {
            return false;
        }
        const float startSec = static_cast<float>(tickStart) * intervalPerTick;
        return currentGameTime >= startSec &&
               (currentGameTime - startSec) >= durationSec;
    }

    inline uint64_t UpdateUtilityMarkerDeadline(
        bool signal, bool terminal, float remainingSec, uint64_t nowUs,
        uint64_t fallbackDurationUs, uint64_t& deadlineUs,
        UtilityTimerSource source = UtilityTimerSource::None) noexcept
    {
        if (terminal) {
            deadlineUs = nowUs; // Keep an expired tombstone: no fallback re-arming.
            return 0;
        }
        if (signal && !IsUtilityRemainingUnknown(remainingSec)) {
            const uint64_t candidate = nowUs + static_cast<uint64_t>(
                (std::max)(0.0f, remainingSec) * 1000000.0f);
            // A late stationary observation must not extend an already known
            // game-tick deadline or re-arm an expired effect.
            if (deadlineUs == 0 || source == UtilityTimerSource::GameTick || candidate < deadlineUs)
                deadlineUs = candidate;
        } else if (signal && deadlineUs == 0) {
            deadlineUs = nowUs + fallbackDurationUs;
        }
        // A read miss keeps the last deadline, never a newly invented full duration.
        return deadlineUs > nowUs ? deadlineUs : 0;
    }

    inline bool UpdateUtilityStationaryEvidence(
        bool positionFresh,
        bool positionUnchanged,
        uint64_t nowUs,
        uint64_t& lastSampleUs,
        uint64_t& stationarySinceUs,
        uint8_t& stationarySamples) noexcept
    {
        if (!positionFresh || nowUs == 0u) {
            return lastSampleUs != 0u &&
                   nowUs >= lastSampleUs &&
                   (nowUs - lastSampleUs) <= kUtilityStationaryMaxSampleGapUs &&
                   stationarySinceUs != 0u &&
                   stationarySamples >= kUtilityStationaryMinSamples &&
                   nowUs >= stationarySinceUs &&
                   (nowUs - stationarySinceUs) >= kUtilityStationaryMinUs;
        }

        const bool discontinuity =
            lastSampleUs == 0u ||
            nowUs < lastSampleUs ||
            (nowUs - lastSampleUs) > kUtilityStationaryMaxSampleGapUs;
        if (discontinuity || !positionUnchanged) {
            lastSampleUs = nowUs;
            stationarySinceUs = nowUs;
            stationarySamples = 1u;
            return false;
        }

        lastSampleUs = nowUs;
        if (stationarySamples < UINT8_MAX)
            ++stationarySamples;
        return stationarySinceUs != 0u &&
               stationarySamples >= kUtilityStationaryMinSamples &&
               nowUs >= stationarySinceUs &&
               (nowUs - stationarySinceUs) >= kUtilityStationaryMinUs;
    }

    inline UtilityTimerDecision ResolveUtilityTimer(
        bool identityConfirmed,
        bool explicitTerminal,
        bool tickRemainingKnown,
        float tickRemainingSec,
        bool effectActive,
        bool allowStationaryFallback,
        bool stationaryConfirmed,
        uint64_t stationarySinceUs,
        uint64_t nowUs,
        float durationSec) noexcept
    {
        UtilityTimerDecision result{};
        if (!identityConfirmed)
            return result;
        if (explicitTerminal) {
            result.terminal = true;
            return result;
        }
        if (tickRemainingKnown && !IsUtilityRemainingUnknown(tickRemainingSec)) {
            const float remaining = ClampUtilityRemainingSeconds(tickRemainingSec);
            if (remaining <= 0.0f) {
                result.terminal = true;
                return result;
            }
            result.active = true;
            result.remainingSec = remaining;
            result.source = UtilityTimerSource::GameTick;
            return result;
        }
        if (effectActive) {
            result.active = true;
            result.source = UtilityTimerSource::EffectState;
            return result;
        }
        if (!allowStationaryFallback ||
            !stationaryConfirmed ||
            stationarySinceUs == 0u ||
            nowUs < stationarySinceUs ||
            durationSec <= 0.0f) {
            return result;
        }

        const float elapsedSec =
            static_cast<float>(nowUs - stationarySinceUs) / 1000000.0f;
        const float remaining = durationSec - elapsedSec;
        if (remaining <= 0.0f) {
            result.terminal = true;
            return result;
        }
        result.active = true;
        result.remainingSec = remaining;
        result.fallbackStartUs = stationarySinceUs;
        result.source = UtilityTimerSource::StationaryFallback;
        return result;
    }
}
