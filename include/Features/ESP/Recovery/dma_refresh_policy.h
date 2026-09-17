#pragma once

#include "Features/ESP/esp.h"

#include <cstdint>

namespace esp::recovery
{
    inline constexpr uint64_t kDmaProbeRefreshCooldownUs = 2000000u;
    inline constexpr uint64_t kDmaRepairRefreshCooldownUs = 5000000u;
    inline constexpr uint64_t kDmaFullRefreshCooldownUs = 10000000u;
    inline constexpr uint32_t kBoneProbeGlobalFailureThreshold = 2u;
    inline constexpr uint32_t kBoneProbeGlobalDegradedThreshold = 8u;
    static_assert(kDmaProbeRefreshCooldownUs < kDmaRepairRefreshCooldownUs);
    static_assert(kDmaRepairRefreshCooldownUs < kDmaFullRefreshCooldownUs);

    enum DmaRefreshOperation : uint32_t {
        DmaRefreshOperationNone = 0u,
        DmaRefreshOperationTlbPartial = 1u << 0,
        DmaRefreshOperationMemoryPartial = 1u << 1,
        DmaRefreshOperationTlbFull = 1u << 2,
        DmaRefreshOperationMemoryFull = 1u << 3,
        DmaRefreshOperationProcessSpecific = 1u << 4,
    };

    struct DmaRefreshPolicyInput {
        DmaRefreshTier tier = DmaRefreshTier::Probe;
        bool force = false;
        uint64_t nowUs = 0;
        uint64_t lastRefreshUs = 0;
        DmaRefreshTrigger trigger = DmaRefreshTrigger::General;
        bool backgroundRefreshEnabled = false;
        bool stableLiveScene = false;
        int32_t activePlayerCount = 0;
        uint32_t consecutiveFailures = 0;
        uint32_t consecutiveDegraded = 0;
    };

    struct DmaRefreshPolicyDecision {
        bool queueRefresh = false;
        uint32_t pendingFlag = 0;
        uint64_t nextLastRefreshUs = 0;
        uint64_t cooldownUs = 0;
        const char* tierLabel = "probe";
        bool containedLocally = false;
    };

    inline uint64_t DmaRefreshCooldownUs(DmaRefreshTier tier)
    {
        switch (tier) {
        case DmaRefreshTier::Full:
            return kDmaFullRefreshCooldownUs;
        case DmaRefreshTier::Repair:
            return kDmaRepairRefreshCooldownUs;
        case DmaRefreshTier::Probe:
        default:
            return kDmaProbeRefreshCooldownUs;
        }
    }

    inline uint32_t DmaRefreshPendingFlag(DmaRefreshTier tier)
    {
        switch (tier) {
        case DmaRefreshTier::Full:
            return 0x4u;
        case DmaRefreshTier::Repair:
            return 0x2u;
        case DmaRefreshTier::Probe:
        default:
            return 0x1u;
        }
    }

    inline uint32_t DmaRefreshCoveredFlags(DmaRefreshTier tier)
    {
        switch (tier) {
        case DmaRefreshTier::Full:
            return 0x7u;
        case DmaRefreshTier::Repair:
            return 0x3u;
        case DmaRefreshTier::Probe:
        default:
            return 0x1u;
        }
    }

    inline const char* DmaRefreshTierLabel(DmaRefreshTier tier)
    {
        switch (tier) {
        case DmaRefreshTier::Full:
            return "full";
        case DmaRefreshTier::Repair:
            return "repair";
        case DmaRefreshTier::Probe:
        default:
            return "probe";
        }
    }

    inline uint32_t DmaRefreshOperations(DmaRefreshTier tier)
    {
        switch (tier) {
        case DmaRefreshTier::Full:
            return DmaRefreshOperationMemoryFull |
                   DmaRefreshOperationTlbFull |
                   DmaRefreshOperationProcessSpecific;
        case DmaRefreshTier::Repair:
            return DmaRefreshOperationMemoryPartial |
                   DmaRefreshOperationTlbFull |
                   DmaRefreshOperationProcessSpecific;
        case DmaRefreshTier::Probe:
        default:
            // A probe only invalidates stale address translations. Process
            // enumeration belongs to Repair/Full because it may block DMA for
            // hundreds of milliseconds on FPGA-backed sessions.
            return DmaRefreshOperationTlbPartial;
        }
    }

    inline bool HasDmaRefreshOperation(uint32_t operations, DmaRefreshOperation operation)
    {
        return (operations & static_cast<uint32_t>(operation)) != 0u;
    }

    inline bool ShouldContainBoneProbeLocally(const DmaRefreshPolicyInput& input)
    {
        if (input.force ||
            input.tier != DmaRefreshTier::Probe ||
            input.trigger != DmaRefreshTrigger::BoneSlotStale) {
            return false;
        }

        return input.backgroundRefreshEnabled &&
               input.stableLiveScene &&
               input.activePlayerCount > 0 &&
               input.consecutiveFailures < kBoneProbeGlobalFailureThreshold &&
               input.consecutiveDegraded < kBoneProbeGlobalDegradedThreshold;
    }

    inline DmaRefreshPolicyDecision EvaluateDmaRefreshPolicy(const DmaRefreshPolicyInput& input)
    {
        const uint64_t cooldownUs = DmaRefreshCooldownUs(input.tier);
        const uint32_t pendingFlag = DmaRefreshPendingFlag(input.tier);
        const char* tierLabel = DmaRefreshTierLabel(input.tier);

        if (ShouldContainBoneProbeLocally(input)) {
            return {
                false,
                pendingFlag,
                input.lastRefreshUs,
                cooldownUs,
                tierLabel,
                true,
            };
        }

        if (!input.force &&
            input.lastRefreshUs > 0 &&
            input.nowUs >= input.lastRefreshUs &&
            (input.nowUs - input.lastRefreshUs) < cooldownUs) {
            return {
                false,
                pendingFlag,
                input.lastRefreshUs,
                cooldownUs,
                tierLabel,
                false,
            };
        }

        return {
            true,
            pendingFlag,
            input.nowUs,
            cooldownUs,
            tierLabel,
            false,
        };
    }
}
