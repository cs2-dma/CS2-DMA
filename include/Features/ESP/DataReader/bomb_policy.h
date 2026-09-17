#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace esp::data
{
    // C_CSGameRules::m_nRoundStartCount is a uint8, not an int32.
    using BombRoundStartCounter = uint8_t;
    static_assert(sizeof(BombRoundStartCounter) == 1);
    inline constexpr uint32_t kBombCachedPositionSources =
        (1u << 6) | (1u << 7) | (1u << 9);
    inline constexpr int kBombCarrierTeamT = 2;
    inline constexpr int kDroppedC4AcceptScore = 80;
    inline constexpr int kDroppedC4ConfirmedScore = 120;
    inline constexpr uint64_t kResolvedBombCarrierFreshUs = 750000u;
    inline constexpr uint64_t kResolvedBombCarrierExpireUs = 2500000u;
    inline constexpr uint64_t kCachedBombCarryEvidenceUs = 300000u;
    inline constexpr uint64_t kFreshInventoryC4CarrierUs = 125000u;
    inline constexpr uint64_t kCachedBombCarryOwnerExpireUs = 800000u;
    inline constexpr uint64_t kBombOwnerAttachGraceUs = 200000u;
    inline constexpr uint64_t kWeaponC4ProbeUrgentCooldownUs = 30000u;
    inline constexpr uint64_t kWeaponC4ProbeCooldownUs = 150000u;
    inline constexpr uint64_t kWeakDroppedC4StickyUs = 220000u;
    inline constexpr uint64_t kRulesWorldC4StickyUs = 1200000u;
    inline constexpr uint64_t kRulesConfirmedDroppedStickyUs = 1200000u;
    inline constexpr uint64_t kWeakConfirmedDroppedStickyUs = 300000u;
    inline constexpr uint64_t kPlantedC4PositionFallbackUs = 2500000u;
    inline constexpr uint64_t kStalePlantedC4TaintUs = 8000000u;
    inline constexpr uint64_t kWeaponC4EntityHoldUs = 250000u;
    inline constexpr uint64_t kWeaponC4DynamicDroppedRefreshUs = 10000u;
    inline constexpr uint64_t kWeaponC4DynamicUnknownRefreshUs = 30000u;
    inline constexpr uint64_t kWeaponC4DynamicCarriedRefreshUs = 25000u;
    inline constexpr uint64_t kWeaponC4DropTickEdgeHoldUs = 150000u;
    inline constexpr uint64_t kPlantedC4MetaRefreshUs = 40000u;
    inline constexpr uint64_t kPlantedC4DefuseMetaRefreshUs = 10000u;
    inline constexpr uint64_t kPlantedC4MetaFreshUs = 125000u;
    inline constexpr uint64_t kBombTimerSignalGapHoldUs = 250000u;
    inline constexpr uint64_t kDefuseSignalGapHoldUs = 80000u;
    inline constexpr uint64_t kBombHeavyReadMaxDeferralUs = 8000u;
    inline constexpr uint64_t kUrgentBombHeavyReadMaxDeferralUs = 4000u;
    inline constexpr uint64_t kWeaponC4PositionFreshUs = 125000u;
    inline constexpr int kWeaponC4CandidateConfirmSamples = 3;
    inline constexpr uint64_t kWeaponC4CandidateSampleMaxGapUs = 100000u;
    inline constexpr float kWeaponC4CandidateSampleMaxDelta = 96.0f;
    inline constexpr uint64_t kBombRulesExitConfirmUs = 120000u;
    inline constexpr uint8_t kBombRuleFalseConfirmSamples = 3u;
    inline constexpr uint64_t kBombInventoryDropEdgeSettleUs = 75000u;
    inline constexpr uint64_t kPlantedC4RootGapHoldUs = 250000u;

    inline bool ShouldHoldPlantedC4Entity(
        bool rulesPlanted, uintptr_t currentRoot, uintptr_t cachedRoot,
        uintptr_t cachedEntity, bool terminal,
        uint64_t rootSeenUs, uint64_t nowUs)
    {
        return rulesPlanted && !terminal && cachedRoot != 0 && cachedEntity != 0 &&
            (currentRoot == 0 || currentRoot == cachedRoot) &&
            rootSeenUs > 0 && nowUs >= rootSeenUs &&
            nowUs - rootSeenUs <= kPlantedC4RootGapHoldUs;
    }

    inline bool IsBombFieldReadComplete(
        bool requested,
        size_t bytesRead,
        size_t expectedBytes)
    {
        return !requested || bytesRead == expectedBytes;
    }

    inline bool IsValidWeaponPickableSample(
        bool requested,
        size_t bytesRead,
        uint8_t value)
    {
        return requested &&
               bytesRead == sizeof(value) &&
               value <= 1u;
    }

    inline bool IsWeaponDropTickAdvance(
        bool sameEntity,
        bool previousSampleKnown,
        uint32_t previousDropTick,
        uint32_t currentDropTick)
    {
        if (!sameEntity || !previousSampleKnown || currentDropTick == 0u)
            return false;
        if (previousDropTick == 0u)
            return true;
        return static_cast<int32_t>(
                   currentDropTick - previousDropTick) > 0;
    }

    inline bool IsAuthoritativeDetachedWeaponC4(
        bool positionFresh,
        bool pickableKnown,
        bool canBePickedUp,
        bool recentDropTickEdge)
    {
        if (!positionFresh)
            return false;
        if (recentDropTickEdge)
            return true;
        if (pickableKnown)
            return canBePickedUp;
        return false;
    }

    inline bool ShouldFreshCarryOverrideDetachedWeaponC4(
        bool detached,
        bool recentDropTickEdge,
        bool bombDroppedByRules,
        bool ownerAlive,
        bool ownerSelectedC4,
        bool freshInventoryC4,
        bool recentOwnerAttach,
        bool ownerAttachedByHandle)
    {
        if (!detached || recentDropTickEdge)
            return false;

        // A live owner transition is newer than a stale pickable sample. An
        // inventory-only sample is not: it may remain cached briefly after a
        // drop and must not contradict the rules' dropped state.
        if (ownerAlive && (ownerSelectedC4 || recentOwnerAttach))
            return true;
        if (freshInventoryC4)
            return true;
        if (bombDroppedByRules)
            return false;
        return ownerAlive && ownerAttachedByHandle;
    }

    inline bool IsBombInventorySamplePastDropEdge(
        bool bombDroppedByRules,
        uint64_t droppedRulesRiseUs,
        uint64_t sampleUs)
    {
        if (!bombDroppedByRules)
            return true;
        return droppedRulesRiseUs > 0 &&
               sampleUs >= droppedRulesRiseUs &&
               (sampleUs - droppedRulesRiseUs) >=
                   kBombInventoryDropEdgeSettleUs;
    }

    inline bool IsFreshInventoryC4CarrierEvidence(
        int carrierSlot,
        uint64_t sampleUs,
        uint64_t nowUs)
    {
        return carrierSlot >= 0 &&
               sampleUs > 0 &&
               nowUs >= sampleUs &&
               (nowUs - sampleUs) <= kFreshInventoryC4CarrierUs;
    }

    inline bool IsWeaponC4PositionSampleCurrent(
        bool entityMatches,
        bool positionValid,
        uint64_t metadataUs,
        uint64_t positionUs,
        uint64_t nowUs)
    {
        return entityMatches &&
               positionValid &&
               metadataUs > 0 &&
               positionUs >= metadataUs &&
               nowUs >= positionUs &&
               (nowUs - positionUs) <= kWeaponC4PositionFreshUs;
    }

    inline bool IsWeaponC4OwnerAttachTransition(
        bool sameEntity,
        bool previousSampleKnown,
        bool previousOwnerValid,
        bool currentOwnerValid,
        bool ownerChanged,
        bool detachedOwnerObserved,
        bool dropTickAdvanced)
    {
        return sameEntity &&
               currentOwnerValid &&
               !dropTickAdvanced &&
               (detachedOwnerObserved ||
                (previousSampleKnown && !previousOwnerValid) ||
                (previousOwnerValid && ownerChanged));
    }

    struct PlantedC4MetadataSample
    {
        uint8_t ticking = 0;
        uint8_t beingDefused = 0;
        uint8_t hasExploded = 0;
        uint8_t bombDefused = 0;
        uint8_t activated = 0;
        float blowTime = 0.0f;
        float timerLength = 0.0f;
        float defuseEndTime = 0.0f;
        float defuseLength = 0.0f;
    };

    inline bool IsPlausiblePlantedC4Metadata(
        const PlantedC4MetadataSample& sample)
    {
        const bool booleansValid =
            sample.ticking <= 1u &&
            sample.beingDefused <= 1u &&
            sample.hasExploded <= 1u &&
            sample.bombDefused <= 1u &&
            sample.activated <= 1u;
        const bool absoluteTimesValid =
            std::isfinite(sample.blowTime) &&
            sample.blowTime >= 0.0f &&
            sample.blowTime <= 100120.0f &&
            std::isfinite(sample.defuseEndTime) &&
            sample.defuseEndTime >= 0.0f &&
            sample.defuseEndTime <= 100120.0f;
        const bool durationsValid =
            std::isfinite(sample.timerLength) &&
            sample.timerLength >= 0.0f &&
            sample.timerLength <= 120.0f &&
            std::isfinite(sample.defuseLength) &&
            sample.defuseLength >= 0.0f &&
            sample.defuseLength <= 15.0f;
        return booleansValid && absoluteTimesValid && durationsValid;
    }

    struct StableBombRuleSignal
    {
        bool value = false;
        uint8_t falseStreak = 0;
    };

    inline StableBombRuleSignal SelectStableBombRuleSignal(
        bool currentSample,
        bool previousValue,
        uint8_t previousFalseStreak)
    {
        if (currentSample)
            return { true, 0 };
        if (!previousValue)
            return { false, 0 };

        const uint8_t nextFalseStreak =
            previousFalseStreak < 0xFFu
                ? static_cast<uint8_t>(previousFalseStreak + 1u)
                : previousFalseStreak;
        if (nextFalseStreak >= kBombRuleFalseConfirmSamples)
            return { false, 0 };
        return { true, nextFalseStreak };
    }

    inline bool IsBombRulesExitConfirmed(
        bool plantedCycleArmed,
        uint64_t falseSinceUs,
        uint64_t nowUs)
    {
        return plantedCycleArmed &&
               falseSinceUs > 0 &&
               nowUs >= falseSinceUs &&
               (nowUs - falseSinceUs) >= kBombRulesExitConfirmUs;
    }

    inline bool IsFreshLiveBombCycleMetadata(
        bool ticking,
        bool terminal,
        float blowTime,
        float currentGameTime)
    {
        return ticking &&
               !terminal &&
               std::isfinite(blowTime) &&
               std::isfinite(currentGameTime) &&
               blowTime > currentGameTime + 0.05f &&
               blowTime < currentGameTime + 120.0f;
    }

    inline bool ShouldAdvanceBombTerminalEpoch(
        bool liveMetadataSeenForCycle,
        bool terminalHandledForCycle,
        bool terminalOrElapsed)
    {
        return liveMetadataSeenForCycle &&
               !terminalHandledForCycle &&
               terminalOrElapsed;
    }

    struct C4EntityKey
    {
        uintptr_t entityPtr = 0;
        // Diagnostic only: direct/world/inventory lookups have no vector index,
        // and CUtlVector compaction can change the index of the SAME entity.
        uint32_t listOrdinal = 0;
        uintptr_t sceneNode = 0;
        uint64_t dropGeneration = 0;
        uint32_t dropTick = 0;

        bool IsValid() const
        {
            return entityPtr != 0 &&
                   sceneNode != 0 &&
                   dropGeneration != 0;
        }

        bool Matches(const C4EntityKey& o) const
        {
            return entityPtr == o.entityPtr &&
                   sceneNode == o.sceneNode &&
                   dropGeneration == o.dropGeneration &&
                   dropTick == o.dropTick;
        }
    };

    struct BombRoundCounterTracker
    {
        static constexpr uint32_t kConfirmSamples = 2;
        static constexpr uint32_t kMaximumPlausibleValue = 10000;

        bool initialized = false;
        uint32_t committed = 0;
        uint32_t candidate = 0;
        uint32_t candidateSamples = 0;

        void Reset() noexcept
        {
            initialized = false;
            committed = 0;
            candidate = 0;
            candidateSamples = 0;
        }

        void DiscardPending() noexcept
        {
            candidate = 0;
            candidateSamples = 0;
        }

        bool Observe(uint32_t value) noexcept
        {
            if (value > kMaximumPlausibleValue) {
                DiscardPending();
                return false;
            }

            if (initialized && value == committed) {
                DiscardPending();
                return false;
            }

            if (candidate != value) {
                candidate = value;
                candidateSamples = 1;
                return false;
            }

            if (candidateSamples < kConfirmSamples)
                ++candidateSamples;
            if (candidateSamples < kConfirmSamples)
                return false;

            const bool changed = initialized && committed != candidate;
            committed = candidate;
            initialized = true;
            DiscardPending();
            return changed;
        }
    };

    inline bool IsDistinctContinuousC4Sample(
        uint64_t previousSampleUs,
        uint64_t currentSampleUs,
        float distanceSquared)
    {
        return previousSampleUs > 0 &&
               currentSampleUs > previousSampleUs &&
               (currentSampleUs - previousSampleUs) <=
                   kWeaponC4CandidateSampleMaxGapUs &&
               std::isfinite(distanceSquared) &&
               distanceSquared <=
                   kWeaponC4CandidateSampleMaxDelta *
                       kWeaponC4CandidateSampleMaxDelta;
    }

    struct C4PositionConfirmation
    {
        C4EntityKey key = {};
        int samples = 0;
        uint64_t sampleUs = 0;
        float x = 0.0f, y = 0.0f, z = 0.0f;
        bool keyChanged = false;

        void Reset() noexcept { *this = {}; }

        bool Observe(const C4EntityKey& current, bool eligible,
                     uint64_t currentSampleUs, uint64_t nowUs,
                     float px, float py, float pz) noexcept
        {
            keyChanged = key.IsValid() &&
                ((current.entityPtr != 0 && current.entityPtr != key.entityPtr) ||
                 (current.dropGeneration != 0 && current.dropGeneration != key.dropGeneration) ||
                 (current.IsValid() && !key.Matches(current)));
            if (keyChanged) {
                samples = 0;
                sampleUs = 0;
                key = {};
            }
            if (current.IsValid())
                key = current;

            // Scheduling gaps are not new samples and do not erase a short
            // confirmation sequence. Nor may they renew its deadline.
            if (sampleUs > 0 && (nowUs < sampleUs ||
                nowUs - sampleUs > kWeaponC4CandidateSampleMaxGapUs)) {
                samples = 0;
                sampleUs = 0;
            }
            if (!eligible || !current.IsValid() || currentSampleUs == 0 ||
                nowUs < currentSampleUs ||
                nowUs - currentSampleUs > kWeaponC4CandidateSampleMaxGapUs ||
                !std::isfinite(px) || !std::isfinite(py) || !std::isfinite(pz))
                return false;
            if (currentSampleUs < sampleUs)
                return false;
            if (currentSampleUs > sampleUs) {
                const float dx = px - x, dy = py - y, dz = pz - z;
                samples = IsDistinctContinuousC4Sample(
                    sampleUs, currentSampleUs, dx * dx + dy * dy + dz * dz)
                    ? (std::min)(samples + 1, kWeaponC4CandidateConfirmSamples) : 1;
                sampleUs = currentSampleUs;
                x = px; y = py; z = pz;
            }
            return samples >= kWeaponC4CandidateConfirmSamples;
        }
    };

    enum class DroppedC4PublicationStatus : uint8_t {
        Inactive, NoCandidate, InvalidIdentity, StaleSample,
        Confirming, SourceRejected, Current, Cache
    };

    inline const char* DroppedC4PublicationStatusName(DroppedC4PublicationStatus status) noexcept
    {
        switch (status) {
        case DroppedC4PublicationStatus::Inactive: return "inactive";
        case DroppedC4PublicationStatus::NoCandidate: return "no_candidate";
        case DroppedC4PublicationStatus::InvalidIdentity: return "invalid_identity";
        case DroppedC4PublicationStatus::StaleSample: return "stale_sample";
        case DroppedC4PublicationStatus::Confirming: return "confirming";
        case DroppedC4PublicationStatus::SourceRejected: return "source_rejected";
        case DroppedC4PublicationStatus::Current: return "current";
        case DroppedC4PublicationStatus::Cache: return "cache";
        default: return "unknown";
        }
    }

    inline bool IsUnconfirmedDroppedC4PublicationAllowed(
        uint32_t sourceFlags,
        int score,
        bool matchedFreshSamples,
        bool worldScanConfirmed)
    {
        constexpr uint32_t kRulesSource = 1u << 0;
        constexpr uint32_t kWeaponSource = 1u << 1;
        constexpr uint32_t kWorldSource = 1u << 2;
        if ((sourceFlags & kBombCachedPositionSources) != 0u)
            return false; // Cached evidence cannot confirm itself as a new sample.
        if (worldScanConfirmed || (sourceFlags & kWorldSource) != 0u)
            return true;
        const bool rulesWeaponCandidate =
            (sourceFlags & (kRulesSource | kWeaponSource)) ==
            (kRulesSource | kWeaponSource);
        if (matchedFreshSamples &&
            ((rulesWeaponCandidate && score >= kDroppedC4AcceptScore) ||
             score >= kDroppedC4ConfirmedScore)) {
            return true;
        }
        return false;
    }

    inline uint8_t SelectPublishedDroppedC4Confidence(
        uint8_t candidateConfidence,
        bool matchedFreshSamples,
        bool worldScanConfirmed)
    {
        if (!matchedFreshSamples && !worldScanConfirmed)
            return candidateConfidence;
        return (std::max)(
            candidateConfidence,
            static_cast<uint8_t>(kDroppedC4ConfirmedScore));
    }

    enum class DroppedC4PositionSource : uint8_t { None, Current, Cache };

    inline bool IsBombPositionSampleFresh(uint64_t sampleUs, uint64_t nowUs,
                                         uint64_t maximumAgeUs) noexcept
    {
        return sampleUs > 0 && nowUs >= sampleUs && nowUs - sampleUs <= maximumAgeUs;
    }

    inline DroppedC4PositionSource SelectDroppedC4PositionSource(
        bool dropped, bool candidateValid, bool candidateAllowed,
        bool cacheContextValid, bool cachePositionValid,
        uint64_t cacheSampleUs, uint64_t nowUs, uint64_t cacheHoldUs) noexcept
    {
        if (!dropped)
            return DroppedC4PositionSource::None;
        if (candidateValid && candidateAllowed)
            return DroppedC4PositionSource::Current;
        if (cacheContextValid && cachePositionValid &&
            IsBombPositionSampleFresh(cacheSampleUs, nowUs, cacheHoldUs))
            return DroppedC4PositionSource::Cache;
        return DroppedC4PositionSource::None;
    }

    inline bool IsBombPositionModeCompatible(
        bool planted, bool dropped, bool cachedPlanted, bool cachedDropped,
        bool allowUnresolved = false) noexcept
    {
        if ((planted && dropped) || cachedPlanted == cachedDropped)
            return false;
        if (!planted && !dropped)
            return allowUnresolved;
        return planted == cachedPlanted && dropped == cachedDropped;
    }

    inline bool ShouldInvalidateChangedDropCandidateCache(
        bool candidateKeyChanged,
        bool previousStateDropped)
    {
        return candidateKeyChanged &&
               previousStateDropped;
    }

    struct StableGameTimeSelection
    {
        float value = 0.0f;
        bool acceptedRaw = false;
        size_t candidateIndex = static_cast<size_t>(-1);
    };

    inline StableGameTimeSelection SelectStableGameTimeCandidate(
        const float* candidates,
        size_t candidateCount,
        float lastStableValue,
        uint64_t lastStableUs,
        uint64_t nowUs,
        size_t preferredCandidateIndex = static_cast<size_t>(-1))
    {
        auto isPlausible = [](float value) {
            return std::isfinite(value) && value >= 1.0f && value <= 100000.0f;
        };

        if (!candidates || candidateCount == 0)
            return { lastStableValue, false, static_cast<size_t>(-1) };

        if (!isPlausible(lastStableValue)) {
            for (size_t i = 0; i < candidateCount; ++i) {
                if (isPlausible(candidates[i]))
                    return { candidates[i], true, i };
            }
            return {};
        }

        const float elapsedSec =
            lastStableUs > 0 && nowUs >= lastStableUs
                ? std::clamp(
                      static_cast<float>(nowUs - lastStableUs) / 1000000.0f,
                      0.0f,
                      30.0f)
                : 0.0f;
        const float expectedValue = lastStableValue + elapsedSec;
        const float minValue = lastStableValue - 0.05f;
        const float maxValue =
            lastStableValue + std::max(0.25f, elapsedSec + 0.25f);

        size_t bestIndex = static_cast<size_t>(-1);
        float bestDistance = (std::numeric_limits<float>::max)();
        for (size_t i = 0; i < candidateCount; ++i) {
            const float value = candidates[i];
            if (!isPlausible(value) || value < minValue || value > maxValue)
                continue;
            float distance = std::fabs(value - expectedValue);
            if (i == preferredCandidateIndex)
                distance = std::max(0.0f, distance - 0.01f);
            if (distance < bestDistance) {
                bestDistance = distance;
                bestIndex = i;
            }
        }

        if (bestIndex != static_cast<size_t>(-1))
            return { candidates[bestIndex], true, bestIndex };

        return {
            lastStableValue + std::min(elapsedSec, 0.25f),
            false,
            static_cast<size_t>(-1)
        };
    }

    inline bool IsBombCooldownElapsed(uint64_t lastUs, uint64_t nowUs, uint64_t cooldownUs)
    {
        return lastUs == 0 ||
               nowUs <= lastUs ||
               (nowUs - lastUs) >= cooldownUs;
    }

    inline bool IsResolvedBombCarrierFresh(int carrierSlot, uint64_t lastResolvedUs, uint64_t nowUs)
    {
        return carrierSlot >= 0 &&
               lastResolvedUs > 0 &&
               nowUs >= lastResolvedUs &&
               (nowUs - lastResolvedUs) <= kResolvedBombCarrierFreshUs;
    }

    inline bool ShouldExpireResolvedBombCarrier(uint64_t lastResolvedUs, uint64_t nowUs)
    {
        return lastResolvedUs > 0 &&
               nowUs >= lastResolvedUs &&
               (nowUs - lastResolvedUs) > kResolvedBombCarrierExpireUs;
    }

    inline bool IsCachedBombCarryEvidenceFresh(int carrierSlot, uint64_t cachedUs, uint64_t nowUs)
    {
        return carrierSlot >= 0 &&
               cachedUs > 0 &&
               nowUs >= cachedUs &&
               (nowUs - cachedUs) <= kCachedBombCarryEvidenceUs;
    }

    inline bool ShouldExpireCachedBombCarryOwner(uint64_t cachedUs, uint64_t nowUs)
    {
        return cachedUs > 0 &&
               nowUs >= cachedUs &&
               (nowUs - cachedUs) > kCachedBombCarryOwnerExpireUs;
    }

    inline bool ShouldDeferWeaponC4Probe(
        bool bombPlantedByRules,
        bool bombDroppedByRules,
        bool cachedDroppedEntityResolved,
        bool inventoryC4CarrierEvidence,
        bool cachedCarryEvidence,
        bool currentBombStatePlanted,
        bool currentBombStateDropped)
    {
        return !bombPlantedByRules &&
               ((bombDroppedByRules && cachedDroppedEntityResolved) ||
                (!bombDroppedByRules &&
                 (inventoryC4CarrierEvidence || cachedCarryEvidence))) &&
               !currentBombStatePlanted &&
               (!currentBombStateDropped || cachedDroppedEntityResolved);
    }

    inline bool IsWeaponC4ProbeDue(bool canDeferWeaponC4Probe, uint64_t lastProbeUs, uint64_t nowUs)
    {
        const uint64_t cooldownUs =
            canDeferWeaponC4Probe ? kWeaponC4ProbeCooldownUs : kWeaponC4ProbeUrgentCooldownUs;
        return IsBombCooldownElapsed(lastProbeUs, nowUs, cooldownUs);
    }

    inline bool IsDroppedC4EntityCacheFresh(
        bool bombDroppedByRules,
        bool dropEdge,
        bool worldCandidateAvailable,
        uintptr_t cachedEntity,
        uint64_t cachedUs,
        uint64_t nowUs)
    {
        return bombDroppedByRules &&
               !dropEdge &&
               !worldCandidateAvailable &&
               cachedEntity != 0 &&
               cachedUs > 0 &&
               nowUs >= cachedUs &&
               (nowUs - cachedUs) < kWeaponC4ProbeUrgentCooldownUs;
    }

    inline uint64_t SelectWeaponC4DynamicRefreshUs(
        bool bombDroppedByRules,
        bool currentBombStateDropped,
        bool carryEvidence)
    {
        if (bombDroppedByRules || currentBombStateDropped)
            return kWeaponC4DynamicDroppedRefreshUs;
        return carryEvidence
            ? kWeaponC4DynamicCarriedRefreshUs
            : kWeaponC4DynamicUnknownRefreshUs;
    }

    inline bool ShouldDeferBombReadForBusyLane(
        bool higherPriorityLaneActive,
        bool urgent,
        bool forced,
        bool cacheStable,
        uint64_t cacheAgeUs,
        uint64_t refreshUs)
    {
        if (!higherPriorityLaneActive || forced || !cacheStable)
            return false;
        const uint64_t maximumDeferralUs =
            urgent
                ? kUrgentBombHeavyReadMaxDeferralUs
                : kBombHeavyReadMaxDeferralUs;
        return cacheAgeUs < refreshUs + maximumDeferralUs;
    }

    inline bool IsWeaponC4DynamicReadDue(
        bool entityResolved,
        bool cacheStable,
        bool forced,
        bool urgent,
        bool higherPriorityLaneActive,
        uint64_t lastReadUs,
        uint64_t nowUs,
        uint64_t refreshUs)
    {
        if (!entityResolved)
            return false;

        const bool due =
            forced ||
            !cacheStable ||
            IsBombCooldownElapsed(lastReadUs, nowUs, refreshUs);
        if (!due)
            return false;

        const uint64_t cacheAgeUs =
            lastReadUs > 0 && nowUs >= lastReadUs
                ? nowUs - lastReadUs
                : 0u;
        return !ShouldDeferBombReadForBusyLane(
            higherPriorityLaneActive,
            urgent,
            forced,
            cacheStable,
            cacheAgeUs,
            refreshUs);
    }

    inline bool ShouldCarryOverrideDroppedBomb(
        bool droppedPositionValid,
        int droppedScore,
        bool bombDroppedByRules,
        bool strongCarryEvidence,
        bool cachedCarryEvidence)
    {
        if (strongCarryEvidence)
            return true;
        if (bombDroppedByRules)
            return false;
        if (!droppedPositionValid)
            return cachedCarryEvidence;
        return cachedCarryEvidence &&
               droppedScore < kDroppedC4ConfirmedScore;
    }

    inline bool HasStrictInventoryCarryEvidence(
        bool ownerAlive,
        bool ownerInventoryHasC4,
        bool anyInventoryHasC4)
    {
        return anyInventoryHasC4 ||
               (ownerAlive && ownerInventoryHasC4);
    }

    inline bool HasStrictCurrentCarryEvidence(
        bool ownerAlive,
        bool ownerSelectedC4,
        bool ownerInventoryHasC4,
        bool anyInventoryHasC4)
    {
        return HasStrictInventoryCarryEvidence(
                   ownerAlive,
                   ownerInventoryHasC4,
                   anyInventoryHasC4) ||
               (ownerAlive && ownerSelectedC4);
    }

    inline bool HasRulesCompatibleCarryEvidence(
        bool bombDroppedByRules,
        bool strictInventoryEvidence,
        bool broadCarryEvidence)
    {
        return strictInventoryEvidence ||
               (!bombDroppedByRules && broadCarryEvidence);
    }

    inline bool ShouldWorldC4ReplaceWeaponPosition(
        bool weaponPositionFresh,
        bool worldEntityMatchesWeapon,
        bool worldEntityDormant,
        bool preferDetachedWorldWhenDropped = false)
    {
        if (worldEntityDormant)
            return false;
        // Rules-dropped + detached world C4 must be allowed to correct a stale
        // weapon-entity origin (common after round recycle of the same pointer).
        if (preferDetachedWorldWhenDropped)
            return true;
        return !weaponPositionFresh || worldEntityMatchesWeapon;
    }

    inline bool IsDetachedWorldC4Candidate(
        bool bombDroppedByRules,
        bool worldNoOwner,
        int worldOwnerIdx,
        bool worldOwnerAlive)
    {
        return bombDroppedByRules ||
               worldNoOwner ||
               worldOwnerIdx < 0 ||
               !worldOwnerAlive;
    }

    inline bool ShouldPreferDetachedWorldC4Position(
        bool bombDroppedByRules,
        bool worldPositionValid,
        bool worldDetached,
        bool weaponPositionValid,
        float distance2D)
    {
        if (!bombDroppedByRules || !worldPositionValid || !worldDetached)
            return false;
        if (!weaponPositionValid)
            return true;
        // Same-entity recycle often leaves weapon abs-origin at the previous
        // drop spot while the live no-owner world entity has already moved.
        return distance2D > 48.0f;
    }

    inline int NewestFirstC4ListIndex(int size, int probe)
    {
        if (size <= 0 || size > 32 || probe < 0 || probe >= size)
            return -1;
        return size - 1 - probe;
    }

    inline int SelectNewestC4DropCandidateIndex(
        const uint32_t* dropTicks,
        const bool* eligible,
        int count)
    {
        if (!dropTicks || !eligible || count <= 0 || count > 32)
            return -1;

        int newestTickIndex = -1;
        uint32_t newestTick = 0;
        for (int i = 0; i < count; ++i) {
            if (!eligible[i] || dropTicks[i] == 0)
                continue;
            if (newestTickIndex < 0 ||
                static_cast<int32_t>(dropTicks[i] - newestTick) > 0) {
                newestTickIndex = i;
                newestTick = dropTicks[i];
            }
        }
        if (newestTickIndex >= 0)
            return newestTickIndex;

        for (int probe = 0; probe < count; ++probe) {
            const int index = NewestFirstC4ListIndex(count, probe);
            if (index >= 0 && eligible[index])
                return index;
        }
        return -1;
    }

    inline bool ShouldReplaceWorldC4Candidate(
        bool hasCurrent,
        uint32_t candidateDropTick,
        uint32_t currentDropTick,
        int candidateScore,
        int currentScore,
        bool candidateDormant,
        bool currentDormant)
    {
        if (!hasCurrent)
            return true;

        if (candidateDropTick != currentDropTick) {
            if (candidateDropTick == 0)
                return false;
            if (currentDropTick == 0)
                return true;
            return static_cast<int32_t>(
                       candidateDropTick - currentDropTick) > 0;
        }

        if (candidateScore != currentScore)
            return candidateScore > currentScore;
        return currentDormant && !candidateDormant;
    }

    inline int ScoreWorldDroppedC4Candidate(
        bool bombDroppedByRules,
        bool noOwner,
        int ownerIndex,
        bool ownerAlive,
        bool ownerHoldingNearby,
        bool ownerActiveWeaponMatches,
        bool nearTeamT,
        bool continuousWithPublishedDrop)
    {
        int score = 0;
        if (bombDroppedByRules) {
            score += 180;
            if (noOwner)
                score += 120;
            if (ownerIndex >= 0)
                score += 200;
            if (ownerIndex < 0)
                score += 40;
            if (!ownerAlive)
                score += 40;
            if (!ownerHoldingNearby)
                score += 20;
            if (ownerActiveWeaponMatches)
                score -= 20;
            if (ownerAlive && ownerHoldingNearby)
                score += 300;
            if (nearTeamT)
                score += 900;
            if (continuousWithPublishedDrop)
                score += 1000;
            return score;
        }

        if (noOwner)
            score += 220;
        if (ownerIndex < 0)
            score += 120;
        if (!ownerAlive)
            score += 100;
        if (!ownerHoldingNearby)
            score += 80;
        if (ownerActiveWeaponMatches)
            score -= 220;
        if (ownerAlive && ownerHoldingNearby)
            score -= 120;
        if (continuousWithPublishedDrop)
            score += 45;
        return score;
    }

    inline uintptr_t SelectValidatedWeaponC4Entity(
        uintptr_t rootCandidate,
        bool rootIsC4,
        uintptr_t indirectCandidate,
        bool indirectIsC4,
        uintptr_t worldCandidate)
    {
        if (rootCandidate && rootIsC4)
            return rootCandidate;
        if (indirectCandidate && indirectIsC4)
            return indirectCandidate;
        if (worldCandidate &&
            (worldCandidate == rootCandidate ||
             worldCandidate == indirectCandidate)) {
            return worldCandidate;
        }
        return 0;
    }

    inline bool IsPlantedC4Terminal(bool hasExploded, bool bombDefused)
    {
        return hasExploded || bombDefused;
    }

    inline bool IsLivePlantedC4(
        bool plantedByRules,
        bool entityResolved,
        bool metadataFresh,
        bool entityTainted,
        bool terminal,
        bool activated,
        bool ticking,
        bool beingDefused,
        bool blowTimeFresh)
    {
        return plantedByRules &&
               entityResolved &&
               metadataFresh &&
               !entityTainted &&
               !terminal &&
               (activated || ticking || beingDefused || blowTimeFresh);
    }

    inline bool IsDefusingPawnCandidate(
        bool defusing,
        bool pawnResolved,
        int health,
        uint8_t lifeState,
        int team)
    {
        return defusing &&
               pawnResolved &&
               health > 0 &&
               lifeState == 0 &&
               team != kBombCarrierTeamT;
    }

    inline bool IsBombMetadataFresh(
        bool entityResolved,
        bool cacheEntityMatches,
        uint64_t lastReadUs,
        uint64_t nowUs,
        uint64_t freshnessUs = kPlantedC4MetaFreshUs)
    {
        return entityResolved &&
               cacheEntityMatches &&
               lastReadUs > 0 &&
               nowUs >= lastReadUs &&
               (nowUs - lastReadUs) <= freshnessUs;
    }

    inline bool IsAuthoritativeBombTimer(
        bool planted,
        bool terminal,
        bool metadataFresh,
        bool activated,
        bool ticking,
        bool blowTimeValid)
    {
        return planted &&
               !terminal &&
               metadataFresh &&
               (activated || ticking) &&
               blowTimeValid;
    }

    inline bool IsAuthoritativeDefuseTimer(
        bool planted,
        bool terminal,
        bool metadataFresh,
        bool c4BeingDefused,
        bool c4DefuserHandleValid,
        bool defuseTimeValid,
        bool defuseWindowValid)
    {
        return planted &&
               !terminal &&
               metadataFresh &&
               (c4BeingDefused || c4DefuserHandleValid) &&
               defuseTimeValid &&
               defuseWindowValid;
    }

    inline bool ShouldHoldTransientBombTimer(
        bool previousLive,
        bool planted,
        bool terminal,
        uint64_t lastAuthoritativeUs,
        uint64_t nowUs,
        float previousBlowTime,
        float previousTimerLength,
        float currentGameTime)
    {
        return previousLive &&
               planted &&
               !terminal &&
               lastAuthoritativeUs > 0 &&
               nowUs >= lastAuthoritativeUs &&
               (nowUs - lastAuthoritativeUs) <= kBombTimerSignalGapHoldUs &&
               std::isfinite(previousBlowTime) &&
               std::isfinite(previousTimerLength) &&
               std::isfinite(currentGameTime) &&
               previousBlowTime > currentGameTime &&
               previousBlowTime <= currentGameTime + 120.0f &&
               previousTimerLength >= 5.0f &&
               previousTimerLength <= 90.0f;
    }

    inline bool ShouldHoldTransientDefuseTimer(
        bool previousLive,
        bool planted,
        bool terminal,
        uint64_t lastAuthoritativeUs,
        uint64_t nowUs,
        float previousEndTime,
        float currentGameTime)
    {
        return previousLive &&
               planted &&
               !terminal &&
               lastAuthoritativeUs > 0 &&
               nowUs >= lastAuthoritativeUs &&
               (nowUs - lastAuthoritativeUs) <= kDefuseSignalGapHoldUs &&
               std::isfinite(previousEndTime) &&
               std::isfinite(currentGameTime) &&
               previousEndTime > currentGameTime &&
               previousEndTime <= currentGameTime + 15.0f;
    }

    inline uint64_t SelectWorldC4StickyUs(bool bombDroppedByRules)
    {
        return bombDroppedByRules ? kRulesWorldC4StickyUs : kWeakDroppedC4StickyUs;
    }

    inline uint64_t SelectDroppedBombStickyUs(bool bombDroppedByRules, uint64_t rulesDroppedStickyUs)
    {
        return bombDroppedByRules ? rulesDroppedStickyUs : kWeakDroppedC4StickyUs;
    }

    inline uint64_t SelectConfirmedDroppedStickyUs(bool bombDroppedByRules)
    {
        return bombDroppedByRules ? kRulesConfirmedDroppedStickyUs : kWeakConfirmedDroppedStickyUs;
    }

    inline bool IsStickyBombEvidenceFresh(uint64_t lastSeenUs, uint64_t nowUs, uint64_t stickyWindowUs)
    {
        return lastSeenUs > 0 &&
               nowUs >= lastSeenUs &&
               (nowUs - lastSeenUs) <= stickyWindowUs;
    }

    inline bool ShouldUsePlantedC4PositionFallback(
        bool bombPlantedNow,
        bool bombEpochJustWiped,
        uint64_t lastSeenUs,
        uint64_t nowUs,
        uint64_t fallbackWindowUs = kPlantedC4PositionFallbackUs)
    {
        return bombPlantedNow &&
               !bombEpochJustWiped &&
               IsStickyBombEvidenceFresh(lastSeenUs, nowUs, fallbackWindowUs);
    }
}
