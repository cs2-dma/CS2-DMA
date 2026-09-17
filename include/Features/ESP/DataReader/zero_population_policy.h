#pragma once

#include <chrono>
#include <cstdint>

namespace esp::data
{
    inline constexpr std::chrono::milliseconds kZeroPopulationProbeDelay{300};
    inline constexpr std::chrono::milliseconds kZeroPopulationRepairDelay{800};
    inline constexpr std::chrono::milliseconds kZeroPopulationFullDelay{2000};
    inline constexpr std::chrono::milliseconds kZeroPopulationRetryDelay{4000};
    inline constexpr uint64_t kZeroPopulationRecoveryRetryCooldownUs = 6000000u;
    inline constexpr uint64_t kZeroPopulationGeneralGraceUs = 1800000u;
    inline constexpr uint64_t kZeroPopulationKnownMapGraceUs = 700000u;

    inline bool IsZeroPopulationRecoveryRetryCooldownElapsed(
        uint64_t lastHardRecoveryUs,
        uint64_t nowUs)
    {
        return lastHardRecoveryUs == 0 ||
               nowUs <= lastHardRecoveryUs ||
               (nowUs - lastHardRecoveryUs) >= kZeroPopulationRecoveryRetryCooldownUs;
    }

    inline bool ShouldRunZeroPopulationProbe(
        bool canAct,
        uint8_t stage,
        std::chrono::steady_clock::duration zeroAge,
        bool suddenDropFromLive)
    {
        return canAct &&
               stage < 1u &&
               (zeroAge >= kZeroPopulationProbeDelay || suddenDropFromLive);
    }

    inline bool ShouldRunZeroPopulationRepair(
        bool canAct,
        uint8_t stage,
        std::chrono::steady_clock::duration zeroAge)
    {
        return canAct &&
               stage < 2u &&
               zeroAge >= kZeroPopulationRepairDelay;
    }

    inline bool ShouldRunZeroPopulationFull(
        bool canAct,
        uint8_t stage,
        std::chrono::steady_clock::duration zeroAge)
    {
        return canAct &&
               stage < 3u &&
               zeroAge >= kZeroPopulationFullDelay;
    }

    inline bool ShouldRetryZeroPopulationRecovery(
        bool canAct,
        bool suspiciousFlatEntityRange,
        uint8_t stage,
        std::chrono::steady_clock::duration zeroAge,
        uint64_t lastHardRecoveryUs,
        uint64_t nowUs)
    {
        return canAct &&
               !suspiciousFlatEntityRange &&
               stage >= 3u &&
               zeroAge >= kZeroPopulationRetryDelay &&
               IsZeroPopulationRecoveryRetryCooldownElapsed(lastHardRecoveryUs, nowUs);
    }

    inline bool IsZeroPopulationGraceElapsed(
        uint64_t sceneAgeUs,
        uint64_t warmupAgeUs,
        bool liveMapRecentlySeen,
        bool liveByRecentHistory,
        bool suspiciousFlatEntityRange,
        bool localIdentityMissing)
    {
        return sceneAgeUs >= kZeroPopulationGeneralGraceUs ||
               warmupAgeUs >= kZeroPopulationGeneralGraceUs ||
               (liveMapRecentlySeen &&
                (sceneAgeUs >= kZeroPopulationKnownMapGraceUs ||
                 warmupAgeUs >= kZeroPopulationKnownMapGraceUs)) ||
               liveByRecentHistory ||
               suspiciousFlatEntityRange ||
               localIdentityMissing;
    }
}
