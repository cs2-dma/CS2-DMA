static void EnterConfirmedCs2Wait(const char* reason)
{
    const auto previousStatus =
        static_cast<esp::GameStatus>(s_gameStatus.load(std::memory_order_relaxed));
    const uint64_t nowUs = TickNowUs();
    const auto warmupState =
        static_cast<esp::SceneWarmupState>(
            s_sceneWarmupState.load(std::memory_order_relaxed));
    const bool recentColdAttachReset =
        esp::data::ShouldPreserveRecentColdAttachReset(
            previousStatus == esp::GameStatus::WaitCs2,
            warmupState == esp::SceneWarmupState::ColdAttach,
            s_lastSceneResetUs.load(std::memory_order_relaxed),
            nowUs);

    s_requiredReadFailureCount = 0;
    s_engineStatusResolved.store(false, std::memory_order_relaxed);
    s_engineSignOnState.store(-1, std::memory_order_relaxed);
    s_engineLocalPlayerSlot.store(-1, std::memory_order_relaxed);
    s_engineMaxClients.store(0, std::memory_order_relaxed);
    s_engineBackgroundMap.store(false, std::memory_order_relaxed);
    s_engineMenu.store(false, std::memory_order_relaxed);
    s_engineInGame.store(false, std::memory_order_relaxed);
    s_gameStatus.store(
        static_cast<uint8_t>(esp::GameStatus::WaitCs2),
        std::memory_order_relaxed);

    if (previousStatus != esp::GameStatus::WaitCs2) {
        DmaLogPrintf(
            "[INFO] GameStatus: OK -> Wait cs2.exe (%s)",
            reason ? reason : "confirmed_process_loss");
    }
    if (!recentColdAttachReset) {
        const auto reset = esp::recovery::EvaluateResetPolicy(
            esp::recovery::ResetTrigger::WaitForProcess);
        ResetRuntimeStateHard(reset.reason, reset.publishClearedSnapshot);
    }

    s_mapFingerprint = 0;
    SetSceneWarmupState(esp::SceneWarmupState::ColdAttach, nowUs);
    g::clientBase = 0;
    g::engine2Base = 0;
    SetAttachedCs2ProcessId(0);
    // The failed request is fully handled by entering the process-wait state.
    // Missing bases now drive the lightweight PID probe; keeping the request
    // set would delay reattachment behind the slower recovery cadence.
    ClearDmaRecoveryRequest();
    ApplyDmaRuntimeCacheProfile(DmaCacheMode::ProcessDiscovery, true);
}

static bool TryRecoverDma()
{
    if (s_dataWorkerStopRequested.load(std::memory_order_relaxed))
        return false;

    bool expected = false;
    if (!s_dmaRecovering.compare_exchange_strong(expected, true))
        return false;

    const DmaLogLevel previousLogLevel = DmaGetLogLevel();
    struct RecoveryScope {
        DmaLogLevel previousLogLevel;

        ~RecoveryScope()
        {
            mem.SetDirectReadWarningSuppressed(false);
            DmaSetLogLevel(previousLogLevel);
            ReleaseCameraWorkerPauseRequest();
            s_dmaRecovering.store(false, std::memory_order_release);
        }
    } recoveryScope{previousLogLevel};

    DmaSetLogLevel(DmaLogLevel::Silent);
    mem.SetDirectReadWarningSuppressed(true);

    AcquireCameraWorkerPauseRequest();
    const auto pauseDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (s_cameraWorkerRunning.load(std::memory_order_acquire) &&
           !s_cameraWorkerPaused.load(std::memory_order_acquire) &&
           !s_dataWorkerStopRequested.load(std::memory_order_relaxed) &&
           std::chrono::steady_clock::now() < pauseDeadline) {
        std::this_thread::sleep_for(std::chrono::microseconds(500));
    }
    if (s_cameraWorkerRunning.load(std::memory_order_acquire) &&
        !s_cameraWorkerPaused.load(std::memory_order_acquire)) {
        RecordDmaEvent({
            .action = "recovery_deferred",
            .reason = "camera_pause_timeout"
        });
        return false;
    }
    if (s_dataWorkerStopRequested.load(std::memory_order_relaxed))
        return false;

    std::unique_lock<std::shared_timed_mutex> lifecycleLock(
        s_dmaLifecycleMutex,
        std::defer_lock);
    if (!lifecycleLock.try_lock_for(std::chrono::seconds(2))) {
        RecordDmaEvent({
            .action = "recovery_deferred",
            .reason = "dma_lifecycle_busy"
        });
        return false;
    }
    if (s_dataWorkerStopRequested.load(std::memory_order_relaxed))
        return false;

    // Recovery supersedes refresh requests raised by the failed session.
    s_pendingRefreshFlags.store(0, std::memory_order_release);
    CloseReaderScatterHandles(mem);

    using Clock = std::chrono::steady_clock;
    static esp::recovery::ProcessIdentityTracker s_processTracker = {};
    static uint32_t s_pendingModulePid = 0;
    static Clock::time_point s_pendingModuleSince = {};
    static Clock::time_point s_lastAttachAttempt = {};
    static uint32_t s_lastAttachPid = 0;
    static Clock::time_point s_lastCatalogRefreshAttempt = {};
    static Clock::time_point s_lastCatalogFailureCounted = {};
    static uint32_t s_consecutiveRecoveryFailures = 0;
    static uint32_t s_processDiscoveryMisses = 0;
    constexpr auto kPendingModuleGrace = std::chrono::seconds(6);
    constexpr auto kAttachRetryInterval = std::chrono::seconds(2);
    constexpr auto kCatalogRefreshInterval = std::chrono::milliseconds(750);
    constexpr uint32_t kFullCatalogRefreshMisses = 2u;

    const auto now = Clock::now();
    const uintptr_t previousClientBase = g::clientBase;
    const uintptr_t previousEngine2Base = g::engine2Base;

    auto resolveReadableModules = [&]() -> std::pair<uintptr_t, uintptr_t> {
        if (s_dataWorkerStopRequested.load(std::memory_order_relaxed))
            return {0, 0};

        const uintptr_t clientBase = mem.GetModuleBase("client.dll");
        const uintptr_t engine2Base = mem.GetModuleBase("engine2.dll");
        if (!clientBase || !engine2Base)
            return {0, 0};

        uint16_t clientMz = 0;
        uint16_t engineMz = 0;
        if (!mem.Read(clientBase, &clientMz, sizeof(clientMz)) ||
            clientMz != 0x5A4D ||
            !mem.Read(engine2Base, &engineMz, sizeof(engineMz)) ||
            engineMz != 0x5A4D) {
            return {0, 0};
        }
        return {clientBase, engine2Base};
    };

    auto commitResolvedModules = [&](uintptr_t clientBase, uintptr_t engine2Base) {
        g::clientBase = clientBase;
        g::engine2Base = engine2Base;

        runtime_offsets::RuntimeResolveReport offsetReport = {};
        std::string offsetMessage;
        const bool runtimeOffsetsReady =
            runtime_offsets::ResolveFromAttachedProcess(
                &offsetReport,
                &offsetMessage);
        std::string sanityMessage;
        const bool offsetsReady =
            runtimeOffsetsReady ||
            runtime_offsets::SanityCheckOffsets(&sanityMessage);
        if (!offsetsReady) {
            if (!offsetReport.cached) {
                RecordDmaEvent({
                    .action = "offsets_invalid",
                    .reason = "runtime_and_local_validation_failed"
                });
            }
            return false;
        }
        if (!offsetReport.cached) {
            RecordDmaEvent({
                .action = runtimeOffsetsReady ? "offsets_runtime" : "offsets_local",
                .reason = runtimeOffsetsReady
                    ? "attached_process_validated"
                    : "runtime_unavailable_local_valid"
            });
        }

        s_dmaConsecutiveFailures.store(0, std::memory_order_relaxed);
        s_consecutiveRecoveryFailures = 0;
        s_dmaTotalRecoveries.fetch_add(1, std::memory_order_relaxed);
        esp::recovery::ResetProcessIdentityCandidate(s_processTracker);
        s_pendingModulePid = 0;
        s_pendingModuleSince = {};
        ApplyDmaRuntimeCacheProfile(DmaCacheMode::Live);
        RecordDmaEvent({
            .action = "recovery_ok",
            .reason = "modules_resolved"
        });
        return true;
    };

    auto hardReinitializeSession = [&](const char* reason) {
        DmaSetLogLevel(DmaLogLevel::Info);
        DmaLogPrintf("[WARN] DMA session could not recover. Reinitializing VMM...");
        RecordDmaEvent({
            .action = "recovery_reinit",
            .reason = reason
        });
        s_dmaSessionGeneration.fetch_add(1, std::memory_order_acq_rel);
        mem.CloseDma();
        if (!mem.InitDma(true, false)) {
            mem.CloseDma();
            mem.InitDma(false, false);
        }
        (void)app::input::InitializePrimaryKeyboard();
        esp::recovery::ResetProcessIdentityCandidate(s_processTracker);
        s_pendingModulePid = 0;
        s_pendingModuleSince = {};
        s_lastAttachAttempt = {};
        s_lastAttachPid = 0;
        s_lastCatalogRefreshAttempt = {};
        s_lastCatalogFailureCounted = {};
        s_consecutiveRecoveryFailures = 0;
        s_processDiscoveryMisses = 0;
        s_dmaConsecutiveFailures.store(0, std::memory_order_relaxed);
        EnterConfirmedCs2Wait("dma_session_reinitialized");
    };

    auto registerRecoveryFailure = [&](const char* reason) {
        const uint32_t failures = ++s_consecutiveRecoveryFailures;
        RecordDmaEvent({
            .action = "recovery_fail",
            .reason = reason
        });
        if (esp::recovery::ShouldHardReinitializeDma(failures) &&
            !s_dataWorkerStopRequested.load(std::memory_order_relaxed)) {
            hardReinitializeSession(reason);
        }
    };

    uintptr_t clientBase = 0;
    uintptr_t engine2Base = 0;
    const uint32_t trackedPid = GetAttachedCs2ProcessId();
    DWORD observedPid = mem.GetPidFromName("cs2.exe");

    // Escalate progressively. Most transient failures recover from the current
    // catalog or a cheap TLB/FAST refresh and should never trigger a heavy
    // process-catalog refresh that pauses live rendering.
    std::tie(clientBase, engine2Base) = resolveReadableModules();
    if (trackedPid != 0 &&
        observedPid == trackedPid &&
        clientBase &&
        engine2Base) {
        return commitResolvedModules(clientBase, engine2Base);
    }

    if (mem.vHandle) {
        VMMDLL_ConfigSet(mem.vHandle, VMMDLL_OPT_REFRESH_FREQ_TLB_PARTIAL, 1);
        VMMDLL_ConfigSet(mem.vHandle, VMMDLL_OPT_REFRESH_FREQ_FAST, 1);
        std::tie(clientBase, engine2Base) = resolveReadableModules();
        observedPid = mem.GetPidFromName("cs2.exe");
        if (trackedPid != 0 &&
            observedPid == trackedPid &&
            clientBase &&
            engine2Base) {
            return commitResolvedModules(clientBase, engine2Base);
        }
    }

    const bool catalogRefreshDue =
        s_lastCatalogRefreshAttempt == Clock::time_point{} ||
        now - s_lastCatalogRefreshAttempt >= kCatalogRefreshInterval;
    if (catalogRefreshDue) {
        s_lastCatalogRefreshAttempt = now;
        const bool catalogRefreshed =
            mem.vHandle &&
            VMMDLL_ConfigSet(mem.vHandle, VMMDLL_OPT_REFRESH_FREQ_MEDIUM, 1);
        if (!catalogRefreshed) {
            if (s_lastCatalogFailureCounted == Clock::time_point{} ||
                now - s_lastCatalogFailureCounted >= kAttachRetryInterval) {
                s_lastCatalogFailureCounted = now;
                registerRecoveryFailure("process_catalog_refresh_failed");
            }
            return false;
        }
        s_lastCatalogFailureCounted = {};
        std::tie(clientBase, engine2Base) = resolveReadableModules();
        observedPid = mem.GetPidFromName("cs2.exe");
        if (trackedPid == 0 && observedPid == 0) {
            ++s_processDiscoveryMisses;
            if (s_processDiscoveryMisses >= kFullCatalogRefreshMisses) {
                s_processDiscoveryMisses = 0;
                VMMDLL_ConfigSet(
                    mem.vHandle,
                    VMMDLL_OPT_REFRESH_ALL,
                    1);
                observedPid = mem.GetPidFromName("cs2.exe");
            }
        } else {
            s_processDiscoveryMisses = 0;
        }
    }

    const auto identityDecision = esp::recovery::ObserveProcessIdentity(
        s_processTracker,
        trackedPid,
        observedPid,
        TickNowUs());

    if (identityDecision ==
            esp::recovery::ProcessIdentityDecision::Stable &&
        clientBase &&
        engine2Base) {
        return commitResolvedModules(clientBase, engine2Base);
    }

    if (identityDecision ==
        esp::recovery::ProcessIdentityDecision::PendingConfirmation) {
        return false;
    }

    if (identityDecision ==
        esp::recovery::ProcessIdentityDecision::ConfirmedProcessLost) {
        RecordDmaEvent({
            .action = "process_lost",
            .reason = "pid_missing_confirmed"
        });
        s_dmaConsecutiveFailures.store(0, std::memory_order_relaxed);
        s_consecutiveRecoveryFailures = 0;
        s_pendingModulePid = 0;
        s_pendingModuleSince = {};
        s_dmaSessionGeneration.fetch_add(1, std::memory_order_acq_rel);
        mem.ResetProcessState();
        EnterConfirmedCs2Wait("pid_missing_confirmed");
        return false;
    }

    if (identityDecision ==
        esp::recovery::ProcessIdentityDecision::NoProcess) {
        ApplyDmaRuntimeCacheProfile(DmaCacheMode::ProcessDiscovery);
        s_dmaConsecutiveFailures.store(0, std::memory_order_relaxed);
        s_consecutiveRecoveryFailures = 0;
        return false;
    }

    const bool processChanged =
        identityDecision ==
            esp::recovery::ProcessIdentityDecision::ConfirmedNewProcess ||
        identityDecision ==
            esp::recovery::ProcessIdentityDecision::ConfirmedReplacement;
    if (identityDecision ==
        esp::recovery::ProcessIdentityDecision::ConfirmedReplacement) {
        RecordDmaEvent({
            .action = "process_replaced",
            .reason = "new_pid_confirmed"
        });
        EnterConfirmedCs2Wait("new_pid_confirmed");
    }

    uint32_t attachPid = processChanged ? observedPid : trackedPid;
    if (attachPid == 0)
        return false;

    const bool pendingModules =
        s_pendingModulePid == attachPid &&
        s_pendingModuleSince != Clock::time_point{};
    if (!processChanged && pendingModules &&
        now - s_pendingModuleSince < kPendingModuleGrace) {
        return false;
    }
    if (s_lastAttachPid == attachPid &&
        s_lastAttachAttempt != Clock::time_point{} &&
        now - s_lastAttachAttempt < kAttachRetryInterval) {
        return false;
    }

    s_lastAttachAttempt = now;
    s_lastAttachPid = attachPid;
    s_dmaSessionGeneration.fetch_add(1, std::memory_order_acq_rel);
    g::clientBase = 0;
    g::engine2Base = 0;
    mem.ResetProcessState();
    const bool attached =
        mem.vHandle &&
        mem.AttachToProcessId("cs2.exe", attachPid, true);
    if (!attached) {
        SetAttachedCs2ProcessId(0);
        registerRecoveryFailure("process_attach_failed");
        return false;
    }

    SetAttachedCs2ProcessId(attachPid);
    s_pendingModulePid = attachPid;
    s_pendingModuleSince = now;
    std::tie(clientBase, engine2Base) = resolveReadableModules();
    if (clientBase && engine2Base) {
        return commitResolvedModules(clientBase, engine2Base);
    }

    if (previousClientBase != 0 || previousEngine2Base != 0)
        RecordDmaEvent({
            .action = "modules_pending",
            .reason = "attached_process_initializing"
        });
    return false;
}
