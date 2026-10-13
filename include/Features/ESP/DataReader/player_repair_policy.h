#pragma once

#include <cstdint>

namespace esp::data
{
    enum class CoreRecoveryAction : uint8_t { None, Probe, Repair };

    struct CoreRecoveryState {
        uintptr_t pawn = 0;
        uint32_t handle = 0;
        uint64_t sinceUs = 0;
        uint64_t lastSampleUs = 0;
        uint32_t failures = 0;
        CoreRecoveryAction stage = CoreRecoveryAction::None;
    };

    inline CoreRecoveryAction ObserveCoreRecovery(
        CoreRecoveryState& state,
        uintptr_t pawn,
        uint32_t handle,
        bool coreValid,
        bool liveScene,
        uint64_t nowUs)
    {
        if (!pawn || coreValid || state.pawn != pawn || state.handle != handle ||
            nowUs < state.lastSampleUs) {
            state = {};
            state.pawn = pawn;
            state.handle = handle;
        }
        if (!pawn || coreValid)
            return CoreRecoveryAction::None;
        if (state.failures == 0)
            state.sinceUs = nowUs;
        if (state.failures == 0 || nowUs != state.lastSampleUs) {
            if (state.failures < 0xFFFFFFFFu)
                ++state.failures;
        }
        state.lastSampleUs = nowUs;
        if (!liveScene || state.failures < 8u)
            return CoreRecoveryAction::None;
        const uint64_t ageUs = nowUs - state.sinceUs;
        if (ageUs >= 2500000u && state.stage != CoreRecoveryAction::Repair) {
            state.stage = CoreRecoveryAction::Repair;
            return CoreRecoveryAction::Repair;
        }
        if (ageUs >= 750000u && state.stage == CoreRecoveryAction::None) {
            state.stage = CoreRecoveryAction::Probe;
            return CoreRecoveryAction::Probe;
        }
        return CoreRecoveryAction::None;
    }

    inline bool IsScenePositionRepairCoherent(
        bool readsComplete,
        uintptr_t sceneNode,
        uintptr_t confirmedSceneNode,
        uint32_t expectedPawnHandle,
        uint32_t currentPawnHandle,
        bool controllerBacklinkMatches,
        bool positionValid,
        int health,
        uint8_t lifeState)
    {
        return readsComplete && sceneNode != 0 && sceneNode == confirmedSceneNode &&
               expectedPawnHandle != 0 && expectedPawnHandle != 0xFFFFFFFFu &&
               expectedPawnHandle == currentPawnHandle && controllerBacklinkMatches &&
               health >= 0 && health <= 500 && lifeState <= 2 &&
               (health == 0 || lifeState != 0 || positionValid);
    }

    inline constexpr uint64_t kCoreRepairRetryAfterResetUs = 8000u;
    inline constexpr uint64_t kCoreRepairRetryNormalUs = 16000u;
    inline constexpr uint64_t kCoreRepairRetrySustainedUs = 50000u;
    inline constexpr uint64_t kCoreRepairRetryPersistentUs = 250000u;
    inline constexpr uint8_t kCoreRepairSustainedStreak = 16u;
    inline constexpr uint8_t kCoreRepairPersistentStreak = 60u;
    inline constexpr uint8_t kCoreRepairThresholdAfterReset = 1u;
    inline constexpr uint8_t kCoreRepairThresholdNormal = 2u;
    inline constexpr int kCoreRepairMissingToleranceAfterReset = 0;
    inline constexpr int kCoreRepairMissingToleranceNormal = 1;
    inline constexpr uint32_t kPartialCoreThresholdAfterReset = 24u;
    inline constexpr uint32_t kPartialCoreThresholdNormal = 60u;
    inline constexpr uint32_t kPartialCoreConfirmStreak = 12u;
    inline constexpr uint64_t kPartialCoreConfirmAgeAfterResetUs = 550000u;
    inline constexpr uint64_t kPartialCoreConfirmAgeNormalUs = 1200000u;

    struct CoreRepairPolicy {
        uint64_t retryIntervalUs = kCoreRepairRetryNormalUs;
        uint8_t repairThreshold = kCoreRepairThresholdNormal;
        int missingTolerance = kCoreRepairMissingToleranceNormal;
        uint32_t partialCoreThreshold = kPartialCoreThresholdNormal;
    };

    inline CoreRepairPolicy SelectCoreRepairPolicy(bool sceneSettling, bool recentStructuralReset)
    {
        return {
            recentStructuralReset ? kCoreRepairRetryAfterResetUs : kCoreRepairRetryNormalUs,
            (sceneSettling || recentStructuralReset) ? kCoreRepairThresholdAfterReset
                                                     : kCoreRepairThresholdNormal,
            recentStructuralReset ? kCoreRepairMissingToleranceAfterReset
                                  : kCoreRepairMissingToleranceNormal,
            recentStructuralReset ? kPartialCoreThresholdAfterReset
                                  : kPartialCoreThresholdNormal,
        };
    }

    inline bool IsCoreRepairAttemptDue(
        uint64_t lastAttemptUs,
        uint64_t nowUs,
        uint64_t retryIntervalUs)
    {
        return lastAttemptUs == 0 ||
               nowUs <= lastAttemptUs ||
               (nowUs - lastAttemptUs) >= retryIntervalUs;
    }

    inline uint64_t SelectCoreRepairRetryIntervalUs(
        uint64_t initialRetryUs,
        uint8_t failureStreak)
    {
        if (failureStreak >= kCoreRepairPersistentStreak)
            return kCoreRepairRetryPersistentUs;
        if (failureStreak >= kCoreRepairSustainedStreak)
            return kCoreRepairRetrySustainedUs;
        return initialRetryUs;
    }

    inline bool IsCorePartial(int pawnCount, int saneCoreCount, int missingTolerance)
    {
        return pawnCount >= 2 &&
               saneCoreCount + missingTolerance < pawnCount;
    }

    inline bool ArePartialCoreBadSlotsRepeated(uint64_t previousBadMask, uint64_t currentBadMask)
    {
        return previousBadMask != 0 &&
               currentBadMask != 0 &&
               ((currentBadMask & previousBadMask) != 0);
    }

    inline uint64_t SelectPartialCoreConfirmAgeUs(bool recentStructuralReset)
    {
        return recentStructuralReset ? kPartialCoreConfirmAgeAfterResetUs
                                     : kPartialCoreConfirmAgeNormalUs;
    }

    inline bool IsPartialCoreConfirmed(
        bool sceneSettling,
        bool repeatedBadSlots,
        uint64_t partialCoreBadMask,
        uint32_t partialCoreConfirmedStreak,
        uint64_t partialAgeUs,
        bool recentStructuralReset)
    {
        return !sceneSettling &&
               repeatedBadSlots &&
               partialCoreBadMask != 0 &&
               partialCoreConfirmedStreak >= kPartialCoreConfirmStreak &&
               partialAgeUs >= SelectPartialCoreConfirmAgeUs(recentStructuralReset);
    }

    inline bool ShouldReportPartialCoreIncident(
        bool sceneSettling,
        uint32_t partialCoreStreak,
        uint32_t partialCoreThreshold,
        bool confirmedPartial)
    {
        return !sceneSettling &&
               partialCoreStreak >= partialCoreThreshold &&
               confirmedPartial;
    }
}
