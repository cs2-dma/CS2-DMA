#pragma once

#include <algorithm>
#include <chrono>
#include <type_traits>

namespace bootstrap
{
    enum class OffsetAttempt { Ready, Retry, Failed };
    enum class OffsetWaitResult { Ready, Failed, TimedOut, Cancelled };

    enum class OffsetRecovery { None, MemoryAndTranslation, Full };
    enum class OffsetCache { Memory, Translation, All };

    template <typename Refresh>
    bool RecoverOffsetReads(OffsetRecovery action, Refresh&& refresh)
    {
        if (action == OffsetRecovery::MemoryAndTranslation) {
            const bool memory = refresh(OffsetCache::Memory);
            const bool translation = refresh(OffsetCache::Translation);
            return memory && translation;
        }
        return action != OffsetRecovery::Full || refresh(OffsetCache::All);
    }

    struct OffsetRecoveryPlan
    {
        using Clock = std::chrono::steady_clock;
        unsigned retries = 0;
        bool memoryRefreshed = false;
        bool fullRefreshed = false;
        Clock::time_point memoryRefreshAt = {};

        OffsetRecovery BeforeRetry(Clock::time_point now)
        {
            if (retries < 4) ++retries;
            if (!memoryRefreshed && retries >= 2) {
                memoryRefreshed = true;
                memoryRefreshAt = now;
                return OffsetRecovery::MemoryAndTranslation;
            }
            if (memoryRefreshed && !fullRefreshed && retries >= 3 &&
                now >= memoryRefreshAt && now - memoryRefreshAt >= std::chrono::seconds(2)) {
                fullRefreshed = true;
                return OffsetRecovery::Full;
            }
            return OffsetRecovery::None;
        }
    };

    template <typename Attempt, typename Clock, typename Sleep, typename Cancel>
    OffsetWaitResult WaitForRuntimeOffsets(
        Attempt&& attempt, Clock&& now, Sleep&& sleep, Cancel&& cancelled,
        std::chrono::milliseconds budget = std::chrono::seconds(60))
    {
        const auto started = now();
        bool force = false;
        for (;;) {
            if (cancelled()) return OffsetWaitResult::Cancelled;
            if (now() < started || now() - started >= budget)
                return OffsetWaitResult::TimedOut;
            const auto result = [&] {
                if constexpr (std::is_invocable_v<Attempt&, bool, decltype(started)>)
                    return attempt(force, started + budget);
                else
                    return attempt(force);
            }();
            if (cancelled()) return OffsetWaitResult::Cancelled;
            const auto current = now();
            if (current < started || current - started >= budget)
                return OffsetWaitResult::TimedOut;
            if (result == OffsetAttempt::Ready) return OffsetWaitResult::Ready;
            if (result == OffsetAttempt::Failed) return OffsetWaitResult::Failed;
            const auto retryAt = (std::min)(current + std::chrono::seconds(1), started + budget);
            while (now() < retryAt) {
                if (cancelled()) return OffsetWaitResult::Cancelled;
                const auto beforeSleep = now();
                if (beforeSleep < started) return OffsetWaitResult::TimedOut;
                if (beforeSleep >= retryAt) break;
                const auto remaining = std::chrono::ceil<std::chrono::milliseconds>(retryAt - beforeSleep);
                sleep((std::min)(std::chrono::milliseconds(100), remaining));
            }
            force = true;
        }
    }
}
