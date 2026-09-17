#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace esp::worker
{
    // Engine activity, not local pawn existence, controls camera reads. Dead
    // players and spectators still need a camera; background menus do not.
    inline bool ShouldReadLiveCamera(bool hasClientBase, bool resolved,
        bool inGame, bool menu, bool recovering) noexcept
    {
        return hasClientBase && resolved && inGame && !menu && !recovering;
    }

    inline int DataWorkerTargetHz(bool recovering, bool hasBases,
        bool resolved, bool inGame, bool menu, int liveHz) noexcept
    {
        if (recovering) return 220;
        if (!hasBases) return 45;
        if (resolved && menu) return 90;
        if (resolved && inGame) return liveHz;
        return 180;
    }

    inline constexpr int kCameraIdlePollHz = 45;

    template <std::size_t N>
    struct CounterIntervalSample {
        std::array<uint64_t, N> counts = {};
        uint64_t durationUs = 0;
        uint64_t completedAtUs = 0;
        bool valid = false;
    };

    // Single-writer sampler of monotonic totals. The original totals are never
    // reset; a counter/clock reset invalidates just this interval.
    template <std::size_t N>
    class CounterInterval {
    public:
        bool Due(uint64_t nowUs) const noexcept
        {
            return !initialized_ || nowUs < baselineUs_ ||
                nowUs - baselineUs_ >= 1000000u;
        }

        CounterIntervalSample<N> Observe(
            const std::array<uint64_t, N>& totals, uint64_t nowUs) noexcept
        {
            if (!Due(nowUs)) return sample_;
            bool reset = !initialized_ || nowUs <= baselineUs_;
            for (std::size_t i = 0; i < N; ++i)
                reset = reset || totals[i] < baseline_[i];
            sample_ = {};
            if (!reset) {
                for (std::size_t i = 0; i < N; ++i)
                    sample_.counts[i] = totals[i] - baseline_[i];
                sample_.durationUs = nowUs - baselineUs_;
                sample_.completedAtUs = nowUs;
                sample_.valid = true;
            }
            baseline_ = totals;
            baselineUs_ = nowUs;
            initialized_ = true;
            return sample_;
        }

    private:
        std::array<uint64_t, N> baseline_ = {};
        CounterIntervalSample<N> sample_ = {};
        uint64_t baselineUs_ = 0;
        bool initialized_ = false;
    };

    inline constexpr uint64_t kCameraSnapshotFreshUs = 500000u;
    inline constexpr uint64_t kCameraSelfHealCooldownUs = 250000u;
    inline constexpr uint64_t kCameraStallCycleUs = 15000u;
    inline constexpr uint64_t kWorkerStallLogCooldownUs = 1000000u;
    inline constexpr uint64_t kWorkerRecentPeakWindowUs = 10000000u;
    inline constexpr uint64_t kDataSupervisorIntervalUs = 20000u;
    inline constexpr uint32_t kCameraPersistentRecoveryMissMultiplier = 4u;
    inline constexpr uint64_t kLiveHistoryWindowUs = 15000000u;
    inline constexpr uint32_t kWorkerRestartInitialBackoffMs = 100u;
    inline constexpr uint32_t kWorkerRestartMaxBackoffMs = 2000u;
    inline constexpr std::array<uint64_t, 13> kCycleLatencyUpperBoundsUs = {
        500u, 750u, 1000u, 1250u, 1500u, 2000u,
        2500u, 3334u, 4000u, 5000u, 8334u, 16667u, 1000000u,
    };

    inline std::size_t LatencyBucketIndex(uint64_t durationUs) noexcept
    {
        for (std::size_t i = 0; i < kCycleLatencyUpperBoundsUs.size(); ++i) {
            if (durationUs <= kCycleLatencyUpperBoundsUs[i])
                return i;
        }
        return kCycleLatencyUpperBoundsUs.size() - 1u;
    }

    inline uint64_t LatencyPercentileUpperBound(
        const std::array<uint64_t, kCycleLatencyUpperBoundsUs.size()>& buckets,
        uint64_t sampleCount,
        uint32_t percentile) noexcept
    {
        if (sampleCount == 0)
            return 0;
        const uint64_t rank =
            (sampleCount * static_cast<uint64_t>(percentile) + 99u) / 100u;
        uint64_t cumulative = 0;
        for (std::size_t i = 0; i < buckets.size(); ++i) {
            cumulative += buckets[i];
            if (cumulative >= rank)
                return kCycleLatencyUpperBoundsUs[i];
        }
        return kCycleLatencyUpperBoundsUs.back();
    }

    inline bool IsWorkerCooldownElapsed(uint64_t lastUs, uint64_t nowUs, uint64_t cooldownUs)
    {
        return lastUs == 0 ||
               nowUs <= lastUs ||
               (nowUs - lastUs) >= cooldownUs;
    }

    inline bool IsRecentWorkerTimestamp(uint64_t lastUs, uint64_t nowUs, uint64_t windowUs)
    {
        return lastUs > 0 &&
               nowUs >= lastUs &&
               (nowUs - lastUs) <= windowUs;
    }

    inline bool IsCameraSampleFresh(uint64_t lastSuccessfulReadUs, uint64_t nowUs)
    {
        return IsRecentWorkerTimestamp(
            lastSuccessfulReadUs,
            nowUs,
            kCameraSnapshotFreshUs);
    }

    inline bool ShouldRunCameraSelfHeal(
        uint32_t consecutiveViewMisses,
        uint32_t recoveryMissThreshold,
        bool cameraRecoveryAllowed,
        uint64_t lastSelfHealUs,
        uint64_t nowUs)
    {
        return cameraRecoveryAllowed &&
               consecutiveViewMisses >= recoveryMissThreshold &&
               IsWorkerCooldownElapsed(lastSelfHealUs, nowUs, kCameraSelfHealCooldownUs);
    }

    inline bool ShouldRequestPersistentCameraRecovery(
        bool snapshotFresh,
        uint32_t consecutiveViewMisses,
        uint32_t recoveryMissThreshold)
    {
        return !snapshotFresh &&
               consecutiveViewMisses >= (recoveryMissThreshold * kCameraPersistentRecoveryMissMultiplier);
    }

    inline bool ShouldRequestCameraExceptionRecovery(
        bool engineInGame,
        bool sceneStable,
        bool snapshotFresh)
    {
        return engineInGame && sceneStable && !snapshotFresh;
    }

    inline bool ShouldLogWorkerStall(uint64_t cycleUs, uint64_t lastLogUs, uint64_t nowUs)
    {
        return cycleUs > kCameraStallCycleUs &&
               IsWorkerCooldownElapsed(lastLogUs, nowUs, kWorkerStallLogCooldownUs);
    }

    inline bool IsCameraPauseOrphaned(
        uint32_t pauseRequests,
        bool adminPauseActive,
        bool dmaRecovering)
    {
        return pauseRequests != 0u &&
               !adminPauseActive &&
               !dmaRecovering;
    }

    inline bool IsDataSupervisorDue(uint64_t lastRunUs, uint64_t nowUs)
    {
        return IsWorkerCooldownElapsed(
            lastRunUs,
            nowUs,
            kDataSupervisorIntervalUs);
    }

    inline uint32_t WorkerRestartBackoffMs(uint32_t consecutiveFailures)
    {
        uint32_t delayMs = kWorkerRestartInitialBackoffMs;
        for (uint32_t failure = 1u;
             failure < consecutiveFailures && delayMs < kWorkerRestartMaxBackoffMs;
             ++failure) {
            if (delayMs > (kWorkerRestartMaxBackoffMs / 2u))
                return kWorkerRestartMaxBackoffMs;
            delayMs *= 2u;
        }
        return delayMs;
    }

    template <typename TimePoint, typename Duration>
    inline bool IsWorkerScheduleSkipped(
        const TimePoint& previousSampleStart,
        const TimePoint& currentSampleStart,
        const Duration& interval)
    {
        // Call this with consecutive sample start times. A cycle that merely
        // exceeds its budget is represented by the over-budget counters; count
        // only a gap large enough to lose a complete sample opportunity.
        return previousSampleStart + interval + interval <= currentSampleStart;
    }

    template <typename TimePoint, typename Duration>
    inline TimePoint NextWorkerDeadline(
        const TimePoint& previousDeadline,
        const TimePoint& now,
        const Duration& interval)
    {
        const TimePoint scheduled = previousDeadline + interval;
        if (scheduled > now)
            return scheduled;

        // Preserve the fixed-rate cadence after a small overrun. Adding a full
        // interval here turns a 3.7 ms cycle into a ~7 ms loop. After a larger
        // stall, rebase at "now" so the worker does not run an unbounded burst
        // of catch-up iterations.
        return (now - scheduled) >= interval ? now : scheduled;
    }
}
