#pragma once

#include "Features/ESP/DataReader/player_slot_policy.h"

#include <algorithm>
#include <cstdint>

namespace esp::data
{
    enum class EntityHierarchyRecoveryAction : uint8_t {
        None = 0,
        Probe,
        Repair,
        Full,
        ForcedFull,
    };

    inline constexpr uint64_t kHierarchyStableMissingHoldUs = 3000000u;
    inline constexpr uint64_t kHierarchyResetMissingHoldUs = 4500000u;
    inline constexpr uint64_t kHierarchyBulkMissingHoldUs = 6000000u;
    inline constexpr uint64_t kHierarchyWarmupMinimumAgeUs = 20000u;
    inline constexpr uint64_t kHierarchyWarmupExactPopulationUs = 150000u;
    inline constexpr uint64_t kHierarchyWarmupRelaxedPopulationUs = 500000u;
    inline constexpr uint64_t kHierarchyWarmupLocalIdentityGraceUs = 500000u;
    inline constexpr int kHierarchyWarmupRosterFloor = 10;

    inline constexpr uint16_t kHierarchyStableMissingThreshold = 400u;
    inline constexpr uint16_t kHierarchyResetMissingThreshold = 600u;
    inline constexpr uint16_t kHierarchyBulkMissingThreshold = 800u;

    inline constexpr int kHierarchyBulkEvictionThreshold = 2;
    inline constexpr uint64_t kHierarchyBulkRecoveryWindowMaxUs = 12000000u;
    inline constexpr uint32_t kEntityHierarchyMissingProbeStreak = 3u;
    inline constexpr uint32_t kEntityHierarchyMissingRepairStreak = 8u;
    inline constexpr uint32_t kEntityHierarchyMissingFullStreak = 16u;
    inline constexpr uint32_t kEntityHierarchyMissingForcedFullStreak = 24u;
    inline constexpr uint32_t kEntityHierarchyFlatProbeStreak = 3u;
    inline constexpr uint32_t kEntityHierarchyFlatRepairStreak = 8u;
    inline constexpr uint32_t kEntityHierarchyFlatFullStreak = 18u;
    inline constexpr uint64_t kEntityHierarchyMissingProbeAgeUs = 50000u;
    inline constexpr uint64_t kEntityHierarchyMissingRepairAgeUs = 250000u;
    inline constexpr uint64_t kEntityHierarchyMissingFullAgeUs = 2000000u;
    inline constexpr uint64_t kEntityHierarchyMissingForcedFullAgeUs = 6000000u;
    inline constexpr uint64_t kEntityHierarchyMissingRepairCooldownUs = 100000u;
    inline constexpr uint64_t kEntityHierarchyMissingFullCooldownUs = 1000000u;
    inline constexpr uint64_t kEntityHierarchyMissingForcedFullCooldownUs = 3000000u;
    inline constexpr uint64_t kEntityHierarchyFlatSceneAgeUs = 1500000u;
    inline constexpr uint64_t kEntityHierarchyFlatProbeAgeUs = 150000u;
    inline constexpr uint64_t kEntityHierarchyFlatRepairAgeUs = 750000u;
    inline constexpr uint64_t kEntityHierarchyFlatFullAgeUs = 3000000u;
    inline constexpr uint64_t kEntityHierarchyFlatRefreshCooldownUs = 100000u;
    inline constexpr uint64_t kZeroControllerProbeAgeUs = 250000u;
    inline constexpr uint64_t kZeroControllerRepairAgeUs = 1500000u;
    inline constexpr uint64_t kZeroControllerFullAgeUs = 5000000u;
    inline constexpr int kHierarchyPlayerSlotCapacity = 64;
    inline constexpr int kHierarchySteadyDiscoveryBudget = 12;

    struct HierarchyMissingPolicy {
        uint64_t holdUs = kHierarchyStableMissingHoldUs;
        uint16_t threshold = kHierarchyStableMissingThreshold;
    };

    struct ControllerWindowProbe {
        bool read = false;
        bool localMatched = false;
        int validPointers = 0;
    };

    enum class ControllerBacklinkState : uint8_t {
        Unknown = 0,
        Match,
        Mismatch,
    };

    struct ResolvedPlayerIdentityCandidate {
        int slot = -1;
        uintptr_t controller = 0;
        uintptr_t pawn = 0;
        uint32_t pawnHandle = 0;
        uint32_t pawnControllerHandle = 0;
        bool localIdentity = false;
        bool committedIdentity = false;
    };

    struct CommittedPlayerIdentityCandidate {
        int slot = -1;
        uintptr_t controller = 0;
        uintptr_t pawn = 0;
        bool freshCore = false;
        bool currentHierarchy = false;
    };

    inline bool IsHierarchyEntityHandleValid(uint32_t handle, uint32_t entityHandleMask)
    {
        const uint32_t entityIndex = handle & entityHandleMask;
        return handle != 0u &&
               handle != 0xFFFFFFFFu &&
               entityIndex != 0u &&
               entityIndex != entityHandleMask;
    }

    inline bool DoesEntityHandleReferenceIndex(
        uint32_t handle,
        uint32_t entityIndex,
        uint32_t entityHandleMask)
    {
        const uint32_t maskedIndex = entityIndex & entityHandleMask;
        return IsHierarchyEntityHandleValid(handle, entityHandleMask) &&
               entityIndex != 0u &&
               entityIndex != 0xFFFFFFFFu &&
               maskedIndex != 0u &&
               maskedIndex != entityHandleMask &&
               (handle & entityHandleMask) == maskedIndex;
    }

    inline constexpr int SelectHierarchyDiscoveryBudget(
        bool hierarchyWarmupActive,
        bool forceFullDiscovery)
    {
        return hierarchyWarmupActive || forceFullDiscovery
            ? kHierarchyPlayerSlotCapacity
            : kHierarchySteadyDiscoveryBudget;
    }

    inline constexpr bool ShouldRetryMissingControllerSlot(
        bool bulkWindowRead,
        bool controllerCacheWarmed,
        bool forceFullDiscovery,
        bool hadPreviousController)
    {
        if (hadPreviousController)
            return true;
        return !bulkWindowRead &&
               (!controllerCacheWarmed || forceFullDiscovery);
    }

    inline constexpr bool ShouldProbeFallbackEntityStride(
        bool entityStrideValidated)
    {
        return !entityStrideValidated;
    }

    inline ControllerBacklinkState EvaluateControllerBacklink(
        int zeroBasedControllerSlot,
        uint32_t pawnControllerHandle,
        uint32_t entityHandleMask)
    {
        if (zeroBasedControllerSlot < 0 ||
            !IsHierarchyEntityHandleValid(pawnControllerHandle, entityHandleMask)) {
            return ControllerBacklinkState::Unknown;
        }

        const uint32_t expectedControllerIndex =
            static_cast<uint32_t>(zeroBasedControllerSlot + 1);
        return (pawnControllerHandle & entityHandleMask) == expectedControllerIndex
            ? ControllerBacklinkState::Match
            : ControllerBacklinkState::Mismatch;
    }

    inline bool IsSameResolvedPawnIdentity(
        const ResolvedPlayerIdentityCandidate& lhs,
        const ResolvedPlayerIdentityCandidate& rhs,
        uint32_t entityHandleMask,
        bool backlinksTrusted = false)
    {
        if (lhs.controller != 0 &&
            rhs.controller != 0 &&
            lhs.controller == rhs.controller) {
            return true;
        }
        if (lhs.pawn != 0 &&
            rhs.pawn != 0 &&
            lhs.pawn == rhs.pawn) {
            return true;
        }
        if (backlinksTrusted &&
            IsHierarchyEntityHandleValid(
                lhs.pawnControllerHandle,
                entityHandleMask) &&
            IsHierarchyEntityHandleValid(
                rhs.pawnControllerHandle,
                entityHandleMask) &&
            lhs.pawnControllerHandle == rhs.pawnControllerHandle) {
            return true;
        }
        if (!IsHierarchyEntityHandleValid(lhs.pawnHandle, entityHandleMask) ||
            !IsHierarchyEntityHandleValid(rhs.pawnHandle, entityHandleMask)) {
            return false;
        }
        return lhs.pawnHandle == rhs.pawnHandle;
    }

    inline bool PreferSecondResolvedPlayerIdentity(
        const ResolvedPlayerIdentityCandidate& first,
        const ResolvedPlayerIdentityCandidate& second,
        uint32_t entityHandleMask,
        bool backlinksTrusted)
    {
        auto score = [&](const ResolvedPlayerIdentityCandidate& candidate) {
            int value = 0;
            if (candidate.localIdentity)
                value += 64;
            const ControllerBacklinkState backlink = EvaluateControllerBacklink(
                candidate.slot,
                candidate.pawnControllerHandle,
                entityHandleMask);
            if (backlink == ControllerBacklinkState::Match)
                value += 32;
            else if (backlinksTrusted &&
                     backlink == ControllerBacklinkState::Mismatch)
                value -= 16;
            if (candidate.committedIdentity)
                value += 4;
            return value;
        };

        const int firstScore = score(first);
        const int secondScore = score(second);
        if (firstScore != secondScore)
            return secondScore > firstScore;
        return second.slot >= 0 &&
               (first.slot < 0 || second.slot < first.slot);
    }

    inline bool IsSameCommittedPlayerIdentity(
        const CommittedPlayerIdentityCandidate& lhs,
        const CommittedPlayerIdentityCandidate& rhs)
    {
        return (lhs.controller != 0 &&
                rhs.controller != 0 &&
                lhs.controller == rhs.controller) ||
               (lhs.pawn != 0 &&
                rhs.pawn != 0 &&
                lhs.pawn == rhs.pawn);
    }

    inline bool PreferSecondCommittedPlayerIdentity(
        const CommittedPlayerIdentityCandidate& first,
        const CommittedPlayerIdentityCandidate& second)
    {
        auto score = [](const CommittedPlayerIdentityCandidate& candidate) {
            int value = 0;
            if (candidate.freshCore)
                value += 4;
            if (candidate.currentHierarchy)
                value += 2;
            return value;
        };

        const int firstScore = score(first);
        const int secondScore = score(second);
        if (firstScore != secondScore)
            return secondScore > firstScore;
        return second.slot >= 0 &&
               (first.slot < 0 || second.slot < first.slot);
    }

    inline bool ShouldProbeAlternateControllerWindow(
        bool strideValidated,
        const ControllerWindowProbe& preferred,
        bool localControllerKnown)
    {
        if (localControllerKnown && preferred.localMatched)
            return false;
        if (!preferred.read)
            return true;
        if (localControllerKnown)
            return !preferred.localMatched;
        if (!strideValidated)
            return true;
        return preferred.validPointers <= 0;
    }

    inline int SelectControllerWindowProbe(
        const ControllerWindowProbe& preferred,
        const ControllerWindowProbe& alternate)
    {
        if (preferred.localMatched != alternate.localMatched)
            return preferred.localMatched ? 0 : 1;
        if (preferred.validPointers <= 0 && alternate.validPointers <= 0)
            return -1;
        if (alternate.validPointers > preferred.validPointers)
            return 1;
        return preferred.validPointers > 0 ? 0 : 1;
    }

    inline HierarchyMissingPolicy SelectHierarchyMissingPolicy(
        bool recentStructuralReset,
        bool sceneWarmupStable,
        bool inBulkRecovery)
    {
        if (inBulkRecovery) {
            return {
                kHierarchyBulkMissingHoldUs,
                kHierarchyBulkMissingThreshold,
            };
        }

        if (recentStructuralReset || !sceneWarmupStable) {
            return {
                kHierarchyResetMissingHoldUs,
                kHierarchyResetMissingThreshold,
            };
        }

        return {
            kHierarchyStableMissingHoldUs,
            kHierarchyStableMissingThreshold,
        };
    }

    inline bool IsHierarchyWarmupSatisfied(
        uint64_t sceneAgeUs,
        int controllerCount,
        int pawnHandleCount,
        int pawnCount,
        bool localControllerReady,
        bool localPawnReady)
    {
        if (sceneAgeUs < kHierarchyWarmupMinimumAgeUs ||
            controllerCount <= 0) {
            return false;
        }

        // Local controller/pawn globals can lag behind an otherwise coherent
        // entity hierarchy during sign-on and after a DMA cache refresh. Keep
        // the strict identity gate briefly, then allow the independently
        // validated roster to publish instead of freezing every ESP feature.
        if ((!localControllerReady || !localPawnReady) &&
            sceneAgeUs < kHierarchyWarmupLocalIdentityGraceUs) {
            return false;
        }

        // A scene often exposes only one or two controllers before the first
        // entity page is fully populated. Keep the 6 ms discovery cadence for
        // that short interval, then allow genuinely small matches to settle.
        if (sceneAgeUs < kHierarchyWarmupExactPopulationUs &&
            controllerCount < kHierarchyWarmupRosterFloor) {
            return false;
        }

        int missingAllowance = 0;
        if (sceneAgeUs >= kHierarchyWarmupRelaxedPopulationUs)
            missingAllowance = (std::max)(1, controllerCount / 4);
        else if (sceneAgeUs >= kHierarchyWarmupExactPopulationUs)
            missingAllowance = 1;

        const int requiredPopulation =
            (std::max)(1, controllerCount - missingAllowance);
        return pawnHandleCount >= requiredPopulation &&
               pawnCount >= requiredPopulation;
    }

    inline bool IsHierarchyHoldAgeAllowed(uint64_t lastPresentUs, uint64_t nowUs, uint64_t holdUs)
    {
        return lastPresentUs == 0 ||
               nowUs <= lastPresentUs ||
               (nowUs - lastPresentUs) <= holdUs;
    }

    inline uint16_t ProjectHierarchyMissingStreak(uint16_t currentStreak)
    {
        return currentStreak < 0xFFFFu
            ? static_cast<uint16_t>(currentStreak + 1u)
            : currentStreak;
    }

    inline bool ShouldEvictMissingHierarchy(
        bool controllerCompatible,
        bool holdAgeAllowed,
        uint16_t missingStreak,
        uint16_t missingThreshold)
    {
        return !controllerCompatible ||
               !holdAgeAllowed ||
               missingStreak >= missingThreshold;
    }

    inline bool IsBulkPawnOnlyHierarchyLoss(
        int pawnOnlyLossCount,
        int controllerLossCount,
        int threshold = kHierarchyBulkEvictionThreshold)
    {
        return pawnOnlyLossCount >= threshold &&
               pawnOnlyLossCount > controllerLossCount;
    }

    inline bool IsHierarchyBulkEvictionDetected(
        int wouldEvictCount,
        int pawnOnlyLossCount,
        int controllerLossCount,
        int threshold = kHierarchyBulkEvictionThreshold)
    {
        return wouldEvictCount >= threshold &&
               IsBulkPawnOnlyHierarchyLoss(pawnOnlyLossCount, controllerLossCount, threshold);
    }

    inline bool IsHierarchyBulkRecoveryExtensionCandidate(
        bool inBulkRecovery,
        int invalidLiveSlotsCount,
        int pawnOnlyLossCount,
        int controllerLossCount,
        int threshold = kHierarchyBulkEvictionThreshold)
    {
        return inBulkRecovery &&
               invalidLiveSlotsCount >= threshold &&
               pawnOnlyLossCount > controllerLossCount;
    }

    inline bool ShouldExtendHierarchyBulkRecoveryWindow(
        uint64_t recoveryAgeUs,
        uint64_t maxRecoveryAgeUs = kHierarchyBulkRecoveryWindowMaxUs)
    {
        return recoveryAgeUs < maxRecoveryAgeUs;
    }

    inline bool ShouldHoldMissingHierarchy(bool wantsEvict, bool bulkEvictionDetected)
    {
        return !wantsEvict || bulkEvictionDetected;
    }

    inline bool IsEntityHierarchyRecoveryCooldownElapsed(
        uint64_t lastRecoveryUs,
        uint64_t nowUs,
        uint64_t cooldownUs)
    {
        return lastRecoveryUs == 0 ||
               nowUs <= lastRecoveryUs ||
               (nowUs - lastRecoveryUs) >= cooldownUs;
    }

    inline bool IsEntityHierarchyObservationAgeElapsed(
        uint64_t firstObservedUs,
        uint64_t nowUs,
        uint64_t minimumAgeUs)
    {
        return firstObservedUs > 0 &&
               nowUs >= firstObservedUs &&
               (nowUs - firstObservedUs) >= minimumAgeUs;
    }

    inline EntityHierarchyRecoveryAction SelectMissingEntityHierarchyRecovery(
        uint32_t missingStreak,
        uint64_t missingSinceUs,
        EntityHierarchyRecoveryAction completedStage,
        uint64_t lastRecoveryUs,
        uint64_t nowUs)
    {
        if (completedStage < EntityHierarchyRecoveryAction::ForcedFull &&
            missingStreak >= kEntityHierarchyMissingForcedFullStreak &&
            IsEntityHierarchyObservationAgeElapsed(
                missingSinceUs,
                nowUs,
                kEntityHierarchyMissingForcedFullAgeUs) &&
            IsEntityHierarchyRecoveryCooldownElapsed(
                lastRecoveryUs,
                nowUs,
                kEntityHierarchyMissingForcedFullCooldownUs)) {
            return EntityHierarchyRecoveryAction::ForcedFull;
        }

        if (completedStage < EntityHierarchyRecoveryAction::Full &&
            missingStreak >= kEntityHierarchyMissingFullStreak &&
            IsEntityHierarchyObservationAgeElapsed(
                missingSinceUs,
                nowUs,
                kEntityHierarchyMissingFullAgeUs) &&
            IsEntityHierarchyRecoveryCooldownElapsed(
                lastRecoveryUs,
                nowUs,
                kEntityHierarchyMissingFullCooldownUs)) {
            return EntityHierarchyRecoveryAction::Full;
        }

        if (completedStage < EntityHierarchyRecoveryAction::Repair &&
            missingStreak >= kEntityHierarchyMissingRepairStreak &&
            IsEntityHierarchyObservationAgeElapsed(
                missingSinceUs,
                nowUs,
                kEntityHierarchyMissingRepairAgeUs) &&
            IsEntityHierarchyRecoveryCooldownElapsed(
                lastRecoveryUs,
                nowUs,
                kEntityHierarchyMissingRepairCooldownUs)) {
            return EntityHierarchyRecoveryAction::Repair;
        }

        if (completedStage < EntityHierarchyRecoveryAction::Probe &&
            missingStreak >= kEntityHierarchyMissingProbeStreak &&
            IsEntityHierarchyObservationAgeElapsed(
                missingSinceUs,
                nowUs,
                kEntityHierarchyMissingProbeAgeUs)) {
            return EntityHierarchyRecoveryAction::Probe;
        }

        return EntityHierarchyRecoveryAction::None;
    }

    inline bool IsFlatLiveEntityHierarchy(
        bool liveEntityHierarchyContext,
        uint64_t sceneAgeUs,
        int playerSlotScanLimit,
        int activePlayerCount,
        int highestEntityIndex)
    {
        return liveEntityHierarchyContext &&
               sceneAgeUs >= kEntityHierarchyFlatSceneAgeUs &&
               playerSlotScanLimit >= 32 &&
               activePlayerCount == 0 &&
               highestEntityIndex > 0 &&
               highestEntityIndex < 32;
    }

    inline EntityHierarchyRecoveryAction SelectFlatEntityHierarchyRecovery(
        uint32_t flatStreak,
        uint64_t flatSinceUs,
        EntityHierarchyRecoveryAction completedStage,
        uint64_t lastRecoveryUs,
        uint64_t nowUs)
    {
        if (completedStage < EntityHierarchyRecoveryAction::Full &&
            flatStreak >= kEntityHierarchyFlatFullStreak &&
            IsEntityHierarchyObservationAgeElapsed(
                flatSinceUs,
                nowUs,
                kEntityHierarchyFlatFullAgeUs) &&
            IsEntityHierarchyRecoveryCooldownElapsed(
                lastRecoveryUs,
                nowUs,
                kEntityHierarchyFlatRefreshCooldownUs)) {
            return EntityHierarchyRecoveryAction::Full;
        }
        if (completedStage < EntityHierarchyRecoveryAction::Repair &&
            flatStreak >= kEntityHierarchyFlatRepairStreak &&
            IsEntityHierarchyObservationAgeElapsed(
                flatSinceUs,
                nowUs,
                kEntityHierarchyFlatRepairAgeUs) &&
            IsEntityHierarchyRecoveryCooldownElapsed(
                lastRecoveryUs,
                nowUs,
                kEntityHierarchyFlatRefreshCooldownUs)) {
            return EntityHierarchyRecoveryAction::Repair;
        }
        if (completedStage < EntityHierarchyRecoveryAction::Probe &&
            flatStreak >= kEntityHierarchyFlatProbeStreak &&
            IsEntityHierarchyObservationAgeElapsed(
                flatSinceUs,
                nowUs,
                kEntityHierarchyFlatProbeAgeUs)) {
            return EntityHierarchyRecoveryAction::Probe;
        }

        return EntityHierarchyRecoveryAction::None;
    }

    inline EntityHierarchyRecoveryAction SelectZeroControllerRecovery(
        uint64_t zeroSinceUs,
        EntityHierarchyRecoveryAction completedStage,
        uint64_t nowUs)
    {
        if (completedStage < EntityHierarchyRecoveryAction::Full &&
            IsEntityHierarchyObservationAgeElapsed(
                zeroSinceUs,
                nowUs,
                kZeroControllerFullAgeUs)) {
            return EntityHierarchyRecoveryAction::Full;
        }
        if (completedStage < EntityHierarchyRecoveryAction::Repair &&
            IsEntityHierarchyObservationAgeElapsed(
                zeroSinceUs,
                nowUs,
                kZeroControllerRepairAgeUs)) {
            return EntityHierarchyRecoveryAction::Repair;
        }
        if (completedStage < EntityHierarchyRecoveryAction::Probe &&
            IsEntityHierarchyObservationAgeElapsed(
                zeroSinceUs,
                nowUs,
                kZeroControllerProbeAgeUs)) {
            return EntityHierarchyRecoveryAction::Probe;
        }
        return EntityHierarchyRecoveryAction::None;
    }
}
