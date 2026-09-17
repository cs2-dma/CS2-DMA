#pragma once

#include <cstdint>
#include <limits>

namespace esp::data
{
    inline constexpr uint64_t kBulkRecoveryStaleHoldWindowUs = 6000000u;
    inline constexpr uint64_t kBulkRecoveryStaleEvictionMs = 4000u;

    enum class PlayerSlotStaleAction : uint8_t {
        Ignore = 0,
        Refresh,
        Hold,
        Evict,
    };

    struct PlayerSlotStaleInput {
        bool refreshed = false;
        bool valid = false;
        uint64_t nowMs = 0;
        uint64_t lastSeenMs = 0;
        uint64_t normalEvictionMs = 0;
        bool inBulkRecovery = false;
        int staleFrames = 0;
    };

    struct PlayerSlotStaleResult {
        PlayerSlotStaleAction action = PlayerSlotStaleAction::Ignore;
        uint64_t elapsedMs = 0;
        uint64_t evictionWindowMs = 0;
        int nextStaleFrames = 0;
    };

    inline bool IsWithinBulkRecoveryStaleWindow(uint64_t lastBulkEvictionUs, uint64_t nowUs)
    {
        return lastBulkEvictionUs > 0 &&
               nowUs > lastBulkEvictionUs &&
               (nowUs - lastBulkEvictionUs) <= kBulkRecoveryStaleHoldWindowUs;
    }

    inline uint64_t SelectPlayerStaleEvictionWindowMs(
        uint64_t normalEvictionMs,
        bool inBulkRecovery)
    {
        return inBulkRecovery ? kBulkRecoveryStaleEvictionMs : normalEvictionMs;
    }

    inline PlayerSlotStaleResult EvaluatePlayerSlotStalePolicy(const PlayerSlotStaleInput& input)
    {
        const uint64_t evictionWindowMs =
            SelectPlayerStaleEvictionWindowMs(input.normalEvictionMs, input.inBulkRecovery);

        if (input.refreshed) {
            return {
                PlayerSlotStaleAction::Refresh,
                0,
                evictionWindowMs,
                0,
            };
        }

        if (!input.valid) {
            return {
                PlayerSlotStaleAction::Ignore,
                0,
                evictionWindowMs,
                input.staleFrames,
            };
        }

        const uint64_t elapsedMs =
            (input.nowMs > input.lastSeenMs) ? (input.nowMs - input.lastSeenMs) : 0;
        const int nextStaleFrames =
            input.staleFrames < std::numeric_limits<int>::max()
                ? input.staleFrames + 1
                : input.staleFrames;
        return {
            elapsedMs > evictionWindowMs ? PlayerSlotStaleAction::Evict
                                         : PlayerSlotStaleAction::Hold,
            elapsedMs,
            evictionWindowMs,
            nextStaleFrames,
        };
    }
}
