#pragma once

#include "Features/ESP/esp.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace esp::data
{
    using PlayerTeamSample = uint8_t; // C_BaseEntity::m_iTeamNum; next field is spawn flags.
    static_assert(sizeof(PlayerTeamSample) == 1);
    inline constexpr uint64_t kPendingLocalTeamSwitchWindowUs = 1500000u;
    inline constexpr uint64_t kLocalTeamSwitchEdgeCooldownUs = 2000000u;
    inline constexpr uint64_t kPlayerCoreInitialBatchHoldUs = 150000u;

    enum class PlayerCoreBatchDecision : uint8_t
    {
        Hold = 0,
        Coherent,
        Degraded,
    };

    inline PlayerCoreBatchDecision SelectPlayerCoreBatchDecision(
        int resolvedSlots,
        int plausibleSlots,
        uint64_t partialSinceUs,
        uint64_t lastAcceptedUs,
        uint64_t nowUs) noexcept
    {
        if (resolvedSlots <= 0)
            return PlayerCoreBatchDecision::Hold;
        if (plausibleSlots >= resolvedSlots)
            return PlayerCoreBatchDecision::Coherent;

        // Read failures belong to individual slots, not to their healthy peers.
        // A population ratio must never freeze valid records indefinitely.
        if (plausibleSlots <= 0 ||
            partialSinceUs == 0 ||
            nowUs < partialSinceUs) {
            return PlayerCoreBatchDecision::Hold;
        }

        // Once a coherent batch has been accepted, commit the valid portion
        // immediately. Per-slot continuity protects the missing records; a
        // global hold would unnecessarily freeze every player in the frame.
        if (lastAcceptedUs != 0)
            return PlayerCoreBatchDecision::Degraded;

        return (nowUs - partialSinceUs) >= kPlayerCoreInitialBatchHoldUs
            ? PlayerCoreBatchDecision::Degraded
            : PlayerCoreBatchDecision::Hold;
    }

    struct PlayerCoreReadCompletion
    {
        uint32_t healthBytes = 0;
        uint32_t armorBytes = 0;
        uint32_t lifeStateBytes = 0;
        uint32_t positionBytes = 0;
        bool teamReadRequired = false;
        uint32_t teamBytes = 0;
    };

    struct CoreVitalReadLayout
    {
        bool coalesced = false;
        std::ptrdiff_t baseOffset = 0;
        size_t spanBytes = 0;
        size_t healthDelta = 0;
        size_t lifeStateDelta = 0;
    };

    inline CoreVitalReadLayout ResolveCoreVitalReadLayout(
        std::ptrdiff_t healthOffset,
        std::ptrdiff_t lifeStateOffset,
        size_t maximumSpanBytes = 64u) noexcept
    {
        if (healthOffset <= 0 || lifeStateOffset <= 0 || maximumSpanBytes == 0u)
            return {};
        if (healthOffset > (std::numeric_limits<std::ptrdiff_t>::max)() -
                               static_cast<std::ptrdiff_t>(sizeof(int)) ||
            lifeStateOffset > (std::numeric_limits<std::ptrdiff_t>::max)() -
                                  static_cast<std::ptrdiff_t>(sizeof(uint8_t))) {
            return {};
        }

        const std::ptrdiff_t baseOffset =
            healthOffset < lifeStateOffset ? healthOffset : lifeStateOffset;
        const std::ptrdiff_t healthEnd =
            healthOffset + static_cast<std::ptrdiff_t>(sizeof(int));
        const std::ptrdiff_t lifeStateEnd =
            lifeStateOffset + static_cast<std::ptrdiff_t>(sizeof(uint8_t));
        const std::ptrdiff_t endOffset =
            healthEnd > lifeStateEnd ? healthEnd : lifeStateEnd;
        if (endOffset <= baseOffset)
            return {};

        const auto spanBytes = static_cast<size_t>(endOffset - baseOffset);
        if (spanBytes > maximumSpanBytes)
            return {};

        return {
            true,
            baseOffset,
            spanBytes,
            static_cast<size_t>(healthOffset - baseOffset),
            static_cast<size_t>(lifeStateOffset - baseOffset),
        };
    }

    inline bool DecodeCoreVitalRead(const CoreVitalReadLayout& layout,
        const uint8_t* bytes, size_t capacity, size_t bytesRead,
        int& health, uint8_t& lifeState) noexcept
    {
        if (!layout.coalesced || !bytes || layout.spanBytes > capacity ||
            bytesRead != layout.spanBytes || layout.spanBytes < sizeof(int) ||
            layout.healthDelta > layout.spanBytes - sizeof(int) ||
            layout.lifeStateDelta >= layout.spanBytes)
            return false;
        std::memcpy(&health, bytes + layout.healthDelta, sizeof(health));
        std::memcpy(&lifeState, bytes + layout.lifeStateDelta, sizeof(lifeState));
        return true;
    }

    inline bool IsPlayerCoreReadComplete(const PlayerCoreReadCompletion& read)
    {
        return read.healthBytes == sizeof(int) &&
               read.armorBytes == sizeof(int) &&
               read.lifeStateBytes == sizeof(uint8_t) &&
               read.positionBytes == sizeof(Vector3) &&
               (!read.teamReadRequired || read.teamBytes == sizeof(PlayerTeamSample));
    }

    inline bool IsPlayerCoreStatePlausible(
        bool teamValid,
        int health,
        int armor,
        uint8_t lifeState,
        bool positionValid) noexcept
    {
        const bool valuesInRange =
            health >= 0 && health <= 500 &&
            armor >= 0 && armor <= 500 &&
            lifeState <= 2;
        if (!teamValid || !valuesInRange)
            return false;

        // Dead and observer pawns may legitimately publish a zero origin. Their
        // completed health/life-state record is still coherent and must not
        // make the entire roster batch look unreadable.
        const bool alive = health > 0 && lifeState == 0;
        return !alive || positionValid;
    }

    inline uint64_t ElapsedSinceOrZero(uint64_t nowUs, uint64_t sinceUs)
    {
        return nowUs >= sinceUs ? nowUs - sinceUs : 0;
    }

    inline bool IsSamePendingLocalTeamSwitch(
        int pendingFrom,
        int currentFrom,
        int pendingTo,
        int currentTo,
        uint64_t pendingSinceUs,
        uint64_t nowUs)
    {
        return pendingFrom == currentFrom &&
               pendingTo == currentTo &&
               pendingSinceUs > 0 &&
               nowUs >= pendingSinceUs &&
               (nowUs - pendingSinceUs) <= kPendingLocalTeamSwitchWindowUs;
    }

    inline bool IsNewLocalTeamSwitchEdge(
        uint64_t lastHandledUs,
        int lastHandledFrom,
        int currentFrom,
        int lastHandledTo,
        int currentTo,
        uint64_t nowUs)
    {
        return lastHandledUs == 0 ||
               nowUs <= lastHandledUs ||
               (nowUs - lastHandledUs) > kLocalTeamSwitchEdgeCooldownUs ||
               lastHandledFrom != currentFrom ||
               lastHandledTo != currentTo;
    }

    inline esp::SubsystemHealthState EvaluatePlayerCoreHealth(
        bool engineInGame,
        int resolvedSlots,
        int plausibleSlots,
        bool recoveredScatterFailure,
        bool hardFailure)
    {
        if (!engineInGame || resolvedSlots <= 0)
            return esp::SubsystemHealthState::Unknown;
        if (hardFailure || plausibleSlots <= 0)
            return esp::SubsystemHealthState::Failed;
        if (recoveredScatterFailure ||
            plausibleSlots < resolvedSlots) {
            return esp::SubsystemHealthState::Degraded;
        }
        return esp::SubsystemHealthState::Healthy;
    }

}
