#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace esp::data
{
    enum class WorldEntityClass : uint8_t
    {
        Unknown = 0,
        SmokeProjectile,
        Inferno,
        DecoyProjectile,
        HeProjectile,
        MolotovProjectile,
        FlashProjectile,
        DroppedWeapon,
    };

    enum class WorldOwnerEvidence : uint8_t
    {
        Unknown = 0,
        Held,
        Dropped,
    };

    inline constexpr int kFirstWorldEntitySlot = 64;

    inline uint32_t NextWorldDiscoveryShard(uint32_t activeShard, uint32_t shardCount,
        bool discoveryPerformed) noexcept
    {
        if (shardCount <= 1u) return 0;
        return (activeShard % shardCount + (discoveryPerformed ? 1u : 0u)) % shardCount;
    }

    inline bool ShouldReadWorldItemDefinition(
        bool wantsUtilityData,
        bool droppedItemsDue,
        bool bombRescueDue) noexcept
    {
        return wantsUtilityData || droppedItemsDue || bombRescueDue;
    }

    inline bool ShouldRetryWorldIdentity(
        bool probeDue,
        uint16_t itemId,
        WorldEntityClass entityClass,
        bool classIdentityRequired,
        uint64_t lastRetryUs,
        uint64_t nowUs,
        uint64_t retryIntervalUs) noexcept
    {
        if (!probeDue || entityClass != WorldEntityClass::Unknown)
            return false;
        if (itemId != 0u && !classIdentityRequired)
            return false;
        return lastRetryUs == 0u ||
               nowUs < lastRetryUs ||
               (nowUs - lastRetryUs) >= retryIntervalUs;
    }

    inline uint16_t UtilityItemIdFromWorldClass(
        WorldEntityClass entityClass) noexcept
    {
        switch (entityClass) {
        case WorldEntityClass::SmokeProjectile:
            return 45;
        case WorldEntityClass::Inferno:
        case WorldEntityClass::MolotovProjectile:
            return 46;
        case WorldEntityClass::DecoyProjectile:
            return 47;
        case WorldEntityClass::HeProjectile:
            return 44;
        case WorldEntityClass::FlashProjectile:
            return 43;
        default:
            return 0;
        }
    }

    inline bool IsWorldUtilityClass(WorldEntityClass entityClass) noexcept
    {
        switch (entityClass) {
        case WorldEntityClass::SmokeProjectile:
        case WorldEntityClass::Inferno:
        case WorldEntityClass::DecoyProjectile:
        case WorldEntityClass::HeProjectile:
        case WorldEntityClass::MolotovProjectile:
        case WorldEntityClass::FlashProjectile:
            return true;
        default:
            return false;
        }
    }

    inline uint16_t CanonicalWorldItemId(uint16_t rawItemId, WorldEntityClass entityClass) noexcept
    {
        // Inferno/projectiles are not C_EconEntity. Bytes at an item-definition
        // offset can be fire positions or other unrelated payload.
        const uint16_t utilityId = UtilityItemIdFromWorldClass(entityClass);
        if (entityClass == WorldEntityClass::MolotovProjectile && rawItemId == 48)
            return 48;
        return utilityId != 0 ? utilityId : rawItemId;
    }

    inline std::string_view ReadWorldDesignerName(const char* name, size_t capacity,
        size_t completedBytes) noexcept
    {
        if (!name) return {};
        const size_t count = (std::min)(capacity, completedBytes);
        for (size_t i = 0; i < count; ++i)
            if (name[i] == '\0') return std::string_view(name, i);
        return {}; // Do not accept a terminator in an unread/zero-filled tail.
    }

    inline bool ShouldRetainWorldEntity(
        bool wantsBomb,
        bool wantsDroppedItems,
        bool wantsUtility,
        bool isBombItem,
        bool isDroppedItem,
        bool isUtilityItem,
        WorldEntityClass entityClass,
        bool knownUtilitySubclass,
        bool hasUtilityHistory) noexcept
    {
        if (wantsBomb && isBombItem)
            return true;
        if (wantsDroppedItems && isDroppedItem &&
            entityClass == WorldEntityClass::DroppedWeapon)
            return true;
        return wantsUtility &&
               (isUtilityItem ||
                IsWorldUtilityClass(entityClass) ||
                knownUtilitySubclass ||
                hasUtilityHistory);
    }

    inline bool WorldClassNameEquals(
        std::string_view value,
        std::string_view expected) noexcept
    {
        if (value.size() != expected.size())
            return false;
        for (size_t i = 0; i < value.size(); ++i) {
            const char left = value[i] >= 'A' && value[i] <= 'Z'
                ? static_cast<char>(value[i] + ('a' - 'A'))
                : value[i];
            if (left != expected[i])
                return false;
        }
        return true;
    }

    inline bool WorldClassNameContains(
        std::string_view value,
        std::string_view expected) noexcept
    {
        if (expected.empty() || value.size() < expected.size())
            return false;
        for (size_t start = 0; start + expected.size() <= value.size(); ++start) {
            bool matches = true;
            for (size_t i = 0; i < expected.size(); ++i) {
                const char left = value[start + i] >= 'A' && value[start + i] <= 'Z'
                    ? static_cast<char>(value[start + i] + ('a' - 'A'))
                    : value[start + i];
                if (left != expected[i]) {
                    matches = false;
                    break;
                }
            }
            if (matches)
                return true;
        }
        return false;
    }

    inline WorldEntityClass ClassifyWorldDesignerName(
        std::string_view designerName) noexcept
    {
        // Designer names are normally exact lowercase strings. Some builds and
        // schema readers expose a decorated class name instead, so accept only
        // bounded, grenade-specific substrings as a compatibility fallback.
        if (WorldClassNameContains(designerName, "smokegrenade_projectile") ||
            WorldClassNameContains(designerName, "smokegrenadeprojectile"))
            return WorldEntityClass::SmokeProjectile;
        if (WorldClassNameContains(designerName, "decoy_projectile") ||
            WorldClassNameContains(designerName, "decoyprojectile"))
            return WorldEntityClass::DecoyProjectile;
        if (WorldClassNameContains(designerName, "hegrenade_projectile") ||
            WorldClassNameContains(designerName, "hegrenadeprojectile"))
            return WorldEntityClass::HeProjectile;
        if (WorldClassNameContains(designerName, "molotov_projectile") ||
            WorldClassNameContains(designerName, "molotovprojectile") ||
            WorldClassNameContains(designerName, "incendiarygrenade_projectile") ||
            WorldClassNameContains(designerName, "incendiarygrenadeprojectile") ||
            WorldClassNameContains(designerName, "incendiarygrenade_proj"))
            return WorldEntityClass::MolotovProjectile;
        if (WorldClassNameContains(designerName, "flashbang_projectile") ||
            WorldClassNameContains(designerName, "flashbangprojectile"))
            return WorldEntityClass::FlashProjectile;
        if (WorldClassNameEquals(designerName, "inferno") ||
            WorldClassNameContains(designerName, "c_inferno"))
            return WorldEntityClass::Inferno;
        if (designerName.size() > 7u &&
            WorldClassNameContains(designerName.substr(0u, 7u), "weapon_"))
            return WorldEntityClass::DroppedWeapon;
        return WorldEntityClass::Unknown;
    }

    inline WorldOwnerEvidence ResolveWorldOwnerEvidence(
        bool ownerFieldAvailable,
        bool ownerReadComplete,
        uint32_t ownerHandle) noexcept
    {
        if (!ownerFieldAvailable || !ownerReadComplete)
            return WorldOwnerEvidence::Unknown;
        if (ownerHandle == 0u || ownerHandle == UINT32_MAX)
            return WorldOwnerEvidence::Dropped;
        return WorldOwnerEvidence::Held;
    }

    inline constexpr uint64_t kWorldScanSceneSettleUs = 2000000u;
    inline constexpr uint64_t kWorldBombRescueSceneSettleUs = 300000u;
    inline constexpr uint64_t kWorldBombFullDiscoverySafetyUs = 250000u;
    inline constexpr uint64_t kWorldOwnerEvidenceHoldUs = 250000u;

    inline bool IsWorldOwnerEvidenceFresh(
        WorldOwnerEvidence evidence,
        uint64_t sampleUs,
        uint64_t nowUs) noexcept
    {
        return evidence != WorldOwnerEvidence::Unknown &&
               sampleUs != 0u &&
               nowUs >= sampleUs &&
               (nowUs - sampleUs) <= kWorldOwnerEvidenceHoldUs;
    }

    inline bool IsWorldFieldReadComplete(uint32_t bytesRead, size_t expectedBytes)
    {
        return expectedBytes != 0u &&
               expectedBytes <= UINT32_MAX &&
               bytesRead == static_cast<uint32_t>(expectedBytes);
    }

    inline bool HasUsableWorldStructuralReads(
        bool aggregateBatchSucceeded,
        uint32_t completedBlockReads)
    {
        // Optional utility probes share a scatter batch with structural reads.
        // Their failure must not discard block/entity pointers that were read fully.
        return aggregateBatchSucceeded || completedBlockReads > 0u;
    }

    inline bool AreWorldBasicDetailsComplete(
        bool sceneNodeComplete,
        bool subclassComplete)
    {
        // Owner handles and Econ item definitions are domain-specific evidence.
        // Utility entities are valid without either field.
        return sceneNodeComplete && subclassComplete;
    }

    struct WorldDomainCadence {
        uint64_t scanIntervalUs = 0;
        uint64_t utilityDetailIntervalUs = 0;
        uint64_t utilityProbeIntervalUs = 0;
        uint32_t discoveryShardCount = 1;
        uint64_t droppedItemsIntervalUs = 0;
        uint64_t activeUtilityIntervalUs = 0;
        uint64_t slowDiscoveryIntervalUs = 0;
        uint64_t bombRescueIntervalUs = 0;
    };

    inline bool IsWorldScanAllowed(bool liveWorldContext, uint64_t sceneResetAgeUs)
    {
        return liveWorldContext && sceneResetAgeUs > kWorldScanSceneSettleUs;
    }

    inline bool IsWorldBombRescueScanAllowed(bool liveWorldContext, uint64_t sceneResetAgeUs)
    {
        return liveWorldContext && sceneResetAgeUs > kWorldBombRescueSceneSettleUs;
    }

    inline bool ShouldRunScheduledWorldScan(
        bool scanDue,
        bool urgentBombRescueDue,
        bool higherPriorityLaneActive,
        bool maximumDeferralElapsed)
    {
        return scanDue &&
               (urgentBombRescueDue ||
                !higherPriorityLaneActive ||
                maximumDeferralElapsed);
    }

    inline bool ShouldForceBombWorldDiscovery(
        bool bombDroppedByRules,
        bool hasKnownCandidateSlots,
        bool confirmedDropPosition,
        uint64_t lastFullDiscoveryUs,
        uint64_t nowUs)
    {
        if (!bombDroppedByRules)
            return false;
        if (!hasKnownCandidateSlots || !confirmedDropPosition)
            return true;
        return lastFullDiscoveryUs == 0 ||
               nowUs < lastFullDiscoveryUs ||
               (nowUs - lastFullDiscoveryUs) >=
                   kWorldBombFullDiscoverySafetyUs;
    }

    inline uint64_t CalculateWorldScanIntervalUs(int highestEntityIndex)
    {
        if (highestEntityIndex <= 800)
            return 50000u;
        if (highestEntityIndex <= 1200)
            return 70000u;
        if (highestEntityIndex <= 2000)
            return 90000u;
        return 120000u;
    }

    inline uint64_t CalculateWorldUtilityDetailIntervalUs(
        int highestEntityIndex,
        bool wantsWorldProjectiles,
        uint64_t worldScanIntervalUs)
    {
        if (highestEntityIndex <= 1200)
            return worldScanIntervalUs;
        if (highestEntityIndex <= 1600)
            return wantsWorldProjectiles ? 110000u : 140000u;
        if (highestEntityIndex <= 2400)
            return wantsWorldProjectiles ? 130000u : 160000u;
        return wantsWorldProjectiles ? 150000u : 200000u;
    }

    inline bool ShouldUseWorldIdleDiscovery(
        bool forceBombWorldDiscovery,
        bool wantsGeneralWorldScan,
        uint32_t worldIdleScanStreak,
        uint32_t worldTrackedIndexCount,
        uint32_t worldMarkerCount)
    {
        return !forceBombWorldDiscovery &&
               wantsGeneralWorldScan &&
               worldIdleScanStreak >= 3u &&
               worldTrackedIndexCount == 0u &&
               worldMarkerCount == 0u;
    }

    inline uint64_t CalculateEffectiveWorldUtilityDetailIntervalUs(
        int highestEntityIndex,
        bool wantsWorldProjectiles,
        bool worldIdleDiscovery,
        uint64_t utilityDetailIntervalUs)
    {
        if (!worldIdleDiscovery)
            return utilityDetailIntervalUs;
        if (highestEntityIndex <= 1200)
            return wantsWorldProjectiles ? 90000u : 140000u;
        if (highestEntityIndex <= 2200)
            return wantsWorldProjectiles ? 120000u : 180000u;
        return wantsWorldProjectiles ? 150000u : 220000u;
    }

    inline uint64_t CalculateEffectiveWorldUtilityProbeIntervalUs(
        bool wantsWorldProjectiles,
        bool worldIdleDiscovery,
        uint64_t effectiveUtilityDetailIntervalUs)
    {
        const uint64_t minProbeIntervalUs =
            worldIdleDiscovery
                ? (wantsWorldProjectiles ? 180000u : 260000u)
                : (wantsWorldProjectiles ? 150000u : 180000u);
        return std::max<uint64_t>(effectiveUtilityDetailIntervalUs, minProbeIntervalUs);
    }

    inline uint32_t CalculateWorldDiscoveryShardCount(
        int highestEntityIndex,
        bool wantsWorldProjectiles,
        bool forceBombWorldDiscovery,
        bool worldIdleDiscovery,
        uint32_t worldWarmupScans)
    {
        if (forceBombWorldDiscovery)
            return 1u;
        if (worldWarmupScans < 2u)
            return 1u;
        if (worldIdleDiscovery) {
            if (highestEntityIndex <= 800)
                return wantsWorldProjectiles ? 3u : 4u;
            if (highestEntityIndex <= 1400)
                return wantsWorldProjectiles ? 4u : 6u;
            if (highestEntityIndex <= 2200)
                return wantsWorldProjectiles ? 6u : 8u;
            return wantsWorldProjectiles ? 8u : 10u;
        }
        if (highestEntityIndex <= 800)
            return 2u;
        if (highestEntityIndex <= 1400)
            return 3u;
        if (highestEntityIndex <= 2200)
            return 4u;
        return 6u;
    }

    inline WorldDomainCadence CalculateWorldDomainCadence(
        int highestEntityIndex,
        bool wantsWorldProjectiles,
        bool forceBombWorldDiscovery,
        bool worldIdleDiscovery,
        uint32_t worldWarmupScans)
    {
        WorldDomainCadence result{};
        result.scanIntervalUs = CalculateWorldScanIntervalUs(highestEntityIndex);
        const uint64_t utilityDetailIntervalUs =
            CalculateWorldUtilityDetailIntervalUs(
                highestEntityIndex,
                wantsWorldProjectiles,
                result.scanIntervalUs);
        result.utilityDetailIntervalUs =
            CalculateEffectiveWorldUtilityDetailIntervalUs(
                highestEntityIndex,
                wantsWorldProjectiles,
                worldIdleDiscovery,
                utilityDetailIntervalUs);
        result.utilityProbeIntervalUs =
            CalculateEffectiveWorldUtilityProbeIntervalUs(
                wantsWorldProjectiles,
                worldIdleDiscovery,
                result.utilityDetailIntervalUs);
        result.discoveryShardCount =
            CalculateWorldDiscoveryShardCount(
                highestEntityIndex,
                wantsWorldProjectiles,
                forceBombWorldDiscovery,
                worldIdleDiscovery,
                worldWarmupScans);
        result.droppedItemsIntervalUs =
            worldIdleDiscovery
                ? std::max<uint64_t>(result.scanIntervalUs, 90000u)
                : result.scanIntervalUs;
        result.activeUtilityIntervalUs =
            worldIdleDiscovery
                ? std::max<uint64_t>(result.utilityDetailIntervalUs, 90000u)
                : result.utilityDetailIntervalUs;
        result.slowDiscoveryIntervalUs =
            worldIdleDiscovery
                ? std::max<uint64_t>(result.scanIntervalUs * 2u, 120000u)
                : result.scanIntervalUs;
        result.bombRescueIntervalUs =
            forceBombWorldDiscovery
                ? std::min<uint64_t>(result.scanIntervalUs, 30000u)
                : result.scanIntervalUs;
        return result;
    }
}
