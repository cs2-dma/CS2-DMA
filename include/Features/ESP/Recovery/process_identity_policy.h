#pragma once

#include <cstdint>

namespace esp::recovery
{
    inline constexpr uint32_t kProcessIdentityConfirmationSamples = 2u;
    inline constexpr uint64_t kProcessIdentityConfirmationAgeUs = 100000u;

    enum class ProcessIdentityDecision : uint8_t {
        NoProcess = 0,
        Stable,
        PendingConfirmation,
        ConfirmedNewProcess,
        ConfirmedProcessLost,
        ConfirmedReplacement,
    };

    struct ProcessIdentityTracker {
        uint32_t candidatePid = 0;
        uint32_t observationCount = 0;
        uint64_t firstObservedUs = 0;
    };

    inline void ResetProcessIdentityCandidate(ProcessIdentityTracker& tracker)
    {
        tracker = {};
    }

    inline ProcessIdentityDecision ObserveProcessIdentity(
        ProcessIdentityTracker& tracker,
        uint32_t trackedPid,
        uint32_t observedPid,
        uint64_t nowUs,
        uint32_t requiredSamples = kProcessIdentityConfirmationSamples,
        uint64_t minimumAgeUs = kProcessIdentityConfirmationAgeUs)
    {
        if (trackedPid != 0 && observedPid == trackedPid) {
            ResetProcessIdentityCandidate(tracker);
            return ProcessIdentityDecision::Stable;
        }
        if (trackedPid == 0 && observedPid == 0) {
            ResetProcessIdentityCandidate(tracker);
            return ProcessIdentityDecision::NoProcess;
        }

        if (tracker.observationCount == 0 ||
            tracker.candidatePid != observedPid ||
            nowUs < tracker.firstObservedUs) {
            tracker.candidatePid = observedPid;
            tracker.observationCount = 1;
            tracker.firstObservedUs = nowUs;
        } else {
            ++tracker.observationCount;
        }

        const uint32_t samplesNeeded = requiredSamples == 0 ? 1u : requiredSamples;
        const bool oldEnough =
            nowUs >= tracker.firstObservedUs &&
            (nowUs - tracker.firstObservedUs) >= minimumAgeUs;
        if (tracker.observationCount < samplesNeeded || !oldEnough)
            return ProcessIdentityDecision::PendingConfirmation;

        ResetProcessIdentityCandidate(tracker);
        if (observedPid == 0)
            return ProcessIdentityDecision::ConfirmedProcessLost;
        if (trackedPid == 0)
            return ProcessIdentityDecision::ConfirmedNewProcess;
        return ProcessIdentityDecision::ConfirmedReplacement;
    }
}
