#pragma once

#include "Features/ESP/esp.h"

#include <cstdint>

namespace esp::recovery {
    struct DmaCacheProfile {
        uint64_t tickPeriodMs = 300;
        uint64_t readCacheTicks = 1;
        uint64_t tlbCacheTicks = 7;
        uint64_t processPartialTicks = 100;
        uint64_t processFullTicks = 4000;
    };

    inline constexpr DmaCacheProfile kDmaMaintenanceCacheProfile = {};
    inline constexpr DmaCacheProfile kDmaLiveCacheProfile = {
        2000,
        1,
        1,
        1800,
        10800,
    };
    inline constexpr DmaCacheProfile kDmaProcessDiscoveryCacheProfile = {
        250,
        1,
        4,
        1,
        1,
    };

    inline constexpr uint64_t kMemProcFsSlowRefreshTicks = 3000;
    inline constexpr uint64_t kDmaCacheProfileRetryUs = 5000000u;

    inline constexpr bool ShouldAttemptDmaCacheProfileApply(
        bool force,
        bool sameMode,
        bool profileVerified,
        uint64_t lastAttemptUs,
        uint64_t nowUs) noexcept
    {
        if (force || !sameMode)
            return true;
        if (profileVerified)
            return false;
        return lastAttemptUs == 0 ||
               nowUs < lastAttemptUs ||
               (nowUs - lastAttemptUs) >= kDmaCacheProfileRetryUs;
    }

    constexpr uint64_t CacheIntervalMs(uint64_t tickPeriodMs, uint64_t ticks)
    {
        return tickPeriodMs * ticks;
    }

    constexpr const DmaCacheProfile& CacheProfileForMode(esp::DmaCacheMode mode)
    {
        switch (mode) {
        case esp::DmaCacheMode::Live:
            return kDmaLiveCacheProfile;
        case esp::DmaCacheMode::ProcessDiscovery:
            return kDmaProcessDiscoveryCacheProfile;
        case esp::DmaCacheMode::Maintenance:
        default:
            return kDmaMaintenanceCacheProfile;
        }
    }

    constexpr const char* DmaCacheModeName(esp::DmaCacheMode mode)
    {
        switch (mode) {
        case esp::DmaCacheMode::Live:
            return "live";
        case esp::DmaCacheMode::ProcessDiscovery:
            return "process_discovery";
        case esp::DmaCacheMode::Maintenance:
        default:
            return "maintenance";
        }
    }

    static_assert(CacheIntervalMs(
        kDmaLiveCacheProfile.tickPeriodMs,
        kDmaLiveCacheProfile.tlbCacheTicks) == 2000);
    static_assert(CacheIntervalMs(
        kDmaLiveCacheProfile.tickPeriodMs,
        kDmaLiveCacheProfile.processPartialTicks) == 3600000);
    static_assert(CacheIntervalMs(
        kDmaLiveCacheProfile.tickPeriodMs,
        kDmaLiveCacheProfile.processFullTicks) == 21600000);
    static_assert(CacheIntervalMs(
        kDmaProcessDiscoveryCacheProfile.tickPeriodMs,
        kDmaProcessDiscoveryCacheProfile.processPartialTicks) == 250);
    static_assert(CacheIntervalMs(
        kDmaProcessDiscoveryCacheProfile.tickPeriodMs,
        kDmaProcessDiscoveryCacheProfile.processFullTicks) == 250);
    static_assert(CacheIntervalMs(
        kDmaLiveCacheProfile.tickPeriodMs,
        kMemProcFsSlowRefreshTicks) == 6000000);
}
