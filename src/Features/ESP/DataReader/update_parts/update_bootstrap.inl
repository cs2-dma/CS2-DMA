
    auto statusToString = [](esp::GameStatus status) -> const char* {
        switch (status) {
        case esp::GameStatus::Ok:      return "OK";
        case esp::GameStatus::WaitCs2: return "Wait cs2.exe";
        default:                       return "Unknown";
        }
    };

    auto logStatusTransition = [&](esp::GameStatus from, esp::GameStatus to, const char* reason) {
        if (from == to)
            return;
        DmaLogPrintf(
            "[INFO] GameStatus: %s -> %s (%s)",
            statusToString(from),
            statusToString(to),
            reason ? reason : "no-reason");
    };

    auto setSceneWarmupState = [&](esp::SceneWarmupState state) {
        SetSceneWarmupState(state);
    };

    auto clearAllState = [&](esp::recovery::ResetTrigger trigger) {
        const auto reset = esp::recovery::EvaluateResetPolicy(trigger);
        ResetRuntimeStateHard(reset.reason, reset.publishClearedSnapshot);
    };

    auto promoteInGameFrame = [&](const char* reason) {
        const auto curStatus = static_cast<esp::GameStatus>(s_gameStatus.load(std::memory_order_relaxed));
        s_requiredReadFailureCount = 0;
        if (curStatus != esp::GameStatus::Ok) {
            s_gameStatus.store(static_cast<uint8_t>(esp::GameStatus::Ok), std::memory_order_relaxed);
            logStatusTransition(curStatus, esp::GameStatus::Ok, reason);
        }
    };

    auto refreshDmaCaches = [&](const char* reason, DmaRefreshTier tier = DmaRefreshTier::Probe, bool force = false) {
        RefreshDmaCaches(reason, tier, force);
    };

    auto handleSceneTransition = [&](const char* reason,
                                     bool bumpMapEpoch,
                                     bool preserveLiveSnapshot,
                                     DmaRefreshTier refreshTier,
                                     bool refreshCaches) {
        if (preserveLiveSnapshot) {
            ResetRuntimeStateSoft(reason ? reason : "network_client_changed");
            // The old player frame may be held, but bomb/entity pointers belong
            // to the replaced network client and must not cross this boundary.
            s_bombEpoch.fetch_add(1, std::memory_order_relaxed);
        } else {
            clearAllState(esp::recovery::ResetTrigger::SceneTransition);
        }
        uint64_t transitionMapEpoch = s_mapEpoch.load(std::memory_order_relaxed);
        if (bumpMapEpoch) {
            transitionMapEpoch = s_mapEpoch.fetch_add(1, std::memory_order_relaxed) + 1u;
            s_mapFingerprint = 0;
            DmaLogPrintf(
                "[INFO] Scene transition: %s -> map epoch %llu",
                reason ? reason : "transition",
                static_cast<unsigned long long>(transitionMapEpoch));
        }
        setSceneWarmupState(esp::SceneWarmupState::SceneTransition);
        if (refreshCaches) {
            refreshDmaCaches(
                reason,
                refreshTier,
                false);
        }
    };

    if (!g::clientBase || !g::engine2Base) {
        // DataWorkerLoop owns missing-base supervision and probes attachment
        // every 250 ms. Raising a generic recovery request here changed that
        // path to the slower 3 s recovery cadence and kept process-wait mode
        // running at recovery frequency.
        MarkDmaReadSuccess();
        return false;
    }
    const uint64_t _stagePipelineStart = TickNowUs();
    static bool s_cachedWebRadarConsumerDemand = false;
    static uint64_t s_lastWebRadarConsumerDemandCheckUs = 0;
    const bool webRadarEnabledForDemand = wantsWebRadarEnabled || wantsWebRadarRemoteEnabled;
    if (!webRadarEnabledForDemand) {
        s_cachedWebRadarConsumerDemand = false;
        s_lastWebRadarConsumerDemandCheckUs = 0;
    } else if (s_lastWebRadarConsumerDemandCheckUs == 0 ||
               (_stagePipelineStart - s_lastWebRadarConsumerDemandCheckUs) >= 250000u) {
        s_cachedWebRadarConsumerDemand = webradar::HasActiveConsumers();
        s_lastWebRadarConsumerDemandCheckUs = _stagePipelineStart;
    }
    const bool webRadarDemandActive = webRadarEnabledForDemand && s_cachedWebRadarConsumerDemand;

    promoteInGameFrame("client_attached");
