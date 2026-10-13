#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>

namespace bootstrap
{
    inline constexpr auto kProcessPollInterval = std::chrono::milliseconds(100);
    inline constexpr auto kProcessCatalogRefreshInterval = std::chrono::milliseconds(250);
    inline constexpr uint32_t kProcessCatalogFullRefreshMisses = 4u;
    inline constexpr uint32_t kProcessCatalogHealthProbeMisses = 12u;
    inline constexpr uint32_t kProcessCatalogFailureLimit = 8u;
    inline constexpr auto kAttachRetryInterval = std::chrono::milliseconds(750);
    inline constexpr auto kModuleResolveInterval = std::chrono::milliseconds(50);
    inline constexpr auto kModuleFullRefreshDelay = std::chrono::milliseconds(750);
    inline constexpr auto kModuleReattachDelay = std::chrono::seconds(8);
    inline constexpr auto kDmaReinitializeCooldown = std::chrono::seconds(10);

    struct ProcessWaitSchedule
    {
        using Clock = std::chrono::steady_clock;
        Clock::time_point nextProcessPoll = {};
        Clock::time_point nextProcessCatalogRefresh = {};
        Clock::time_point nextAttachAttempt = {};
        Clock::time_point nextModuleResolve = {};
        Clock::time_point attachedAt = {};
        Clock::time_point lastDmaReinitialize = {};

        bool PollDue(Clock::time_point now) const noexcept
        {
            return now >= nextProcessPoll || now >= nextProcessCatalogRefresh;
        }

        Clock::time_point NextWake(
            Clock::time_point now, bool processAttached, bool hasSelectedPid) const noexcept
        {
            auto wake = (std::min)({now + kProcessPollInterval,
                nextProcessPoll, nextProcessCatalogRefresh});
            if (processAttached)
                wake = (std::min)(wake, nextModuleResolve);
            else if (hasSelectedPid)
                wake = (std::min)(wake, nextAttachAttempt);
            return (std::max)(wake, now);
        }

        bool CanReinitialize(Clock::time_point now) const noexcept
        {
            return lastDmaReinitialize == Clock::time_point{} ||
                now < lastDmaReinitialize ||
                now - lastDmaReinitialize >= kDmaReinitializeCooldown;
        }
    };

    inline bool ShouldReinitializeProcessDiscovery(
        bool handleAvailable, bool processAttached, uint32_t selectedPid,
        uint32_t catalogRefreshFailures, bool catalogUnavailable,
        bool moduleResolveTimedOut) noexcept
    {
        return moduleResolveTimedOut || !handleAvailable ||
            (!processAttached && selectedPid == 0 &&
                (catalogRefreshFailures >= kProcessCatalogFailureLimit || catalogUnavailable));
    }
}
