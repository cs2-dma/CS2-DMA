#pragma once

#include <cstdint>

namespace esp::data
{
    inline constexpr uint32_t kMatchExitConfirmationSamples = 5u;
    inline constexpr uint32_t kNetworkClientConfirmationSamples = 4u;
    inline constexpr uint64_t kMatchExitConfirmationUs = 250000u;
    inline constexpr uint64_t kNetworkClientConfirmationUs = 250000u;
    inline constexpr uint64_t kZeroNetworkClientConfirmationUs = 500000u;
    inline constexpr uint64_t kNonLiveSignOnConfirmationUs = 250000u;
    inline constexpr uint64_t kEngineStateReadGapHoldUs = 500000u;
    inline constexpr uint64_t kEngineResolveProbeAgeUs = 1000000u;
    inline constexpr uint64_t kEngineResolveRepairAgeUs = 5000000u;
    inline constexpr uint64_t kEngineResolveRecoveryAgeUs = 15000000u;

    enum class EngineResolveMissAction : uint8_t {
        None = 0,
        Probe,
        Repair,
        Recovery,
    };

    inline constexpr uint8_t EngineResolveMissActionBit(EngineResolveMissAction action)
    {
        switch (action) {
        case EngineResolveMissAction::Probe:
            return 1u << 0;
        case EngineResolveMissAction::Repair:
            return 1u << 1;
        case EngineResolveMissAction::Recovery:
            return 1u << 2;
        case EngineResolveMissAction::None:
        default:
            return 0u;
        }
    }

    inline EngineResolveMissAction SelectEngineResolveMissAction(
        uint64_t missAgeUs,
        uint8_t completedActions)
    {
        if (missAgeUs >= kEngineResolveProbeAgeUs &&
            (completedActions & EngineResolveMissActionBit(EngineResolveMissAction::Probe)) == 0u) {
            return EngineResolveMissAction::Probe;
        }

        if (missAgeUs >= kEngineResolveRepairAgeUs &&
            (completedActions & EngineResolveMissActionBit(EngineResolveMissAction::Repair)) == 0u) {
            return EngineResolveMissAction::Repair;
        }

        if (missAgeUs >= kEngineResolveRecoveryAgeUs &&
            (completedActions & EngineResolveMissActionBit(EngineResolveMissAction::Recovery)) == 0u) {
            return EngineResolveMissAction::Recovery;
        }

        return EngineResolveMissAction::None;
    }

    inline bool IsEngineStateConfirmationElapsed(
        uint64_t candidateSinceUs,
        uint64_t nowUs,
        uint64_t confirmationUs)
    {
        return candidateSinceUs > 0 &&
               nowUs >= candidateSinceUs &&
               (nowUs - candidateSinceUs) >= confirmationUs;
    }

    inline bool IsValidSignOnState(int32_t value)
    {
        return value >= 0 && value <= 12;
    }

    inline int32_t SelectSignOnStateCandidate(
        int32_t primary,
        int32_t fallbackA,
        int32_t fallbackB,
        int32_t /*maxClients*/,
        uint8_t /*backgroundMap*/,
        bool /*cachedInGame*/)
    {
        // The configured field wins, including valid disconnect/loading values.
        // An unrelated legacy-offset value of 6 must not latch a match forever.
        if (IsValidSignOnState(primary))
            return primary;
        if (IsValidSignOnState(fallbackA))
            return fallbackA;
        if (IsValidSignOnState(fallbackB))
            return fallbackB;
        return -1;
    }

    inline bool IsCompleteEngineActivitySample(
        int32_t signOn, int32_t maxClients, uint8_t backgroundMap,
        uint32_t maxClientsBytes, uint32_t backgroundBytes) noexcept
    {
        return IsValidSignOnState(signOn) && maxClients >= 0 && maxClients <= 256 &&
            backgroundMap <= 1 && maxClientsBytes == sizeof(maxClients) &&
            backgroundBytes == sizeof(backgroundMap);
    }

    inline bool IsLiveEngineActivitySample(
        int32_t signOn, int32_t maxClients, uint8_t backgroundMap) noexcept
    {
        // One-client workshop/offline servers are still gameplay. Background
        // map is the menu discriminator; zero capacity is not invented as 64.
        return signOn == 6 && maxClients >= 1 && maxClients <= 256 && backgroundMap == 0;
    }

    inline bool HoldTransientEngineExit(bool currentLive, bool previousLive,
        bool sameClient, uint64_t& nonLiveSinceUs, uint64_t nowUs) noexcept
    {
        if (currentLive || !previousLive || !sameClient) {
            nonLiveSinceUs = 0;
            return false;
        }
        if (nonLiveSinceUs == 0 || nowUs < nonLiveSinceUs)
            nonLiveSinceUs = nowUs;
        return !IsEngineStateConfirmationElapsed(
            nonLiveSinceUs, nowUs, kNonLiveSignOnConfirmationUs);
    }

    inline bool IsEngineStateCacheFresh(uint64_t sampleUs, uint64_t nowUs) noexcept
    {
        return sampleUs > 0 && nowUs >= sampleUs && nowUs - sampleUs <= kEngineStateReadGapHoldUs;
    }

    enum class SceneTransitionReason : uint8_t {
        None = 0,
        MatchEnter,
        MatchExit,
        NetworkClientChanged,
    };

    struct SceneTransitionState {
        bool initialized = false;
        bool committedMatchLike = false;
        uintptr_t committedNetworkClient = 0;
        bool pending = false;
        bool pendingMatchLike = false;
        uintptr_t pendingNetworkClient = 0;
        uint32_t pendingCount = 0;
        uint64_t pendingSinceUs = 0;
        uint64_t sessionGeneration = 0;
    };

    struct SceneTransitionDecision {
        SceneTransitionReason reason = SceneTransitionReason::None;
        bool transition = false;
        bool bumpMapEpoch = false;
        bool preserveLiveSnapshot = false;
        bool refreshCaches = false;
    };

    inline void ClearPendingSceneTransition(SceneTransitionState& state)
    {
        state.pending = false;
        state.pendingMatchLike = false;
        state.pendingNetworkClient = 0;
        state.pendingCount = 0;
        state.pendingSinceUs = 0;
    }

    inline const char* SceneTransitionReasonName(SceneTransitionReason reason)
    {
        switch (reason) {
        case SceneTransitionReason::MatchEnter:
            return "engine_match_enter";
        case SceneTransitionReason::MatchExit:
            return "engine_match_exit";
        case SceneTransitionReason::NetworkClientChanged:
            return "network_client_changed";
        case SceneTransitionReason::None:
        default:
            return "none";
        }
    }

    inline SceneTransitionDecision ObserveSceneTransition(
        SceneTransitionState& state,
        bool matchLike,
        uintptr_t networkClient,
        uint64_t sessionGeneration,
        uint64_t nowUs,
        bool inactiveAlreadyConfirmed = false)
    {
        if (!state.initialized) {
            state.initialized = true;
            state.committedMatchLike = false;
            state.committedNetworkClient = networkClient;
            state.sessionGeneration = sessionGeneration;
        } else if (state.sessionGeneration != sessionGeneration) {
            state.committedMatchLike = matchLike;
            state.committedNetworkClient = networkClient;
            state.sessionGeneration = sessionGeneration;
            ClearPendingSceneTransition(state);
            return {};
        }

        if (state.committedNetworkClient == 0 && networkClient != 0)
            state.committedNetworkClient = networkClient;

        const bool matchLikeChanged =
            matchLike != state.committedMatchLike;
        const bool networkClientChanged =
            state.committedNetworkClient != 0 &&
            networkClient != 0 &&
            networkClient != state.committedNetworkClient;
        if (!matchLikeChanged && !networkClientChanged) {
            ClearPendingSceneTransition(state);
            return {};
        }

        const SceneTransitionReason reason =
            matchLikeChanged
                ? (matchLike
                       ? SceneTransitionReason::MatchEnter
                       : SceneTransitionReason::MatchExit)
                : SceneTransitionReason::NetworkClientChanged;
        const bool requiresConfirmation =
            !(matchLikeChanged && !matchLike && inactiveAlreadyConfirmed) &&
            ((matchLikeChanged && !matchLike) || networkClientChanged);

        if (requiresConfirmation) {
            const bool sameCandidate =
                state.pending &&
                state.pendingMatchLike == matchLike &&
                state.pendingNetworkClient == networkClient;
            if (!sameCandidate) {
                state.pending = true;
                state.pendingMatchLike = matchLike;
                state.pendingNetworkClient = networkClient;
                state.pendingCount = 1;
                state.pendingSinceUs = nowUs;
                return {};
            }

            ++state.pendingCount;
            const uint32_t confirmationsNeeded =
                networkClientChanged
                    ? kNetworkClientConfirmationSamples
                    : kMatchExitConfirmationSamples;
            const uint64_t confirmationAgeUs =
                state.pendingSinceUs > 0 && nowUs >= state.pendingSinceUs
                    ? nowUs - state.pendingSinceUs
                    : 0u;
            const uint64_t confirmationDurationUs =
                networkClientChanged
                    ? kNetworkClientConfirmationUs
                    : kMatchExitConfirmationUs;
            if (state.pendingCount < confirmationsNeeded ||
                confirmationAgeUs < confirmationDurationUs) {
                return {};
            }
        }

        state.committedMatchLike = matchLike;
        if (networkClient != 0)
            state.committedNetworkClient = networkClient;
        ClearPendingSceneTransition(state);
        return {
            reason,
            true,
            networkClientChanged ||
                reason == SceneTransitionReason::MatchEnter,
            reason == SceneTransitionReason::NetworkClientChanged && matchLike,
            (networkClientChanged && matchLike) ||
                reason == SceneTransitionReason::MatchEnter,
        };
    }
}
