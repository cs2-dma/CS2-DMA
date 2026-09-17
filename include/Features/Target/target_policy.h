#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cmath>

namespace target::policy
{
    inline constexpr float kDefaultFovRadius = 150.0f;
    inline constexpr float kMinimumFovRadius = 5.0f;
    inline constexpr float kMaximumFovRadius = 800.0f;
    inline constexpr float kMinimumSmoothing = 1.0f;
    inline constexpr float kMaximumSmoothing = 50.0f;
    inline constexpr float kMouseYawDegrees = 0.022f;
    inline constexpr int kMaximumMouseStep = 127;
    inline constexpr float kTargetLockRadiusScale = 1.12f;
    inline constexpr float kMaximumPredictionSeconds = 0.10f;
    inline constexpr uint64_t kMaximumPlayerCoreAgeUs = 150000u;
    inline constexpr uint64_t kMaximumPlayerBoneAgeUs = 150000u;
    inline constexpr uint64_t kMaximumTargetViewAgeUs = 35000u;
    inline constexpr uint64_t kMaximumTriggerCrosshairAgeUs = 35000u;
    inline constexpr uint64_t kMaximumTriggerViewSkewUs = 20000u;
    inline constexpr uint64_t kMaximumTriggerTargetAgeUs = 35000u;
    inline constexpr uint64_t kMaximumTriggerTargetSkewUs = 25000u;
    inline constexpr uint64_t kMaximumRecoilSampleAgeUs = 35000u;
    inline constexpr uint64_t kMaximumRecoilViewSkewUs = 20000u;
    inline constexpr uint64_t kMaximumWeaponStateAgeUs = 50000u;
    inline constexpr uint64_t kMaximumWeaponStateSkewUs = 25000u;
    inline constexpr uint64_t kMaximumDamageStateAgeUs = 150000u;
    inline constexpr float kMaximumAimPredictionSeconds = 0.10f;
    inline constexpr float kMaximumTriggerPredictionSeconds = 0.075f;
    inline constexpr float kHeadAimLowerBlend = 0.15f;
    inline constexpr float kMaximumHeadAimCorrection = 1.5f;
    inline constexpr uint8_t kRequiredPostShotAliveSamples = 2u;
    inline constexpr uint8_t kRequiredPostShotRecoilSamples = 2u;
    // A successful device ACK without an ammo/shots/lastShot transition is
    // ambiguous: the game may have fired while one telemetry publication was
    // lost. Blindly retrying the same continuous target is therefore less safe
    // than requiring a crosshair/key cycle. This is intentionally zero.
    inline constexpr uint8_t kMaximumUnobservedClickRetries = 0u;

    enum class TriggerTargetOutcome : uint8_t
    {
        Pending,
        DeadOrGone,
        AliveConfirmed,
    };

    struct FovCircle
    {
        float centerX = 0.0f;
        float centerY = 0.0f;
        float radius = 0.0f;
        bool valid = false;
    };

    struct MouseMove
    {
        int x = 0;
        int y = 0;
        float remainderX = 0.0f;
        float remainderY = 0.0f;
        bool valid = false;
    };

    struct ActivationResult
    {
        bool active = false;
        bool wasDown = false;
        bool toggled = false;
    };

    struct AimAngleDelta
    {
        float pitch = 0.0f;
        float yaw = 0.0f;
        bool valid = false;
    };

    struct ActivationState
    {
        int key = 0;
        int mode = 0;
        bool initialized = false;
        bool wasDown = false;
        bool toggled = false;
    };

    inline float SanitizeFovRadius(
        float radius,
        float fallback = kDefaultFovRadius) noexcept
    {
        if (!std::isfinite(radius) ||
            radius < kMinimumFovRadius ||
            radius > kMaximumFovRadius) {
            return fallback;
        }
        return radius;
    }

    inline float ResolveConfiguredFov(float globalRadius, bool perWeapon, float profileRadius) noexcept
    {
        return perWeapon ? SanitizeFovRadius(profileRadius, SanitizeFovRadius(globalRadius))
                         : SanitizeFovRadius(globalRadius);
    }

    inline FovCircle ResolveFovCircle(
        float viewportWidth,
        float viewportHeight,
        float requestedRadius) noexcept
    {
        if (!std::isfinite(viewportWidth) ||
            !std::isfinite(viewportHeight) ||
            viewportWidth <= 4.0f ||
            viewportHeight <= 4.0f) {
            return {};
        }

        const float viewportLimit =
            std::max(1.0f, std::min(viewportWidth, viewportHeight) * 0.5f - 2.0f);
        const float radius = std::min(
            SanitizeFovRadius(requestedRadius),
            viewportLimit);
        return {
            viewportWidth * 0.5f,
            viewportHeight * 0.5f,
            radius,
            radius > 0.0f
        };
    }

    inline float SanitizeSmoothing(float value, float fallback = 5.0f) noexcept
    {
        if (!std::isfinite(value) ||
            value < kMinimumSmoothing ||
            value > kMaximumSmoothing) {
            return fallback;
        }
        return value;
    }

    inline int SanitizeAimBone(int value, int fallback = 0) noexcept
    {
        return value >= 0 && value <= 4 ? value : fallback;
    }

    inline int SanitizeDelayMs(int value, int fallback = 10) noexcept
    {
        return value >= 0 && value <= 500 ? value : fallback;
    }

    inline int SanitizeActivationMode(int value, int fallback = 0) noexcept
    {
        return value == 0 || value == 1 ? value : fallback;
    }

    inline ActivationResult ResolveActivationState(
        bool down,
        int mode,
        bool wasDown,
        bool toggled) noexcept
    {
        if (SanitizeActivationMode(mode) == 0)
            return {down, down, false};
        if (down && !wasDown)
            toggled = !toggled;
        return {toggled, down, toggled};
    }

    // User intent outlives a target lock or telemetry packet. Unknown input
    // is not a release. In a menu, allow turning OFF but never turning ON.
    inline bool UpdateActivation(ActivationState& state, bool enabled,
        int key, int mode, bool down, bool available, bool paused) noexcept
    {
        mode = SanitizeActivationMode(mode);
        const bool rebound = state.initialized && (state.key != key || state.mode != mode);
        if (!state.initialized || rebound)
            state = {key, mode, true, rebound && available && down, false};
        if (!enabled || key < 1 || key > 0xFE) {
            state.toggled = false;
            if (available) state.wasDown = down;
            return false;
        }
        if (!available) return false;
        if (paused || rebound) {
            if (paused && !rebound && mode == 1 && down && !state.wasDown)
                state.toggled = false;
            state.wasDown = down;
            return !paused && mode == 0 && down;
        }
        const auto result = ResolveActivationState(down, mode, state.wasDown, state.toggled);
        state.wasDown = result.wasDown;
        state.toggled = result.toggled;
        return result.active;
    }

    inline float TimeAdjustedSmoothing(float fraction, float elapsedSeconds) noexcept
    {
        if (!std::isfinite(fraction) || !std::isfinite(elapsedSeconds)) return 0.0f;
        fraction = std::clamp(fraction, 0.0f, 1.0f);
        const float ticks = std::clamp(elapsedSeconds, 0.0f, 0.05f) * 128.0f;
        if (ticks == 0.0f) return 0.0f;
        if (fraction == 1.0f) return 1.0f;
        return -std::expm1(std::log1p(-fraction) * ticks);
    }

    inline AimAngleDelta ResolveCompensatedAimDelta(
        float desiredPitch,
        float desiredYaw,
        float viewPitch,
        float viewYaw,
        float aimPunchPitch,
        float aimPunchYaw,
        float recoilPitchScale,
        float recoilYawScale,
        bool recoilActive) noexcept
    {
        if (!std::isfinite(desiredPitch) || !std::isfinite(desiredYaw) ||
            !std::isfinite(viewPitch) || !std::isfinite(viewYaw) ||
            (recoilActive && (!std::isfinite(aimPunchPitch) || !std::isfinite(aimPunchYaw) ||
             !std::isfinite(recoilPitchScale) || !std::isfinite(recoilYawScale)))) {
            return {};
        }
        const float compensatedPitch = viewPitch +
            (recoilActive ? aimPunchPitch * recoilPitchScale : 0.0f);
        const float compensatedYaw = viewYaw +
            (recoilActive ? aimPunchYaw * recoilYawScale : 0.0f);
        return {
            desiredPitch - compensatedPitch,
            std::remainder(desiredYaw - compensatedYaw, 360.0f),
            true,
        };
    }

    inline float ResolvePredictionSeconds(
        uint64_t snapshotAgeUs,
        int localPingMs,
        float outputLatencySeconds = 0.004f) noexcept
    {
        const float snapshotAgeSeconds = std::clamp(
            static_cast<float>(snapshotAgeUs) * 0.000001f,
            0.0f,
            0.05f);
        const float oneWayLatencySeconds =
            std::clamp(localPingMs, 0, 200) * 0.0005f;
        const float safeOutputLatency = std::clamp(
            std::isfinite(outputLatencySeconds) ? outputLatencySeconds : 0.0f,
            0.0f,
            0.02f);
        return std::clamp(
            snapshotAgeSeconds + oneWayLatencySeconds + safeOutputLatency,
            0.0f,
            kMaximumPredictionSeconds);
    }

    inline float ResolvePostShotOutcomeDelaySeconds(
        int localPingMs,
        float interpolationSeconds,
        float intervalPerTick,
        float outputLatencySeconds = 0.012f) noexcept
    {
        const float roundTripSeconds =
            std::clamp(localPingMs, 0, 250) * 0.001f;
        const float safeInterpolation = std::clamp(
            std::isfinite(interpolationSeconds)
                ? interpolationSeconds
                : 0.0f,
            0.0f,
            0.1f);
        const float safeInterval = std::clamp(
            std::isfinite(intervalPerTick) && intervalPerTick > 0.0f
                ? intervalPerTick
                : (1.0f / 64.0f),
            0.001f,
            0.1f);
        const float safeOutputLatency = std::clamp(
            std::isfinite(outputLatencySeconds)
                ? outputLatencySeconds
                : 0.012f,
            0.002f,
            0.035f);
        // A complete outcome is command -> server -> replicated pawn state.
        // Two extra simulation ticks prevent an immediately published stale
        // alive sample from authorizing another shot.
        return std::clamp(
            roundTripSeconds + safeInterpolation * 2.0f +
                safeInterval * 2.0f + safeOutputLatency,
            0.075f,
            0.30f);
    }

    inline float ResolveUnobservedClickTimeoutSeconds(
        float outputLatencySeconds,
        float holdSeconds = 0.018f) noexcept
    {
        const float safeOutputLatency = std::clamp(
            std::isfinite(outputLatencySeconds)
                ? outputLatencySeconds
                : 0.012f,
            0.002f,
            0.035f);
        const float safeHold = std::clamp(
            std::isfinite(holdSeconds) ? holdSeconds : 0.018f,
            0.008f,
            0.080f);
        // By this time DOWN/UP has completed and several independent ammo,
        // shot-counter and last-shot-time samples must have been published.
        return std::clamp(
            safeOutputLatency + safeHold + 0.055f,
            0.090f,
            0.18f);
    }

    inline float UpdateCorrelatedNoise(
        float previous,
        float innovation,
        float correlation = 0.82f,
        float absoluteLimit = 0.15f) noexcept
    {
        if (!std::isfinite(previous) || !std::isfinite(innovation))
            return 0.0f;
        const float safeCorrelation = std::clamp(correlation, 0.0f, 0.98f);
        const float innovationScale = std::sqrt(
            std::max(0.0f, 1.0f - safeCorrelation * safeCorrelation));
        const float value =
            previous * safeCorrelation + innovation * innovationScale;
        return std::clamp(value, -std::fabs(absoluteLimit), std::fabs(absoluteLimit));
    }

    inline float ResolveNoiseCorrelation(
        float tickSeconds,
        float timeConstantSeconds = 0.075f) noexcept
    {
        if (!std::isfinite(tickSeconds) || tickSeconds <= 0.0f ||
            !std::isfinite(timeConstantSeconds) || timeConstantSeconds <= 0.0f) {
            return 0.0f;
        }
        return std::clamp(
            std::exp(-tickSeconds / timeConstantSeconds),
            0.0f,
            0.98f);
    }

    inline bool ShouldKeepLockedTarget(
        float lockedDistance,
        float selectionRadius) noexcept
    {
        return std::isfinite(lockedDistance) &&
            std::isfinite(selectionRadius) &&
            selectionRadius > 0.0f &&
            lockedDistance <= selectionRadius * kTargetLockRadiusScale;
    }

    inline constexpr bool NeedsBoneData(
        bool targetEnabled,
        bool aimbotEnabled,
        bool triggerbotEnabled = false,
        bool triggerAutoShot = false) noexcept
    {
        (void)triggerAutoShot;
        return targetEnabled && (aimbotEnabled || triggerbotEnabled);
    }

    inline constexpr bool NeedsVisibilityData(
        bool targetEnabled,
        bool aimbotEnabled,
        bool aimVisibleOnly,
        bool triggerbotEnabled,
        bool triggerVisibleOnly,
        bool triggerAutoShot = false) noexcept
    {
        (void)triggerVisibleOnly;
        (void)triggerAutoShot;
        return targetEnabled &&
            ((aimbotEnabled && aimVisibleOnly) || triggerbotEnabled);
    }

    inline constexpr bool IsFreshSample(
        uint64_t updatedAtUs,
        uint64_t sampledAtUs,
        uint64_t maximumAgeUs) noexcept
    {
        return updatedAtUs > 0u && sampledAtUs >= updatedAtUs &&
            sampledAtUs - updatedAtUs <= maximumAgeUs;
    }

    inline MouseMove ResolveMouseMove(
        float pitchDelta,
        float yawDelta,
        float sensitivity,
        float fovSensitivityAdjust,
        float smoothing,
        float remainderX,
        float remainderY) noexcept
    {
        if (!std::isfinite(pitchDelta) || !std::isfinite(yawDelta) ||
            !std::isfinite(sensitivity) || sensitivity <= 0.01f ||
            !std::isfinite(fovSensitivityAdjust) ||
            fovSensitivityAdjust < 0.05f ||
            fovSensitivityAdjust > 2.0f ||
            !std::isfinite(remainderX) || !std::isfinite(remainderY)) {
            return {};
        }

        const float degreesPerCount =
            sensitivity * kMouseYawDegrees * fovSensitivityAdjust;
        if (!std::isfinite(degreesPerCount) || degreesPerCount <= 0.0f)
            return {};

        const float safeSmoothing = SanitizeSmoothing(smoothing);
        const float wantedX =
            -yawDelta / (degreesPerCount * safeSmoothing) + remainderX;
        const float wantedY =
            pitchDelta / (degreesPerCount * safeSmoothing) + remainderY;
        if (!std::isfinite(wantedX) || !std::isfinite(wantedY))
            return {};

        const int rawX = static_cast<int>(std::lround(wantedX));
        const int rawY = static_cast<int>(std::lround(wantedY));
        return {
            std::clamp(rawX, -kMaximumMouseStep, kMaximumMouseStep),
            std::clamp(rawY, -kMaximumMouseStep, kMaximumMouseStep),
            wantedX - static_cast<float>(rawX),
            wantedY - static_cast<float>(rawY),
            true
        };
    }

    inline float ResolveHeadAimCoordinate(
        float head,
        float lowerBone,
        float blend = kHeadAimLowerBlend,
        float maximumCorrection = kMaximumHeadAimCorrection) noexcept
    {
        if (!std::isfinite(head) || !std::isfinite(lowerBone))
            return std::isfinite(head) ? head : 0.0f;
        const float safeBlend = std::clamp(blend, 0.0f, 1.0f);
        const float safeMaximum = std::max(0.0f, std::fabs(maximumCorrection));
        const float correction = std::clamp(
            (lowerBone - head) * safeBlend,
            -safeMaximum,
            safeMaximum);
        return head + correction;
    }

    inline float ClampAimStepToTarget(float step, float targetDelta) noexcept
    {
        if (!std::isfinite(step) || !std::isfinite(targetDelta) ||
            std::fabs(targetDelta) <= 0.000001f) {
            return 0.0f;
        }
        if ((step > 0.0f) != (targetDelta > 0.0f))
            return 0.0f;
        return std::copysign(
            std::min(std::fabs(step), std::fabs(targetDelta)),
            targetDelta);
    }

    inline float ResolveAlignmentToleranceDegrees(
        float sensitivity,
        float fovSensitivityAdjust) noexcept
    {
        if (!std::isfinite(sensitivity) || sensitivity <= 0.01f)
            sensitivity = 1.0f;
        if (!std::isfinite(fovSensitivityAdjust) ||
            fovSensitivityAdjust < 0.05f ||
            fovSensitivityAdjust > 2.0f) {
            fovSensitivityAdjust = 1.0f;
        }
        return std::clamp(
            sensitivity * kMouseYawDegrees * fovSensitivityAdjust * 1.25f,
            0.02f,
            0.065f);
    }

    inline float ResolvePrecisionAlignmentToleranceDegrees(
        float sensitivity,
        float fovSensitivityAdjust,
        float targetDistance,
        float safePointRadius) noexcept
    {
        if (!std::isfinite(sensitivity) || sensitivity <= 0.01f)
            sensitivity = 1.0f;
        if (!std::isfinite(fovSensitivityAdjust) ||
            fovSensitivityAdjust < 0.05f ||
            fovSensitivityAdjust > 2.0f) {
            fovSensitivityAdjust = 1.0f;
        }
        if (!std::isfinite(targetDistance) || targetDistance <= 1.0f ||
            !std::isfinite(safePointRadius) || safePointRadius <= 0.0f) {
            return ResolveAlignmentToleranceDegrees(
                sensitivity,
                fovSensitivityAdjust);
        }

        constexpr float kRadiansToDegrees =
            57.2957795130823208768f;
        const float degreesPerCount =
            sensitivity * kMouseYawDegrees * fovSensitivityAdjust;
        // A nearest integer mouse position is at most half a count away.
        // 0.58 leaves a small transport/rounding margin without accepting
        // the previous 1.25-count error at long range.
        const float quantizationBudget = degreesPerCount * 0.58f;
        const float pointBudget =
            std::atan2(safePointRadius, targetDistance) *
            kRadiansToDegrees * 0.82f;
        return std::clamp(
            std::min({quantizationBudget, pointBudget, 0.032f}),
            0.006f,
            0.032f);
    }

    inline constexpr bool IsTimestampSkewAcceptable(
        uint64_t firstUs,
        uint64_t secondUs,
        uint64_t maximumSkewUs) noexcept
    {
        if (firstUs == 0u || secondUs == 0u)
            return false;
        const uint64_t skew = firstUs > secondUs
            ? firstUs - secondUs
            : secondUs - firstUs;
        return skew <= maximumSkewUs;
    }

    inline constexpr uint8_t AdvanceStableSamples(
        uint8_t currentSamples,
        uint64_t currentGeneration,
        uint64_t previousGeneration,
        bool stillAligned) noexcept
    {
        if (!stillAligned)
            return 0u;
        if (currentGeneration == 0u || currentGeneration == previousGeneration)
            return currentSamples;
        return currentSamples == UINT8_MAX
            ? UINT8_MAX
            : static_cast<uint8_t>(currentSamples + 1u);
    }

    inline bool IsDeterministicSeedWindowReady(
        int tested,
        int hits,
        bool earliestTickHit,
        float earliestGeometricSafety,
        float minimumGeometricSafety = 0.03f) noexcept
    {
        return tested == 3 && hits >= 2 && earliestTickHit &&
            std::isfinite(earliestGeometricSafety) &&
            std::isfinite(minimumGeometricSafety) &&
            minimumGeometricSafety >= 0.0f &&
            earliestGeometricSafety + 0.000001f >=
                minimumGeometricSafety;
    }

    inline uint32_t HashTargetName(
        const char* name,
        size_t maximumLength = 128u) noexcept
    {
        if (!name || maximumLength == 0u)
            return 0u;
        uint32_t hash = 2166136261u;
        size_t length = 0u;
        while (length < maximumLength && name[length] != '\0') {
            hash ^= static_cast<uint8_t>(name[length]);
            hash *= 16777619u;
            ++length;
        }
        return length == 0u ? 0u : hash;
    }

    inline constexpr bool IsSameTargetIdentity(
        uintptr_t expectedPawn,
        int expectedSlot,
        uint32_t expectedNameHash,
        uintptr_t observedPawn,
        int observedSlot,
        uint32_t observedNameHash,
        uint32_t expectedHandle = 0,
        uint32_t observedHandle = 0) noexcept
    {
        if (expectedPawn == 0u || observedPawn != expectedPawn ||
            expectedSlot < 0 || observedSlot != expectedSlot) {
            return false;
        }
        // A full handle includes the entity serial, unlike a reused pointer.
        if (expectedHandle != 0 && expectedHandle != UINT32_MAX &&
            observedHandle != 0 && observedHandle != UINT32_MAX)
            return expectedHandle == observedHandle;
        // Pawn + controller slot are the fallback identity. The name is
        // an additional reuse guard when both coherent snapshots contain it;
        // a transient empty name must not fabricate a target replacement.
        return expectedNameHash == 0u || observedNameHash == 0u ||
            expectedNameHash == observedNameHash;
    }

    inline constexpr bool IsAmmoConsumptionObserved(
        int ammoBefore,
        uint64_t updatedAtBeforeUs,
        int currentAmmo,
        bool currentValid,
        uint64_t currentUpdatedAtUs) noexcept
    {
        return ammoBefore >= 0 && currentValid && currentAmmo >= 0 &&
            currentUpdatedAtUs > updatedAtBeforeUs &&
            currentAmmo < ammoBefore;
    }

    inline constexpr bool IsShotsFiredAdvanceObserved(
        int shotsBefore,
        bool beforeValid,
        uint64_t updatedAtBeforeUs,
        int currentShots,
        bool currentValid,
        uint64_t currentUpdatedAtUs) noexcept
    {
        return beforeValid && currentValid && currentShots > shotsBefore &&
            currentUpdatedAtUs > updatedAtBeforeUs;
    }

    inline bool IsLastShotTimeAdvanceObserved(
        float lastShotTimeBefore,
        bool beforeValid,
        uint64_t updatedAtBeforeUs,
        float currentLastShotTime,
        bool currentValid,
        uint64_t currentUpdatedAtUs) noexcept
    {
        return beforeValid && currentValid &&
            std::isfinite(lastShotTimeBefore) &&
            std::isfinite(currentLastShotTime) &&
            currentUpdatedAtUs > updatedAtBeforeUs &&
            currentLastShotTime > lastShotTimeBefore + 0.0001f;
    }

    inline constexpr uint8_t AdvancePostShotCoreSamples(
        uint8_t currentSamples,
        uint64_t currentCoreUpdatedAtUs,
        uint64_t previousCoreUpdatedAtUs,
        uint64_t baselineCoreUpdatedAtUs,
        bool identityPresent,
        bool coreFresh,
        bool outcomeWindowElapsed) noexcept
    {
        if (!identityPresent || !coreFresh || !outcomeWindowElapsed)
            return 0u;
        if (currentCoreUpdatedAtUs == 0u ||
            currentCoreUpdatedAtUs <= baselineCoreUpdatedAtUs ||
            currentCoreUpdatedAtUs == previousCoreUpdatedAtUs) {
            return currentSamples;
        }
        return currentSamples == UINT8_MAX
            ? UINT8_MAX
            : static_cast<uint8_t>(currentSamples + 1u);
    }

    inline constexpr uint8_t AdvanceMissingTargetSamples(
        uint8_t currentSamples,
        uint64_t currentGeneration,
        uint64_t previousGeneration,
        bool targetMissing,
        bool outcomeWindowElapsed) noexcept
    {
        if (!targetMissing || !outcomeWindowElapsed)
            return 0u;
        if (currentGeneration == 0u || currentGeneration == previousGeneration)
            return currentSamples;
        return currentSamples == UINT8_MAX
            ? UINT8_MAX
            : static_cast<uint8_t>(currentSamples + 1u);
    }

    inline constexpr TriggerTargetOutcome ResolveTriggerTargetOutcome(
        bool outcomeWindowElapsed,
        bool identityPresent,
        bool identityReplaced,
        bool coreFresh,
        int health,
        uint8_t postShotCoreSamples,
        uint8_t missingSamples) noexcept
    {
        if (!outcomeWindowElapsed)
            return TriggerTargetOutcome::Pending;
        if (identityReplaced || missingSamples >= 2u)
            return TriggerTargetOutcome::DeadOrGone;
        if (!identityPresent || !coreFresh || postShotCoreSamples == 0u)
            return TriggerTargetOutcome::Pending;
        if (health <= 0)
            return TriggerTargetOutcome::DeadOrGone;
        return postShotCoreSamples >= kRequiredPostShotAliveSamples
            ? TriggerTargetOutcome::AliveConfirmed
            : TriggerTargetOutcome::Pending;
    }

    inline bool IsRecoilDeltaStable(
        float currentPitch,
        float currentYaw,
        float previousPitch,
        float previousYaw,
        float maximumDelta = 0.08f) noexcept
    {
        return std::isfinite(currentPitch) && std::isfinite(currentYaw) &&
            std::isfinite(previousPitch) && std::isfinite(previousYaw) &&
            std::isfinite(maximumDelta) && maximumDelta >= 0.0f &&
            std::fabs(currentPitch - previousPitch) <= maximumDelta &&
            std::fabs(currentYaw - previousYaw) <= maximumDelta;
    }

    inline constexpr bool IsPostShotRecoilReady(
        bool shotsSampleFresh,
        int shotsFired,
        bool aimPunchSampleFresh,
        uint8_t stableRecoilSamples) noexcept
    {
        if (!shotsSampleFresh)
            return false;
        if (shotsFired <= 0)
            return true;
        return aimPunchSampleFresh &&
            stableRecoilSamples >= kRequiredPostShotRecoilSamples;
    }

    inline constexpr bool ShouldRetryUnobservedClick(
        bool observationWindowElapsed,
        bool clickPulseActive,
        bool postClickWeaponEvidenceFresh,
        bool weaponReady,
        bool targetIdentityAlive,
        uint8_t retryCount) noexcept
    {
        return observationWindowElapsed && !clickPulseActive &&
            postClickWeaponEvidenceFresh && weaponReady &&
            targetIdentityAlive &&
            retryCount < kMaximumUnobservedClickRetries;
    }

    inline bool ShouldHoldCapsuleAim(
        bool rayInsideSelectedCapsule,
        float currentHitchancePercent,
        float requiredHitchancePercent,
        float marginPercent = 0.0f) noexcept
    {
        if (!rayInsideSelectedCapsule ||
            !std::isfinite(currentHitchancePercent) ||
            !std::isfinite(requiredHitchancePercent) ||
            !std::isfinite(marginPercent) ||
            requiredHitchancePercent <= 0.0f || marginPercent < 0.0f) {
            return false;
        }
        return currentHitchancePercent + marginPercent + 0.001f >=
            requiredHitchancePercent;
    }

    inline bool IsMarginalSeedWindowReady(
        float currentHitchancePercent,
        float requiredHitchancePercent,
        int testedSeeds,
        int hitSeeds,
        float marginPercent = 5.0f) noexcept
    {
        if (!std::isfinite(currentHitchancePercent) ||
            !std::isfinite(requiredHitchancePercent) ||
            !std::isfinite(marginPercent) ||
            requiredHitchancePercent <= 0.0f || marginPercent < 0.0f ||
            testedSeeds != 3 || hitSeeds < 2 || hitSeeds > testedSeeds) {
            return false;
        }
        return currentHitchancePercent + marginPercent + 0.001f >=
            requiredHitchancePercent;
    }

    inline bool IsBetterPlannedHitboxChoice(
        int requestedHitgroup,
        int candidateHitgroup,
        float candidateDamage,
        float candidatePointDistance,
        bool hasBest,
        int bestHitgroup,
        float bestDamage,
        float bestPointDistance) noexcept
    {
        if (candidateHitgroup <= 0 || !std::isfinite(candidateDamage) ||
            !std::isfinite(candidatePointDistance) || candidateDamage < 0.0f ||
            candidatePointDistance < 0.0f) {
            return false;
        }
        if (!hasBest)
            return true;
        if (bestHitgroup <= 0 || !std::isfinite(bestDamage) ||
            !std::isfinite(bestPointDistance)) {
            return true;
        }
        const int candidateTier = requestedHitgroup == 0 ||
            candidateHitgroup == requestedHitgroup
                ? 0
                : 1;
        const int bestTier = requestedHitgroup == 0 ||
            bestHitgroup == requestedHitgroup
                ? 0
                : 1;
        if (candidateTier != bestTier)
            return candidateTier < bestTier;
        if (requestedHitgroup == 0) {
            return candidatePointDistance + 0.001f < bestPointDistance ||
                (std::fabs(candidatePointDistance - bestPointDistance) <=
                     0.001f &&
                 candidateDamage > bestDamage);
        }
        return candidateDamage > bestDamage + 0.001f ||
            (std::fabs(candidateDamage - bestDamage) <= 0.001f &&
             candidatePointDistance < bestPointDistance);
    }

    inline constexpr bool IsTriggerFireGateOpen(
        bool requested,
        bool targetValid,
        bool crosshairMatches,
        bool pointAligned,
        bool samplesFresh,
        bool weaponEligible,
        bool shotLatched,
        bool clickOwned,
        bool cooldownReady) noexcept
    {
        return requested && targetValid && crosshairMatches && pointAligned &&
            samplesFresh && weaponEligible && !shotLatched && !clickOwned &&
            cooldownReady;
    }
}
