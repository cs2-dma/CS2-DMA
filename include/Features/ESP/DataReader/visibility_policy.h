#pragma once

#include <cstdint>

namespace esp::data
{
    enum class VisibilityDiagnosticState : uint8_t {
        Disabled = 0,
        Waiting,
        Healthy,
        Unavailable,
        Stale,
    };

    inline constexpr uint64_t kVisibilityHealthyAgeUs = 100000u;
    inline constexpr uint64_t kVisibilityStaleAgeUs = 250000u;

    struct VisibilityResolveInput
    {
        bool spottedReadFresh = false;
        uint64_t spottedMask = 0;
        bool crosshairTarget = false;
        bool localMaskResolved = false;
        int localMaskBit = -1;
        int localMaskSlotBit = -1;
        int localHandleSlotBit = -1;
        int localControllerMaskBit = -1;
    };

    struct VisibilityResolveResult
    {
        bool hasFreshState = false;
        bool visible = false;
    };

    inline bool IsSpottedMaskBitSet(uint64_t mask, int bit)
    {
        if (bit < 0 || bit >= 64)
            return false;
        return ((mask >> bit) & 1ULL) != 0ULL;
    }

    inline uint64_t BuildSpottedMask(uint32_t lowMask, uint32_t highMask)
    {
        return (static_cast<uint64_t>(highMask) << 32) |
               static_cast<uint64_t>(lowMask);
    }

    inline bool IsSpottedStateReadComplete(
        bool requested,
        uint32_t maskBytes,
        uint32_t expectedMaskBytes)
    {
        return requested && maskBytes == expectedMaskBytes;
    }

    inline bool IsVisibleByLocalSpottedMask(
        uint64_t spottedMask,
        int localMaskBit,
        int localMaskSlotBit,
        int localHandleSlotBit,
        int localControllerMaskBit)
    {
        if (spottedMask == 0ULL)
            return false;

        return IsSpottedMaskBitSet(spottedMask, localMaskBit) ||
               IsSpottedMaskBitSet(spottedMask, localMaskSlotBit) ||
               IsSpottedMaskBitSet(spottedMask, localHandleSlotBit) ||
               IsSpottedMaskBitSet(spottedMask, localControllerMaskBit);
    }

    inline VisibilityResolveResult ResolveVisibilityFromSpotted(const VisibilityResolveInput& input)
    {
        if (!input.spottedReadFresh && !input.crosshairTarget)
            return {};
        if (!input.localMaskResolved && !input.crosshairTarget)
            return {};

        const bool visible = input.crosshairTarget ||
            IsVisibleByLocalSpottedMask(
            input.spottedMask,
            input.localMaskBit,
            input.localMaskSlotBit,
            input.localHandleSlotBit,
            input.localControllerMaskBit);

        return {
            true,
            visible,
        };
    }

    inline VisibilityDiagnosticState ResolveVisibilityDiagnosticState(
        bool enabled,
        int activePlayers,
        bool localMaskResolved,
        int freshSlots,
        uint64_t commitAgeUs)
    {
        if (!enabled)
            return VisibilityDiagnosticState::Disabled;
        if (activePlayers <= 1)
            return VisibilityDiagnosticState::Waiting;
        if (freshSlots > 0 && commitAgeUs <= kVisibilityHealthyAgeUs)
            return VisibilityDiagnosticState::Healthy;
        if (commitAgeUs > kVisibilityStaleAgeUs)
            return VisibilityDiagnosticState::Stale;
        if (!localMaskResolved && commitAgeUs > kVisibilityHealthyAgeUs)
            return VisibilityDiagnosticState::Unavailable;
        return VisibilityDiagnosticState::Waiting;
    }
}
