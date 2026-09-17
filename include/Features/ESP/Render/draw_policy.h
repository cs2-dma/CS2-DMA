#pragma once

#include "Game/Schema/structs.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace esp::render
{
    inline constexpr uint64_t kCachedViewMatrixHoldUs = 200000u;
    inline constexpr uint64_t kPrevTickFallbackMaxAgeUs = 100000u;
    inline constexpr uint64_t kDrawEventThrottleUs = 500000u;
    inline constexpr uint64_t kWebRadarBulkRecoveryWindowUs = 6000000u;
    inline constexpr uint64_t kWebRadarDeadHoldBulkRecoveryUs = 3500000u;
    inline constexpr uint64_t kWebRadarAliveHoldBulkRecoveryUs = 2500000u;
    inline constexpr uint64_t kWebRadarDeadHoldUs = 1800000u;
    inline constexpr uint64_t kWebRadarAliveHoldUs = 1200000u;
    inline constexpr uint64_t kBonePersistUs = 150000u;
    inline constexpr uint64_t kPlayerRenderMaxAgeUs = 500000u;
    inline constexpr uint64_t kPoseRenderMaxAgeUs = 250000u;
    inline constexpr uint64_t kRenderDelaySlackUs = 500u;
    inline constexpr float kMaxRenderExtrapolationSec = 0.040f;
    inline constexpr float kMinVelocityExtrapolation2D = 1.0f;
    inline constexpr uint64_t kBombPositionSampleMaxAgeUs = 50000u;
    inline constexpr float kBombMaxExtrapolationSec = 0.025f;
    inline constexpr float kBombMaxVelocity = 4000.0f;
    inline constexpr float kFallbackBombDefuseSeconds = 10.0f;
    inline constexpr float kFallbackStandingHeight = 72.0f;
    inline constexpr float kFallbackHeadPadding = 8.0f;
    inline constexpr float kFallbackFeetPadding = 4.0f;
    inline constexpr float kMinProjectedBoxHeight = 4.0f;
    inline constexpr int kFallbackStaleFrameCap = 250;

    inline bool ShouldRenderOverlayForMenuState(
        bool menuOpen,
        bool showWithMenu) noexcept
    {
        return !menuOpen || showWithMenu;
    }

    struct SnapshotTimingInput {
        uint64_t captureTimeUs = 0;
        uint64_t prevCaptureTimeUs = 0;
        uint64_t nowUs = 0;
        uint64_t nominalIntervalUs = 0;
    };

    struct SnapshotTimingResult {
        uint64_t snapshotIntervalUs = 0;
        uint64_t renderDelayUs = 0;
        uint64_t targetUs = 0;
        float lerpAlpha = 1.0f;
        float smoothAlpha = 1.0f;
        float extrapolationSec = 0.0f;
    };

    inline bool IsFreshTimestamp(uint64_t timestampUs, uint64_t nowUs, uint64_t maxAgeUs)
    {
        return timestampUs > 0 &&
               nowUs >= timestampUs &&
               (nowUs - timestampUs) <= maxAgeUs;
    }

    inline bool ShouldUpdateCachedViewMatrix(
        bool sampleValid, uint64_t sampleUs,
        bool cachedValid, uint64_t cachedUs, uint64_t nowUs)
    {
        return sampleValid && IsFreshTimestamp(sampleUs, nowUs, kCachedViewMatrixHoldUs) &&
            (!cachedValid || sampleUs >= cachedUs ||
             !IsFreshTimestamp(cachedUs, nowUs, kCachedViewMatrixHoldUs));
    }

    inline bool CanInterpolatePlayer(uintptr_t pawn, uintptr_t previousPawn,
        uint32_t handle, uint32_t previousHandle, int previousHealth,
        const Vector3& position, const Vector3& previousPosition) noexcept
    {
        return pawn != 0 && pawn == previousPawn && handle == previousHandle &&
            previousHealth > 0 && IsFiniteVec(position) && IsFiniteVec(previousPosition) &&
            (position - previousPosition).Length() < 128.0f;
    }

    inline bool ShouldReuseCachedViewMatrix(bool cachedValid, uint64_t cachedAtUs, uint64_t nowUs)
    {
        return cachedValid && IsFreshTimestamp(cachedAtUs, nowUs, kCachedViewMatrixHoldUs);
    }

    inline float SmoothSnapshotAlpha(float alpha)
    {
        const float clamped = std::clamp(alpha, 0.0f, 1.0f);
        return clamped * clamped * clamped *
               (clamped * (clamped * 6.0f - 15.0f) + 10.0f);
    }

    inline SnapshotTimingResult EvaluateSnapshotTiming(const SnapshotTimingInput& input)
    {
        SnapshotTimingResult result = {};
        result.snapshotIntervalUs =
            (input.captureTimeUs > input.prevCaptureTimeUs && input.prevCaptureTimeUs > 0)
            ? (input.captureTimeUs - input.prevCaptureTimeUs)
            : input.nominalIntervalUs;
        result.renderDelayUs = std::min(
            result.snapshotIntervalUs,
            input.nominalIntervalUs + kRenderDelaySlackUs);
        result.targetUs =
            (input.nowUs > result.renderDelayUs)
            ? (input.nowUs - result.renderDelayUs)
            : input.nowUs;

        result.lerpAlpha = 1.0f;
        result.extrapolationSec = 0.0f;
        if (input.captureTimeUs > input.prevCaptureTimeUs && input.prevCaptureTimeUs > 0) {
            if (result.targetUs <= input.prevCaptureTimeUs) {
                result.lerpAlpha = 0.0f;
            } else if (result.targetUs < input.captureTimeUs) {
                result.lerpAlpha =
                    static_cast<float>(result.targetUs - input.prevCaptureTimeUs) /
                    static_cast<float>(input.captureTimeUs - input.prevCaptureTimeUs);
            } else {
                result.lerpAlpha = 1.0f;
                result.extrapolationSec = std::clamp(
                    static_cast<float>(result.targetUs - input.captureTimeUs) / 1000000.0f,
                    0.0f,
                    kMaxRenderExtrapolationSec);
            }
        }
        result.smoothAlpha = SmoothSnapshotAlpha(result.lerpAlpha);
        return result;
    }

    inline bool ShouldTrackLastValidAlive(
        bool currentValid,
        bool isLocalSlot,
        int team)
    {
        return currentValid && !isLocalSlot && (team == 2 || team == 3);
    }

    // A missing current identity is not evidence that the previous occupant
    // is still present.  DataReader already absorbs transient hierarchy/core
    // read failures; rendering an old pawn into an empty slot can resurrect a
    // player for one frame after an authoritative death or slot eviction.
    inline bool IsSameNonZeroPlayerIdentity(
        uintptr_t currentPawn,
        uintptr_t previousPawn) noexcept
    {
        return currentPawn != 0 &&
               previousPawn != 0 &&
               currentPawn == previousPawn;
    }

    inline bool ShouldUsePrevTickFallback(
        bool prevValidAlive,
        bool slotIdentityMatchesCurrent,
        bool slotIdentityMatchesCache,
        bool currentConfirmedDead,
        bool prevIsLocal,
        uint64_t lastValidAgeUs)
    {
        return prevValidAlive &&
               slotIdentityMatchesCurrent &&
               slotIdentityMatchesCache &&
               !currentConfirmedDead &&
               !prevIsLocal &&
               lastValidAgeUs <= kPrevTickFallbackMaxAgeUs;
    }

    inline bool ShouldClearLastAliveCache(uintptr_t currentPawn, uintptr_t lastValidPawn)
    {
        return currentPawn == 0 ||
               (lastValidPawn != 0 && currentPawn != lastValidPawn);
    }

    inline int IncrementFallbackStaleFrames(int staleFrames)
    {
        return staleFrames < kFallbackStaleFrameCap
            ? staleFrames + 1
            : staleFrames;
    }

    inline bool ShouldRecordDrawEvent(uint64_t nowUs, uint64_t lastEventUs)
    {
        return nowUs > lastEventUs + kDrawEventThrottleUs;
    }

    inline bool IsWithinWebRadarBulkRecovery(uint64_t lastBulkEvictUs, uint64_t nowUs)
    {
        return IsFreshTimestamp(lastBulkEvictUs, nowUs, kWebRadarBulkRecoveryWindowUs);
    }

    inline uint64_t SelectWebRadarPlayerHoldUs(bool bulkRecovery, int heldHealth)
    {
        if (bulkRecovery)
            return heldHealth <= 0 ? kWebRadarDeadHoldBulkRecoveryUs
                                   : kWebRadarAliveHoldBulkRecoveryUs;

        return heldHealth <= 0 ? kWebRadarDeadHoldUs
                               : kWebRadarAliveHoldUs;
    }

    inline bool IsWebRadarCoreSampleFresh(
        uint64_t sourceUpdatedUs, uint64_t nowUs, int health, bool bulkRecovery)
    {
        return IsFreshTimestamp(sourceUpdatedUs, nowUs,
            SelectWebRadarPlayerHoldUs(bulkRecovery, health));
    }

    inline bool ShouldHoldWebRadarPlayer(
        bool liveContext,
        bool slotReused,
        bool heldValid,
        uintptr_t heldPawn,
        uint64_t heldSeenUs,
        uint64_t nowUs,
        uint64_t holdUs,
        bool currentConfirmedDead = false)
    {
        return liveContext &&
               !currentConfirmedDead &&
               !slotReused &&
               heldValid &&
               heldPawn != 0 &&
               IsFreshTimestamp(heldSeenUs, nowUs, holdUs);
    }

    inline bool ShouldReusePersistedBone(uint64_t lastBoneUs, uint64_t nowUs)
    {
        return IsFreshTimestamp(lastBoneUs, nowUs, kBonePersistUs);
    }

    inline bool ShouldApplyVelocityExtrapolation(
        float extrapolationSec,
        bool validVelocity,
        float velocity2D)
    {
        return extrapolationSec > 0.0f &&
               validVelocity &&
               velocity2D > kMinVelocityExtrapolation2D;
    }

    struct PlayerMotionTransform {
        Vector3 renderPosition = {};
        Vector3 interpolationOffset = {};
        Vector3 extrapolationOffset = {};
    };

    inline PlayerMotionTransform ResolvePlayerMotion(
        const Vector3& effectivePosition,
        const Vector3& smoothedPosition,
        const Vector3& velocity,
        bool validVelocity,
        float extrapolationSec)
    {
        PlayerMotionTransform result = {};
        result.interpolationOffset = smoothedPosition - effectivePosition;
        const float velocity2D = static_cast<float>(std::hypot(velocity.x, velocity.y));
        if (ShouldApplyVelocityExtrapolation(
                extrapolationSec,
                validVelocity,
                velocity2D)) {
            result.extrapolationOffset = velocity * extrapolationSec;
        }
        result.renderPosition = smoothedPosition + result.extrapolationOffset;
        return result;
    }

    // Presentation-only interpolation. Never consumes weapon/owner velocity or
    // changes the measured position sent to other consumers.
    struct BombPositionInterpolator {
        bool initialized = false;
        uint64_t scene = 0, generation = 0, sampleUs = 0, lastFrameUs = 0;
        uintptr_t entity = 0;
        Vector3 from = {}, target = {};
        uint64_t blendStartUs = 0, blendDurationUs = 0;

        void Reset() noexcept { *this = {}; }

        Vector3 Blend(uint64_t nowUs) const noexcept
        {
            if (blendDurationUs == 0 || nowUs < blendStartUs)
                return target;
            const float alpha = std::clamp(
                static_cast<float>(nowUs - blendStartUs) / static_cast<float>(blendDurationUs),
                0.0f, 1.0f);
            return from + (target - from) * alpha;
        }

        Vector3 Update(const Vector3& position, bool dropped, bool currentSource,
            uint64_t positionUs, uint64_t nowUs, uint64_t sceneSerial,
            uint64_t dropGeneration, uintptr_t positionEntity) noexcept
        {
            constexpr uint64_t maxGapUs = 100000;
            if (!dropped || !currentSource || dropGeneration == 0 || positionEntity == 0 ||
                !std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z) ||
                !IsFreshTimestamp(positionUs, nowUs, maxGapUs)) {
                Reset();
                return position;
            }
            const bool contextChanged = !initialized || scene != sceneSerial ||
                generation != dropGeneration || entity != positionEntity ||
                positionUs < sampleUs || nowUs < lastFrameUs || nowUs - lastFrameUs > maxGapUs;
            bool discontinuous = contextChanged;
            if (!contextChanged && positionUs > sampleUs) {
                const uint64_t interval = positionUs - sampleUs;
                const Vector3 delta = position - target;
                const float distanceSq = delta.x * delta.x + delta.y * delta.y + delta.z * delta.z;
                const float maxDistance = (std::min)(96.0f,
                    kBombMaxVelocity * static_cast<float>(interval) / 1000000.0f + 4.0f);
                discontinuous = interval > maxGapUs || !std::isfinite(distanceSq) ||
                    distanceSq > maxDistance * maxDistance;
            }
            if (discontinuous) {
                Reset();
                initialized = true;
                scene = sceneSerial;
                generation = dropGeneration;
                entity = positionEntity;
                sampleUs = positionUs;
                from = target = position;
            } else if (positionUs > sampleUs) {
                from = Blend(nowUs);
                target = position;
                blendDurationUs = std::clamp<uint64_t>(positionUs - sampleUs, 4000, 20000);
                blendStartUs = nowUs;
                sampleUs = positionUs;
            }
            lastFrameUs = nowUs;
            return Blend(nowUs);
        }
    };

    inline Vector3 ResolveBombRenderPosition(
        const Vector3& position,
        const Vector3& velocity,
        bool dropped,
        uint64_t positionSampleTimeUs,
        uint64_t nowUs)
    {
        const bool positionValid =
            std::isfinite(position.x) &&
            std::isfinite(position.y) &&
            std::isfinite(position.z);
        const float velocityMagnitude = static_cast<float>(
            std::sqrt(
                velocity.x * velocity.x +
                velocity.y * velocity.y +
                velocity.z * velocity.z));
        const bool velocityValid =
            std::isfinite(velocityMagnitude) &&
            velocityMagnitude > kMinVelocityExtrapolation2D &&
            velocityMagnitude <= kBombMaxVelocity;
        if (!positionValid ||
            !dropped ||
            !velocityValid ||
            !IsFreshTimestamp(positionSampleTimeUs, nowUs, kBombPositionSampleMaxAgeUs)) {
            return position;
        }

        const float ageSec = std::min(
            static_cast<float>(nowUs - positionSampleTimeUs) / 1000000.0f,
            kBombMaxExtrapolationSec);
        return position + velocity * ageSec;
    }

    enum class BombDefuseOutcome : uint8_t
    {
        Unknown,
        Completes,
        Explodes
    };

    inline float ResolveBombDefuseTotal(float defuseLength)
    {
        return std::isfinite(defuseLength) &&
               defuseLength >= 4.0f &&
               defuseLength <= 11.0f
            ? defuseLength
            : kFallbackBombDefuseSeconds;
    }

    inline float CalculateBombDefuseProgress(float remaining, float total)
    {
        if (!std::isfinite(remaining) ||
            !std::isfinite(total) ||
            total <= 0.0f) {
            return 0.0f;
        }
        return std::clamp(1.0f - (remaining / total), 0.0f, 1.0f);
    }

    inline BombDefuseOutcome EvaluateBombDefuseOutcome(
        float defuseEndTime,
        float blowTime)
    {
        if (!std::isfinite(defuseEndTime) ||
            !std::isfinite(blowTime) ||
            defuseEndTime <= 0.0f ||
            blowTime <= 0.0f) {
            return BombDefuseOutcome::Unknown;
        }
        return defuseEndTime <= blowTime
            ? BombDefuseOutcome::Completes
            : BombDefuseOutcome::Explodes;
    }

    inline bool ShouldUseFallbackProjectionBox(
        bool projectedOnScreen,
        float feetY,
        float headY,
        bool fallbackOnScreen)
    {
        return (!projectedOnScreen || feetY <= headY + kMinProjectedBoxHeight) &&
               fallbackOnScreen;
    }

    inline bool IsRenderableBoxHeight(float boxHeight)
    {
        return boxHeight >= kMinProjectedBoxHeight;
    }
}
