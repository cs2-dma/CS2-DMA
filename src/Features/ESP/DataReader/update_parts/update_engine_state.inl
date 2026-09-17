    const auto& ofs = runtime_offsets::Get();
    auto isLikelyGamePointer = [](uintptr_t ptr) -> bool {
        return app::memory_address::IsLikelyGamePointer(ptr);
    };
    bool engineSignonResolved = false;
    bool engineSignonInGame = false;
    bool engineSignonMenu = false;
    int32_t engineSignonState = -1;
    int32_t engineLocalPlayerSlot = -1;
    int32_t engineMaxClients = 0;
    uint8_t engineBackgroundMap = 0;
    uintptr_t engine2Base = g::engine2Base;
    static bool s_cachedEngineResolved = false;
    static bool s_cachedEngineMenu = false;
    static bool s_cachedEngineInGame = false;
    static int32_t s_cachedEngineSignOnState = -1;
    static int32_t s_cachedEngineLocalPlayerSlot = -1;
    static int32_t s_cachedEngineMaxClients = 0;
    static uint8_t s_cachedEngineBackgroundMap = 0;
    static uint64_t s_engineResolveMissSinceUs = 0;
    static uint8_t s_engineResolveCompletedActions = 0;
    static uint64_t s_lastEngineResolveAttemptUs = 0;
    static uint64_t s_lastEngineSuccessUs = 0;
    static uint64_t s_engineCacheSessionGeneration = 0;
    const char* engineDebugSource = "fresh";

    static uintptr_t s_cachedNetworkGameClient = 0;
    static uint64_t s_zeroNetworkGameClientSinceUs = 0;
    static uint64_t s_nonLiveSignOnSinceUs = 0;
    const uint64_t engineNowUs = TickNowUs();
    const uint64_t engineSessionGeneration =
        s_dmaSessionGeneration.load(std::memory_order_relaxed);
    if (s_engineCacheSessionGeneration != engineSessionGeneration) {
        s_engineCacheSessionGeneration = engineSessionGeneration;
        s_cachedEngineResolved = false;
        s_cachedEngineMenu = false;
        s_cachedEngineInGame = false;
        s_cachedEngineSignOnState = -1;
        s_cachedEngineLocalPlayerSlot = -1;
        s_cachedEngineMaxClients = 0;
        s_cachedEngineBackgroundMap = 0;
        s_cachedNetworkGameClient = 0;
        s_zeroNetworkGameClientSinceUs = 0;
        s_nonLiveSignOnSinceUs = 0;
        s_engineResolveMissSinceUs = 0;
        s_engineResolveCompletedActions = 0;
        s_lastEngineResolveAttemptUs = 0;
        s_lastEngineSuccessUs = 0;
    }

    auto useCachedEngineState = [&]() {
        engineSignonResolved = true;
        engineSignonMenu = s_cachedEngineMenu;
        engineSignonInGame = s_cachedEngineInGame;
        engineSignonState = s_cachedEngineSignOnState;
        engineLocalPlayerSlot = s_cachedEngineLocalPlayerSlot;
        engineMaxClients = s_cachedEngineMaxClients;
        engineBackgroundMap = s_cachedEngineBackgroundMap;
    };

    auto tryResolveEngineState = [&]() -> bool {
        if (!engine2Base)
            engine2Base = mem.GetModuleBase("engine2.dll");
        if (!engine2Base || ofs.dwNetworkGameClient <= 0)
            return false;

        g::engine2Base = engine2Base;

        const std::ptrdiff_t signOffsets[3] = {
            ofs.dwNetworkGameClient_signOnState,
            static_cast<std::ptrdiff_t>(0x230),
            static_cast<std::ptrdiff_t>(0x228)
        };
        uintptr_t networkGameClient = 0;
        int32_t signOnCandidates[3] = { -1, -1, -1 };
        int32_t rawLocalPlayerSlot = -1;
        int32_t rawMaxClients = 0;
        uint8_t rawBackgroundMap = 0;
        DWORD rootBytes = 0;
        DWORD signBytes[3] = {};
        DWORD localSlotBytes = 0, maxClientsBytes = 0, backgroundBytes = 0;
        const uintptr_t speculativeNetworkGameClient =
            isLikelyGamePointer(s_cachedNetworkGameClient)
                ? s_cachedNetworkGameClient
                : 0;
        bool dependentStateRead = false;
        {
            if (!s_engineScatterHandle)
                s_engineScatterHandle = CreateTrackedScatterHandle(mem);
            if (!s_engineScatterHandle)
                return false;
            mem.AddScatterReadRequest(
                s_engineScatterHandle,
                engine2Base + ofs.dwNetworkGameClient,
                &networkGameClient,
                sizeof(networkGameClient), &rootBytes);
            if (speculativeNetworkGameClient) {
                for (int i = 0; i < 3; ++i) {
                    if (signOffsets[i] > 0) {
                        mem.AddScatterReadRequest(
                            s_engineScatterHandle,
                            speculativeNetworkGameClient +
                                static_cast<uintptr_t>(signOffsets[i]),
                            &signOnCandidates[i],
                            sizeof(int32_t), &signBytes[i]);
                    }
                }
                if (ofs.dwNetworkGameClient_localPlayer > 0) {
                    mem.AddScatterReadRequest(
                        s_engineScatterHandle,
                        speculativeNetworkGameClient +
                            ofs.dwNetworkGameClient_localPlayer,
                        &rawLocalPlayerSlot,
                        sizeof(int32_t), &localSlotBytes);
                }
                if (ofs.dwNetworkGameClient_maxClients > 0) {
                    mem.AddScatterReadRequest(
                        s_engineScatterHandle,
                        speculativeNetworkGameClient +
                            ofs.dwNetworkGameClient_maxClients,
                        &rawMaxClients,
                        sizeof(int32_t), &maxClientsBytes);
                }
                if (ofs.dwNetworkGameClient_isBackgroundMap > 0) {
                    mem.AddScatterReadRequest(
                        s_engineScatterHandle,
                        speculativeNetworkGameClient +
                            ofs.dwNetworkGameClient_isBackgroundMap,
                        &rawBackgroundMap,
                        sizeof(uint8_t), &backgroundBytes);
                }
            }
            if (!mem.ExecuteReadScatter(s_engineScatterHandle)) {
                CloseTrackedScatterHandle(mem, s_engineScatterHandle);
                s_engineScatterHandle = nullptr;
                return false;
            }
            dependentStateRead =
                speculativeNetworkGameClient != 0 &&
                networkGameClient == speculativeNetworkGameClient;
        }

        // A zero-filled partial root is not evidence of a disconnect.
        if (rootBytes != sizeof(networkGameClient))
            return false;

        if (networkGameClient == 0) {
            const bool requireZeroConfirmation =
                s_cachedEngineResolved &&
                (s_cachedEngineInGame || s_cachedEngineSignOnState == 6);
            if (requireZeroConfirmation) {
                if (s_zeroNetworkGameClientSinceUs == 0)
                    s_zeroNetworkGameClientSinceUs = engineNowUs;
                if (!esp::data::IsEngineStateConfirmationElapsed(
                        s_zeroNetworkGameClientSinceUs,
                        engineNowUs,
                        esp::data::kZeroNetworkClientConfirmationUs)) {
                    return false;
                }
            }
            s_zeroNetworkGameClientSinceUs = 0;
            s_cachedNetworkGameClient = 0;
            engineSignonResolved = true;
            engineSignonMenu = true;
            engineSignonInGame = false;
            engineSignonState = 0;
            engineLocalPlayerSlot = -1;
            engineMaxClients = 0;
            engineBackgroundMap = 0;
            return true;
        }

        if (!isLikelyGamePointer(networkGameClient)) {
            s_zeroNetworkGameClientSinceUs = 0;
            return false;
        }
        s_zeroNetworkGameClientSinceUs = 0;

        if (!dependentStateRead) {
            if (!s_engineScatterHandle)
                s_engineScatterHandle = CreateTrackedScatterHandle(mem);
            if (!s_engineScatterHandle)
                return false;

            std::fill(std::begin(signOnCandidates), std::end(signOnCandidates), -1);
            std::fill(std::begin(signBytes), std::end(signBytes), 0);
            rawLocalPlayerSlot = -1;
            rawMaxClients = 0;
            rawBackgroundMap = 0;
            localSlotBytes = maxClientsBytes = backgroundBytes = 0;

            for (int i = 0; i < 3; ++i) {
                if (signOffsets[i] > 0)
                    mem.AddScatterReadRequest(s_engineScatterHandle, networkGameClient + static_cast<uintptr_t>(signOffsets[i]), &signOnCandidates[i], sizeof(int32_t), &signBytes[i]);
            }
            if (ofs.dwNetworkGameClient_localPlayer > 0)
                mem.AddScatterReadRequest(s_engineScatterHandle, networkGameClient + ofs.dwNetworkGameClient_localPlayer, &rawLocalPlayerSlot, sizeof(int32_t), &localSlotBytes);
            if (ofs.dwNetworkGameClient_maxClients > 0)
                mem.AddScatterReadRequest(s_engineScatterHandle, networkGameClient + ofs.dwNetworkGameClient_maxClients, &rawMaxClients, sizeof(int32_t), &maxClientsBytes);
            if (ofs.dwNetworkGameClient_isBackgroundMap > 0)
                mem.AddScatterReadRequest(s_engineScatterHandle, networkGameClient + ofs.dwNetworkGameClient_isBackgroundMap, &rawBackgroundMap, sizeof(uint8_t), &backgroundBytes);

            if (!mem.ExecuteReadScatter(s_engineScatterHandle)) {
                CloseTrackedScatterHandle(mem, s_engineScatterHandle);
                s_engineScatterHandle = nullptr;
                return false;
            }
        }

        if (signOffsets[0] > 0 && signBytes[0] != sizeof(int32_t))
            return false; // A short configured-field read is not an offset migration.
        const int32_t signOnValue = esp::data::SelectSignOnStateCandidate(
            signBytes[0] == sizeof(int32_t) ? signOnCandidates[0] : -1,
            signBytes[1] == sizeof(int32_t) ? signOnCandidates[1] : -1,
            signBytes[2] == sizeof(int32_t) ? signOnCandidates[2] : -1,
            rawMaxClients,
            rawBackgroundMap,
            s_cachedEngineInGame);
        if (!esp::data::IsCompleteEngineActivitySample(
                signOnValue, rawMaxClients, rawBackgroundMap, maxClientsBytes, backgroundBytes))
            return false;

        engineSignonResolved = true;
        engineSignonState = signOnValue;

        const bool sameClient = networkGameClient == s_cachedNetworkGameClient;
        const bool localPlayerSlotResolved = localSlotBytes == sizeof(int32_t) &&
            rawLocalPlayerSlot >= -1 && rawLocalPlayerSlot <= 128;
        if (localPlayerSlotResolved)
            engineLocalPlayerSlot = rawLocalPlayerSlot;
        else if (sameClient && s_cachedEngineResolved)
            engineLocalPlayerSlot = s_cachedEngineLocalPlayerSlot;
        engineMaxClients = rawMaxClients;
        engineBackgroundMap = rawBackgroundMap;
        const bool liveSample = esp::data::IsLiveEngineActivitySample(
            signOnValue, rawMaxClients, rawBackgroundMap);
        const bool holdLive = esp::data::HoldTransientEngineExit(
            liveSample, s_cachedEngineInGame &&
                esp::data::IsEngineStateCacheFresh(s_lastEngineSuccessUs, engineNowUs), sameClient,
            s_nonLiveSignOnSinceUs, engineNowUs);
        if (holdLive) {
            engineSignonState = 6;
            engineSignonInGame = true;
            engineSignonMenu = false;
            engineMaxClients = s_cachedEngineMaxClients;
            engineLocalPlayerSlot = s_cachedEngineLocalPlayerSlot;
            engineBackgroundMap = s_cachedEngineBackgroundMap;
        } else {
            engineSignonInGame = liveSample;
            engineSignonMenu = !liveSample;
        }

        // Commit the identity only after all dependent fields passed validation.
        // A plausible but stale pointer must not trigger a hard scene transition.
        s_cachedNetworkGameClient = networkGameClient;
        return true;
    };

    constexpr uint64_t kEngineResolveInGameIntervalUs = 12000;
    constexpr uint64_t kEngineResolveIdleIntervalUs = 30000;
    const uint64_t engineResolveIntervalUs =
        (s_cachedEngineResolved && s_cachedEngineInGame)
            ? kEngineResolveInGameIntervalUs
            : kEngineResolveIdleIntervalUs;
    const bool engineResolveDue =
        s_lastEngineResolveAttemptUs == 0 ||
        engineNowUs <= s_lastEngineResolveAttemptUs ||
        (engineNowUs - s_lastEngineResolveAttemptUs) >= engineResolveIntervalUs;

    if (!engineResolveDue) {
        engineDebugSource = "throttled";
        if (s_cachedEngineResolved &&
            esp::data::IsEngineStateCacheFresh(s_lastEngineSuccessUs, engineNowUs))
            useCachedEngineState();
    } else if (tryResolveEngineState()) {
        s_lastEngineResolveAttemptUs = engineNowUs;
        s_lastEngineSuccessUs = engineNowUs;
        s_cachedEngineResolved = true;
        s_cachedEngineMenu = engineSignonMenu;
        s_cachedEngineInGame = engineSignonInGame;
        s_cachedEngineSignOnState = engineSignonState;
        s_cachedEngineLocalPlayerSlot = engineLocalPlayerSlot;
        s_cachedEngineMaxClients = engineMaxClients;
        s_cachedEngineBackgroundMap = engineBackgroundMap;
        s_engineResolveMissSinceUs = 0;
        s_engineResolveCompletedActions = 0;
    } else {
        s_lastEngineResolveAttemptUs = engineNowUs;
        if (s_engineResolveMissSinceUs == 0)
            s_engineResolveMissSinceUs = engineNowUs;
        const uint64_t engineResolveMissAgeUs =
            engineNowUs >= s_engineResolveMissSinceUs
                ? engineNowUs - s_engineResolveMissSinceUs
                : 0;
        engineDebugSource = s_cachedEngineResolved ? "cached_stale" : "none";
        if (s_cachedEngineResolved &&
            esp::data::IsEngineStateCacheFresh(s_lastEngineSuccessUs, engineNowUs))
            useCachedEngineState();

        if (g::clientBase && g::engine2Base && !s_dmaRecovering.load(std::memory_order_relaxed)) {
            const auto action = esp::data::SelectEngineResolveMissAction(
                engineResolveMissAgeUs,
                s_engineResolveCompletedActions);
            s_engineResolveCompletedActions |=
                esp::data::EngineResolveMissActionBit(action);
            if (action == esp::data::EngineResolveMissAction::Probe) {
                refreshDmaCaches("engine_resolve_miss_probe", DmaRefreshTier::Probe, false);
            } else if (action == esp::data::EngineResolveMissAction::Repair) {
                refreshDmaCaches("engine_resolve_miss_repair", DmaRefreshTier::Repair, false);
            } else if (action == esp::data::EngineResolveMissAction::Recovery) {
                RequestDmaRecovery("engine_resolve_miss_recovery");
            }
        }
    }
    s_engineStatusResolved.store(engineSignonResolved, std::memory_order_relaxed);
    s_engineSignOnState.store(engineSignonState, std::memory_order_relaxed);
    s_engineLocalPlayerSlot.store(engineLocalPlayerSlot, std::memory_order_relaxed);
    s_engineMaxClients.store(engineMaxClients, std::memory_order_relaxed);
    s_engineBackgroundMap.store(engineBackgroundMap != 0, std::memory_order_relaxed);
    s_engineMenu.store(engineSignonMenu, std::memory_order_relaxed);
    s_engineInGame.store(engineSignonInGame, std::memory_order_relaxed);
    {
        static bool s_lastEngineResolved = false;
        static bool s_lastEngineMenu = false;
        static bool s_lastEngineInGame = false;
        static int32_t s_lastEngineSignOnState = -1;
        static int32_t s_lastEngineLocalPlayerSlot = -1;
        static int32_t s_lastEngineMaxClients = 0;
        static uint8_t s_lastEngineBackgroundMap = 0;
        if (engineSignonResolved != s_lastEngineResolved ||
            engineSignonMenu != s_lastEngineMenu ||
            engineSignonInGame != s_lastEngineInGame ||
            engineSignonState != s_lastEngineSignOnState ||
            engineLocalPlayerSlot != s_lastEngineLocalPlayerSlot ||
            engineMaxClients != s_lastEngineMaxClients ||
            engineBackgroundMap != s_lastEngineBackgroundMap) {
            DmaLogPrintf(
                "[DEBUG] EngineStatus: source=%s resolved=%d menu=%d ingame=%d signOn=%d localSlot=%d maxClients=%d bg=%d engine2=0x%llX",
                engineDebugSource,
                engineSignonResolved ? 1 : 0,
                engineSignonMenu ? 1 : 0,
                engineSignonInGame ? 1 : 0,
                engineSignonState,
                engineLocalPlayerSlot,
                engineMaxClients,
                engineBackgroundMap ? 1 : 0,
                static_cast<unsigned long long>(engine2Base));
            s_lastEngineResolved = engineSignonResolved;
            s_lastEngineMenu = engineSignonMenu;
            s_lastEngineInGame = engineSignonInGame;
            s_lastEngineSignOnState = engineSignonState;
            s_lastEngineLocalPlayerSlot = engineLocalPlayerSlot;
            s_lastEngineMaxClients = engineMaxClients;
            s_lastEngineBackgroundMap = engineBackgroundMap;
        }
    }

    const bool engineMatchLike =
        engineSignonResolved &&
        engineSignonInGame && !engineSignonMenu;
    const bool dmaSessionAttached =
        g::clientBase.load(std::memory_order_relaxed) != 0 &&
        g::engine2Base.load(std::memory_order_relaxed) != 0;
    ApplyDmaRuntimeCacheProfile(
        dmaSessionAttached ? DmaCacheMode::Live : DmaCacheMode::Maintenance);
    {
        static esp::data::SceneTransitionState s_sceneTransitionState = {};
        const auto transition = esp::data::ObserveSceneTransition(
            s_sceneTransitionState,
            engineMatchLike,
            s_cachedNetworkGameClient,
            s_dmaSessionGeneration.load(std::memory_order_relaxed),
            engineNowUs,
            engineSignonResolved && engineSignonMenu);
        if (transition.transition) {
            handleSceneTransition(
                esp::data::SceneTransitionReasonName(transition.reason),
                transition.bumpMapEpoch,
                transition.preserveLiveSnapshot,
                DmaRefreshTier::Probe,
                transition.refreshCaches);
            s_lastEngineResolveAttemptUs = 0;
        }
    }
