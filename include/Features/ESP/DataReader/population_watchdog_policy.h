#pragma once

#include <algorithm>
#include <cstdint>

namespace esp::data
{
    enum class PopulationWatchdogRefreshKind : uint8_t {
        Probe = 0,
        Repair,
        Full,
    };

    enum class LaunchUnderresolvedAction : uint8_t {
        None = 0,
        Probe,
        Repair,
        Full,
    };

    inline constexpr uint64_t kPopulationFullGraceResetAgeUs = 2500000u;
    inline constexpr uint64_t kPopulationFullGraceWarmupAgeUs = 1800000u;
    inline constexpr uint64_t kPopulationKnownMapGraceUs = 700000u;
    inline constexpr uint64_t kPopulationWatchdogRefreshCooldownUs = 2500000u;
    inline constexpr uint32_t kPopulationWatchdogRefreshStreak = 8u;
    inline constexpr uint64_t kPopulationWatchdogRefreshAgeUs = 1200000u;
    inline constexpr uint64_t kPopulationWatchdogRepairAgeUs = 2500000u;
    inline constexpr uint64_t kPopulationWatchdogHardAgeUs = 5500000u;
    inline constexpr uint64_t kPopulationWatchdogStaleCommittedResetAgeUs = 1500000u;
    inline constexpr uint64_t kPopulationWatchdogHardCooldownUs = 8000000u;
    inline constexpr uint64_t kLaunchUnderresolvedMinResetAgeUs = 800000u;
    inline constexpr uint64_t kLaunchUnderresolvedMaxResetAgeUs = 12000000u;
    inline constexpr uint64_t kLaunchUnderresolvedProbeAgeUs = 600000u;
    inline constexpr uint64_t kLaunchUnderresolvedRepairAgeUs = 2200000u;
    inline constexpr uint64_t kLaunchUnderresolvedFullAgeUs = 4000000u;
    inline constexpr uint64_t kLaunchUnderresolvedRefreshCooldownUs = 900000u;
    inline constexpr uint64_t kStableLowControllerGuardUs = 5000000u;
    inline constexpr uint64_t kStableLowControllerAcceptUs = 12000000u;

    inline bool IsPopulationGraceElapsed(
        bool populationMapKnown,
        uint64_t populationResetAgeUs,
        uint64_t populationWarmupAgeUs)
    {
        return (populationResetAgeUs >= kPopulationFullGraceResetAgeUs &&
                populationWarmupAgeUs >= kPopulationFullGraceWarmupAgeUs) ||
               (populationMapKnown &&
                (populationResetAgeUs >= kPopulationKnownMapGraceUs ||
                 populationWarmupAgeUs >= kPopulationKnownMapGraceUs));
    }

    inline bool IsWatchdogCooldownElapsed(uint64_t lastRefreshUs, uint64_t nowUs, uint64_t cooldownUs)
    {
        return lastRefreshUs == 0 ||
               nowUs <= lastRefreshUs ||
               (nowUs - lastRefreshUs) >= cooldownUs;
    }

    inline bool IsPopulationWatchdogRefreshDue(
        uint64_t lastRefreshUs,
        uint64_t nowUs,
        uint32_t streak,
        uint64_t brokenAgeUs)
    {
        return IsWatchdogCooldownElapsed(
                   lastRefreshUs,
                   nowUs,
                   kPopulationWatchdogRefreshCooldownUs) &&
               streak >= kPopulationWatchdogRefreshStreak &&
               brokenAgeUs >= kPopulationWatchdogRefreshAgeUs;
    }

    inline PopulationWatchdogRefreshKind SelectPopulationWatchdogRefreshKind(
        uint64_t brokenAgeUs)
    {
        if (brokenAgeUs >= kPopulationWatchdogHardAgeUs) {
            return PopulationWatchdogRefreshKind::Full;
        }

        if (brokenAgeUs >= kPopulationWatchdogRepairAgeUs) {
            return PopulationWatchdogRefreshKind::Repair;
        }

        return PopulationWatchdogRefreshKind::Probe;
    }

    inline bool ShouldSoftResetStaleCommittedPopulation(
        bool staleCommittedPopulation,
        uint64_t brokenAgeUs)
    {
        return staleCommittedPopulation &&
               brokenAgeUs >= kPopulationWatchdogStaleCommittedResetAgeUs;
    }

    inline bool IsStaleCommittedPopulation(
        bool watchdogEligible,
        int resolvedLiveCount,
        int committedLiveCount,
        int resolvedControllersCount)
    {
        if (!watchdogEligible || resolvedLiveCount < 2 || committedLiveCount < 6)
            return false;

        const int missingLivePlayers = committedLiveCount - resolvedLiveCount;
        const int minimumGap = std::max(3, committedLiveCount / 3);
        return missingLivePlayers >= minimumGap &&
               resolvedControllersCount < committedLiveCount;
    }

    inline bool ShouldRequestPopulationWatchdogRecovery(
        PopulationWatchdogRefreshKind refreshKind,
        uint64_t lastHardRefreshUs,
        uint64_t nowUs)
    {
        return refreshKind == PopulationWatchdogRefreshKind::Full &&
               IsWatchdogCooldownElapsed(
                   lastHardRefreshUs,
                   nowUs,
                   kPopulationWatchdogHardCooldownUs);
    }

    inline bool IsLaunchUnderresolvedPopulation(
        bool liveMatchContext,
        bool localAliveEvidence,
        bool localTrackingStalled,
        uint64_t populationResetAgeUs,
        int highestEntityIndex,
        int resolvedControllersCount,
        int resolvedCoreSlots)
    {
        const bool ordinaryLaunchWindow =
            localAliveEvidence &&
            populationResetAgeUs <= kLaunchUnderresolvedMaxResetAgeUs;
        const bool stalledLiveScene =
            localTrackingStalled &&
            resolvedCoreSlots <= 1;
        return liveMatchContext &&
               populationResetAgeUs >= kLaunchUnderresolvedMinResetAgeUs &&
               highestEntityIndex >= 64 &&
               resolvedControllersCount <= 1 &&
               (ordinaryLaunchWindow || stalledLiveScene);
    }

    inline bool IsLaunchUnderresolvedRefreshCooldownElapsed(uint64_t lastRefreshUs, uint64_t nowUs)
    {
        return IsWatchdogCooldownElapsed(
            lastRefreshUs,
            nowUs,
            kLaunchUnderresolvedRefreshCooldownUs);
    }

    inline LaunchUnderresolvedAction SelectLaunchUnderresolvedAction(
        uint64_t underresolvedAgeUs,
        bool refreshCooldownElapsed,
        bool didLaunchProbe,
        bool didLaunchRepair,
        bool didLaunchFull,
        bool allowFull)
    {
        if (!refreshCooldownElapsed)
            return LaunchUnderresolvedAction::None;

        if (allowFull &&
            underresolvedAgeUs >= kLaunchUnderresolvedFullAgeUs &&
            !didLaunchFull) {
            return LaunchUnderresolvedAction::Full;
        }

        if (underresolvedAgeUs >= kLaunchUnderresolvedProbeAgeUs &&
            underresolvedAgeUs < kLaunchUnderresolvedRepairAgeUs &&
            !didLaunchProbe) {
            return LaunchUnderresolvedAction::Probe;
        }

        if (underresolvedAgeUs >= kLaunchUnderresolvedRepairAgeUs &&
            !didLaunchRepair) {
            return LaunchUnderresolvedAction::Repair;
        }

        return LaunchUnderresolvedAction::None;
    }

    inline bool IsStableLowControllerGuardActive(uint64_t stableLowSinceUs, uint64_t nowUs)
    {
        return stableLowSinceUs > 0 &&
               nowUs >= stableLowSinceUs &&
               (nowUs - stableLowSinceUs) < kStableLowControllerGuardUs;
    }

    inline bool ShouldAcceptStableLowControllerPopulation(uint64_t stableLowSinceUs, uint64_t nowUs)
    {
        return stableLowSinceUs > 0 &&
               nowUs >= stableLowSinceUs &&
               (nowUs - stableLowSinceUs) >= kStableLowControllerAcceptUs;
    }

    inline bool IsControllerPopulationCollapsed(
        bool watchdogEligible,
        bool localAliveEvidence,
        int expectedControllers,
        int observedControllers,
        bool stableLowPopulationPending)
    {
        return watchdogEligible &&
               localAliveEvidence &&
               !stableLowPopulationPending &&
               expectedControllers >= 4 &&
               observedControllers > 0 &&
               observedControllers <= std::max(1, expectedControllers / 4);
    }
}
