#pragma once

#include <cstdint>
#include "app/Core/memory_address.h"

namespace esp::data
{
    // Missing bytes are not a null pointer. Retain an established root only
    // briefly, and never renew its age on a read gap or across a scene reset.
    struct BasePointerSample {
        uintptr_t value = 0;
        uint64_t sampledUs = 0;
        static constexpr uint64_t kReadGapHoldUs = 100000;

        uintptr_t Resolve(uintptr_t raw, size_t bytesRead, uint64_t nowUs,
                          bool allowReadGapHold = true) noexcept
        {
            if (app::memory_address::IsCompleteGamePointerSample(raw, bytesRead)) {
                value = raw;
                sampledUs = nowUs;
                return value;
            }
            return allowReadGapHold && sampledUs && nowUs >= sampledUs &&
                nowUs - sampledUs <= kReadGapHoldUs ? value : 0;
        }
    };

    constexpr bool IsLocalVitalSampleComplete(
        uintptr_t sampledPawn, uintptr_t resolvedPawn,
        int health, uint8_t lifeState,
        size_t healthBytes, size_t lifeStateBytes) noexcept
    {
        return resolvedPawn && sampledPawn == resolvedPawn &&
            healthBytes == sizeof(health) && lifeStateBytes == sizeof(lifeState) &&
            health >= 0 && health <= 500 && lifeState <= 2;
    }

    inline constexpr uint32_t kGameRulesSanityRepairStreak = 5u;
    inline constexpr uint64_t kGameRulesSanityRepairAgeUs = 1500000u;
    inline constexpr uint64_t kGameRulesSanityPersistentAgeUs = 8000000u;
    inline constexpr uint64_t kGameRulesSanityLogCooldownUs = 3000000u;
    inline constexpr uint64_t kGameRulesSanityRecoveryCooldownUs = 3000000u;

    inline constexpr uint64_t kBaseSceneImmediateSettlingUs = 2000000u;
    inline constexpr uint64_t kBaseSceneWarmupSettlingUs = 10000000u;
    inline constexpr uint64_t kColdAttachResetPreserveWindowUs = 2000000u;

    inline constexpr uint32_t kEntityShapeLiveConfirmStreak = 2u;
    inline constexpr uint64_t kEntityShapeLiveConfirmAgeUs = 250000u;
    inline constexpr uint64_t kEntityShapeTransitionCooldownUs = 3000000u;
    inline constexpr uint32_t kEntityListWarmupConfirmSamples = 2u;
    inline constexpr uint32_t kEntityListRuntimeConfirmSamples = 4u;
    inline constexpr uint64_t kEntityListWarmupConfirmAgeUs = 8000u;
    inline constexpr uint64_t kEntityListRuntimeConfirmAgeUs = 25000u;

    inline bool IsBasePolicyCooldownElapsed(uint64_t lastUs, uint64_t nowUs, uint64_t cooldownUs)
    {
        return lastUs == 0 ||
               nowUs <= lastUs ||
               (nowUs - lastUs) >= cooldownUs;
    }

    inline bool ShouldLogGameRulesSanityFailure(
        uint32_t failureStreak,
        uint64_t lastLogUs,
        uint64_t nowUs)
    {
        return failureStreak == 1u ||
               IsBasePolicyCooldownElapsed(lastLogUs, nowUs, kGameRulesSanityLogCooldownUs);
    }

    inline bool ShouldRefreshAfterGameRulesSanityFailure(
        uint32_t failureStreak,
        uint64_t failureSinceUs,
        uint64_t lastRecoveryUs,
        uint64_t nowUs)
    {
        return failureStreak >= kGameRulesSanityRepairStreak &&
               failureSinceUs > 0 &&
               nowUs >= failureSinceUs &&
               (nowUs - failureSinceUs) >= kGameRulesSanityRepairAgeUs &&
               IsBasePolicyCooldownElapsed(
                   lastRecoveryUs,
                   nowUs,
                   kGameRulesSanityRecoveryCooldownUs);
    }

    inline bool ShouldRequestPersistentGameRulesRecovery(
        uint64_t failureSinceUs,
        uint64_t nowUs)
    {
        return failureSinceUs > 0 &&
               nowUs >= failureSinceUs &&
               (nowUs - failureSinceUs) >= kGameRulesSanityPersistentAgeUs;
    }

    inline bool IsGameRulesCoreSane(
        bool readComplete,
        uint8_t bombPlanted,
        uint8_t bombDropped)
    {
        // Source 2 network booleans may share a packed byte and therefore be
        // observed as non-zero bit masks (for example 2), not only literal 1.
        // Callers already normalize them with `!= 0`; completeness is the
        // validity signal here, not the numeric representation of true.
        (void)bombPlanted;
        (void)bombDropped;
        return readComplete;
    }

    inline bool IsBaseSceneSettling(uint64_t sceneAgeUs, bool warmupStableOrRecovery)
    {
        return sceneAgeUs < kBaseSceneImmediateSettlingUs ||
               (sceneAgeUs < kBaseSceneWarmupSettlingUs && !warmupStableOrRecovery);
    }

    inline bool ShouldPreserveRecentColdAttachReset(
        bool wasWaitingForProcess,
        bool coldAttachWarmup,
        uint64_t lastSceneResetUs,
        uint64_t nowUs)
    {
        return wasWaitingForProcess &&
               coldAttachWarmup &&
               lastSceneResetUs > 0 &&
               nowUs > lastSceneResetUs &&
               (nowUs - lastSceneResetUs) < kColdAttachResetPreserveWindowUs;
    }

    inline bool ShouldResolveMatchmakingBase(uintptr_t cachedBase)
    {
        return cachedBase == 0;
    }

    inline bool ShouldAcceptEntityListCandidate(
        bool stableInGame,
        uint32_t confirmationCount,
        uint64_t firstSeenUs,
        uint64_t nowUs)
    {
        const uint32_t requiredSamples =
            stableInGame
                ? kEntityListRuntimeConfirmSamples
                : kEntityListWarmupConfirmSamples;
        const uint64_t requiredAgeUs =
            stableInGame
                ? kEntityListRuntimeConfirmAgeUs
                : kEntityListWarmupConfirmAgeUs;
        return confirmationCount >= requiredSamples &&
               firstSeenUs > 0 &&
               nowUs >= firstSeenUs &&
               (nowUs - firstSeenUs) >= requiredAgeUs;
    }

    inline bool IsEntityShapeLive(
        bool definitelyMenuByEngine,
        bool hasClientBase,
        bool hasEngineBase,
        bool hasEntityList,
        bool hasListEntry,
        int playerSlotScanLimit,
        int highestEntityIndex)
    {
        return !definitelyMenuByEngine &&
               hasClientBase &&
               hasEngineBase &&
               hasEntityList &&
               hasListEntry &&
               playerSlotScanLimit >= 32 &&
               highestEntityIndex >= 64;
    }

    inline bool ShouldTransitionOnEntityShapeLive(
        uint32_t liveStreak,
        uint64_t liveSinceUs,
        bool entityShapeLiveConfirmed,
        uint64_t lastTransitionUs,
        uint64_t nowUs)
    {
        return liveStreak >= kEntityShapeLiveConfirmStreak &&
               liveSinceUs > 0 &&
               nowUs >= liveSinceUs &&
               (nowUs - liveSinceUs) >= kEntityShapeLiveConfirmAgeUs &&
               !entityShapeLiveConfirmed &&
               IsBasePolicyCooldownElapsed(
                   lastTransitionUs,
                   nowUs,
                   kEntityShapeTransitionCooldownUs);
    }

    inline bool IsConfirmedLiveEngineState(
        bool resolved,
        bool inGame,
        bool menu,
        int32_t signOnState,
        int32_t maxClients)
    {
        return resolved &&
               inGame &&
               !menu &&
               signOnState == 6 &&
               maxClients >= 2;
    }

}
