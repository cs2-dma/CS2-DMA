#pragma once

#include <cstdint>

namespace esp::intervals {
    
    constexpr uint64_t kBaseHighestEntityRefreshUs = 100000;  
    constexpr uint64_t kBaseMinimapRefreshUs       = 500000;  
    constexpr uint64_t kBaseSensitivityRefreshUs   = 750000;  
    constexpr uint64_t kBaseIntervalRefreshUs      = 500000;  
    constexpr uint64_t kBaseViewFallbackRefreshUs  = 10000;
    constexpr uint64_t kBaseGameTimeSweepUs        = 1000000;
    constexpr uint64_t kBaseGameTimeStallProbeUs   = 100000;

    
    constexpr uint64_t kPlayerIdentityAuxUs   = 350000;
    constexpr uint64_t kPlayerMoneyAuxUs      = 50000;
    constexpr uint64_t kPlayerDefuserAuxUs    = 40000;
    constexpr uint64_t kPlayerSpectatorAuxUs  = 150000;

    constexpr uint64_t kInventoryActiveWeaponLaneUs      = 10000;
    constexpr uint64_t kInventoryFullInventoryLaneUs     = 20000;
    constexpr uint64_t kInventoryWeaponServicesRefreshUs = 60000;
    constexpr uint64_t kInventoryMetadataRetryUs         = 50000;

    constexpr uint64_t kHierarchyWarmupRefreshUs = 3000;
    constexpr uint64_t kHierarchySteadyRefreshUs = 20000;
    constexpr uint64_t kHierarchyFullDiscoveryUs = 100000;

    constexpr uint64_t kBoneReadsUs = 5000;

    constexpr uint64_t kBombStickyDroppedUs = 1800000;
    constexpr uint64_t kBombStickyVisibleUs = 900000;
}
