#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace esp::data
{
    inline constexpr bool ShouldRunActiveInventoryLane(
        bool laneDue,
        bool playerAuxActive) noexcept
    {
        return laneDue && !playerAuxActive;
    }

    inline constexpr bool ShouldRunFullInventoryLane(
        bool laneDue,
        bool playerAuxActive,
        bool activeInventoryActive) noexcept
    {
        return laneDue &&
               !playerAuxActive &&
               !activeInventoryActive;
    }

    inline constexpr bool ShouldRunBoneLane(
        bool playerAuxActive,
        bool activeInventoryActive,
        bool fullInventoryActive) noexcept
    {
        return !playerAuxActive &&
               !activeInventoryActive &&
               !fullInventoryActive;
    }

    inline constexpr bool NeedsFullInventoryData(
        bool webRadarActive,
        bool bombInventoryActive,
        bool noKnifeWeaponSelectionActive) noexcept
    {
        return webRadarActive ||
               bombInventoryActive ||
               noKnifeWeaponSelectionActive;
    }

    inline constexpr bool ShouldIncludeFullInventoryPlayer(
        bool allPlayersRequired,
        int playerTeam) noexcept
    {
        // Bomb ownership can only belong to T. Unknown teams are retained
        // during scene warmup so the first clean inventory sample is not lost.
        return allPlayersRequired || playerTeam != 3;
    }

    inline constexpr uint64_t SelectDeferredLanePeakUs(
        uint64_t playerAuxUs,
        uint64_t inventoryUs,
        uint64_t boneReadsUs,
        uint64_t worldScanUs) noexcept
    {
        return (std::max)({
            playerAuxUs,
            inventoryUs,
            boneReadsUs,
            worldScanUs
        });
    }

    inline constexpr bool ShouldInvalidateInventoryMetadata(
        uint32_t cachedHandle,
        uint32_t currentHandle,
        uintptr_t cachedEntity,
        uintptr_t currentEntity) noexcept
    {
        return cachedHandle != currentHandle ||
               cachedEntity != currentEntity;
    }

    inline constexpr bool IsInventoryPlayerCoverageComplete(
        bool weaponServicesResolved,
        size_t countBytesRead,
        size_t arrayBytesRead,
        int slotCount,
        bool handleArrayResolved) noexcept
    {
        return weaponServicesResolved &&
               countBytesRead == sizeof(int) &&
               arrayBytesRead == sizeof(uintptr_t) &&
               slotCount > 0 &&
               handleArrayResolved;
    }

    inline constexpr bool IsInventoryWeaponCoverageComplete(
        size_t handleBytesRead,
        bool handleValid,
        bool entityResolved,
        uint16_t itemDefinition) noexcept
    {
        if (handleBytesRead != sizeof(uint32_t))
            return false;
        if (!handleValid)
            return true;
        return entityResolved &&
               itemDefinition > 0 &&
               itemDefinition < 20000u;
    }

    inline constexpr bool IsInventoryMetadataRetryDue(
        bool consumerActive,
        bool metadataMissing,
        uint64_t lastRetryUs,
        uint64_t nowUs,
        uint64_t retryIntervalUs) noexcept
    {
        return consumerActive &&
               metadataMissing &&
               (lastRetryUs == 0 ||
                nowUs < lastRetryUs ||
                (nowUs - lastRetryUs) >= retryIntervalUs);
    }
}
