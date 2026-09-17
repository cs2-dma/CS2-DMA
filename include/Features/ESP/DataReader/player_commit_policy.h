#pragma once

#include <cstdint>

namespace esp::data
{
    inline constexpr uint64_t kPlayerCommitCoreStaleHoldUs = 4000000u;
    inline constexpr uint64_t kPlayerCommitCoreStaleHoldAfterResetUs = 5000000u;
    inline constexpr uint64_t kPlayerCommitCoreStaleHoldAfterBulkUs = 8000000u;
    inline constexpr uint64_t kPlayerCommitZeroPawnGraceUs = 500000u;
    inline constexpr uint64_t kPlayerCommitZeroPawnGraceAfterBulkUs = 2000000u;
    inline constexpr uint64_t kPlayerCommitDeathConfirmUs = 35000u;
    inline constexpr uint64_t kPlayerCommitRecentStructuralResetWindowUs = 4000000u;
    inline constexpr uint64_t kMapFingerprintRecentSceneResetUs = 3000000u;
    inline constexpr float kPlayerCommitRespawnTeleportDistance2D = 512.0f;

    inline constexpr bool ShouldExposePlayerRoster(
        bool hierarchyStable,
        bool recoveryActive) noexcept
    {
        return hierarchyStable || recoveryActive;
    }

    struct AliveCoreHoldInput {
        bool missingFreshCore = false;
        bool pawnChangedThisFrame = false;
        bool trackedPlayerValid = false;
        uintptr_t trackedPlayerPawn = 0;
        uintptr_t currentPawn = 0;
        uint64_t lastAliveCoreReadUs = 0;
        uint64_t nowUs = 0;
        uint64_t coreStaleHoldUs = 0;
    };

    inline bool IsRecentStructuralReset(uint64_t resetUs, uint64_t nowUs)
    {
        return resetUs > 0 &&
               nowUs > resetUs &&
               (nowUs - resetUs) <= kPlayerCommitRecentStructuralResetWindowUs;
    }

    inline bool IsRecentMapFingerprintSceneReset(uint64_t resetUs, uint64_t nowUs)
    {
        return resetUs > 0 &&
               nowUs >= resetUs &&
               (nowUs - resetUs) < kMapFingerprintRecentSceneResetUs;
    }

    inline uint64_t SelectZeroPawnGraceUs(bool inBulkRecovery)
    {
        return inBulkRecovery ? kPlayerCommitZeroPawnGraceAfterBulkUs
                              : kPlayerCommitZeroPawnGraceUs;
    }

    inline uint64_t SelectCoreStaleHoldUs(
        bool sceneSettling,
        bool recentStructuralReset,
        bool inBulkRecovery)
    {
        if (inBulkRecovery)
            return kPlayerCommitCoreStaleHoldAfterBulkUs;

        if (sceneSettling || recentStructuralReset)
            return kPlayerCommitCoreStaleHoldAfterResetUs;

        return kPlayerCommitCoreStaleHoldUs;
    }

    inline bool CanTemporarilyHoldAliveCore(const AliveCoreHoldInput& input)
    {
        return input.missingFreshCore &&
               !input.pawnChangedThisFrame &&
               input.trackedPlayerValid &&
               input.trackedPlayerPawn == input.currentPawn &&
               input.lastAliveCoreReadUs > 0 &&
               input.nowUs >= input.lastAliveCoreReadUs &&
               (input.nowUs - input.lastAliveCoreReadUs) <= input.coreStaleHoldUs;
    }

    // A complete, range-checked health/life-state sample is authoritative for
    // liveness even when another auxiliary core read failed or a dead pawn has
    // already cleared its world position. Missing/partial vital reads never
    // reach this predicate, so they retain the continuity behavior above.
    inline bool IsAuthoritativeDeadCoreSample(
        bool vitalSampleFresh,
        uintptr_t currentPawn,
        int health,
        uint8_t lifeState) noexcept
    {
        return vitalSampleFresh &&
               currentPawn != 0 &&
               health >= 0 &&
               health <= 500 &&
               lifeState <= 2 &&
               (health <= 0 || lifeState != 0);
    }

    inline bool IsDeathConfirmed(
        bool looksDeadThisFrame,
        uint8_t deathConfirmCount,
        uint64_t deadReadSinceUs,
        uint64_t nowUs)
    {
        return looksDeadThisFrame &&
               deathConfirmCount >= 2 &&
               deadReadSinceUs > 0 &&
               nowUs > deadReadSinceUs &&
               (nowUs - deadReadSinceUs) >= kPlayerCommitDeathConfirmUs;
    }

    inline uint64_t SelectCoreInvalidGraceUs(
        bool looksDeadThisFrame,
        bool pawnChangedThisFrame,
        uint64_t coreStaleHoldUs)
    {
        const uint64_t baseGraceUs = looksDeadThisFrame
            ? kPlayerCommitDeathConfirmUs
            : coreStaleHoldUs;
        return pawnChangedThisFrame && baseGraceUs > kPlayerCommitDeathConfirmUs
            ? kPlayerCommitDeathConfirmUs
            : baseGraceUs;
    }

    inline bool IsWithinGraceWindow(uint64_t startedUs, uint64_t nowUs, uint64_t graceUs)
    {
        return nowUs >= startedUs && (nowUs - startedUs) < graceUs;
    }
}
