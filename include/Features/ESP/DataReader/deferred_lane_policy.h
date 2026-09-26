#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace esp::data
{
    enum class DeferredLane : uint8_t { None, Auxiliary, ActiveInventory, FullInventory, Bones };

    class DeferredLaneFairness {
    public:
        void Observe(DeferredLane lane, bool due, uint64_t nowUs) noexcept
        {
            auto& since = waiting_[static_cast<size_t>(lane)];
            if (!due) since = 0;
            else if (since == 0 || since > nowUs) since = nowUs;
        }

        void Served(DeferredLane lane) noexcept { waiting_[static_cast<size_t>(lane)] = 0; }

        DeferredLane Select(uint64_t nowUs) noexcept
        {
            if (forcedLastTick_) {
                forcedLastTick_ = false;
                return DeferredLane::None;
            }
            constexpr std::array<uint64_t, 5> budgets{0, 100000, 40000, 100000, 20000};
            DeferredLane chosen = DeferredLane::None;
            uint64_t largestOverdue = 0;
            for (size_t i = 1; i < waiting_.size(); ++i) {
                const uint64_t since = waiting_[i];
                if (since == 0 || since > nowUs || nowUs - since < budgets[i]) continue;
                const uint64_t overdue = nowUs - since - budgets[i] + 1;
                if (overdue > largestOverdue) {
                    chosen = static_cast<DeferredLane>(i);
                    largestOverdue = overdue;
                }
            }
            forcedLastTick_ = chosen != DeferredLane::None;
            return chosen;
        }

    private:
        std::array<uint64_t, 5> waiting_{};
        bool forcedLastTick_ = false;
    };

    inline constexpr bool ShouldPrioritizeBoneLane(bool requested, bool prioritizedLastTick,
        uint64_t lastOpportunityUs, uint64_t nowUs) noexcept
    {
        return requested && !prioritizedLastTick && (lastOpportunityUs == 0 ||
            nowUs < lastOpportunityUs || nowUs - lastOpportunityUs >= 20000u);
    }

    inline constexpr bool IsBoneServiceOverdue(uint64_t sampleUs, uint64_t nowUs) noexcept
    {
        return sampleUs == 0 || nowUs < sampleUs || nowUs - sampleUs > 100000u;
    }

    inline constexpr bool NeedsTargetWeaponTelemetry(bool targetEnabled,
        bool aimbotEnabled, bool triggerbotEnabled, bool aimDamageCheck) noexcept
    {
        return targetEnabled &&
            (triggerbotEnabled || (aimbotEnabled && aimDamageCheck));
    }

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
