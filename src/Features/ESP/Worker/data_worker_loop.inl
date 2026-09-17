    class WorkerDeadlineTimer
    {
    public:
        WorkerDeadlineTimer() noexcept
        {
            constexpr DWORD kHighResolutionTimerFlag = 0x00000002u;
            timer_ = CreateWaitableTimerExW(
                nullptr,
                nullptr,
                kHighResolutionTimerFlag,
                TIMER_MODIFY_STATE | SYNCHRONIZE);
            if (!timer_)
                timer_ = CreateWaitableTimerW(nullptr, FALSE, nullptr);
        }

        ~WorkerDeadlineTimer() noexcept
        {
            if (timer_)
                CloseHandle(timer_);
        }

        WorkerDeadlineTimer(const WorkerDeadlineTimer&) = delete;
        WorkerDeadlineTimer& operator=(const WorkerDeadlineTimer&) = delete;

        template <typename Duration>
        bool WaitFor(Duration duration) noexcept
        {
            if (!timer_ || duration <= Duration::zero())
                return false;

            using HundredNanoseconds =
                std::chrono::duration<LONGLONG, std::ratio<1, 10000000>>;
            const LONGLONG ticks = (std::max)(
                static_cast<LONGLONG>(1),
                std::chrono::duration_cast<HundredNanoseconds>(duration).count());
            LARGE_INTEGER dueTime = {};
            dueTime.QuadPart = -ticks;
            if (!SetWaitableTimer(
                    timer_,
                    &dueTime,
                    0,
                    nullptr,
                    nullptr,
                    FALSE)) {
                return false;
            }
            return WaitForSingleObject(timer_, INFINITE) == WAIT_OBJECT_0;
        }

    private:
        HANDLE timer_ = nullptr;
    };

    template <typename Clock, typename TimePoint>
    void PreciseSleepUntil(const TimePoint& target)
    {
        constexpr auto kSpinWindow = std::chrono::microseconds(150);
        const auto waitTarget = target - kSpinWindow;
        const auto now = Clock::now();
        if (waitTarget > now) {
            static thread_local WorkerDeadlineTimer timer;
            if (!timer.WaitFor(waitTarget - now))
                std::this_thread::sleep_until(waitTarget);
        }

        while (Clock::now() < target)
            _mm_pause();
    }

    std::chrono::microseconds DataWorkerTickInterval()
    {
        const bool hasBases =
            g::clientBase.load(std::memory_order_relaxed) != 0 &&
            g::engine2Base.load(std::memory_order_relaxed) != 0;
        const int hz = esp::worker::DataWorkerTargetHz(
            s_dmaRecovering.load(std::memory_order_relaxed) || IsDmaRecoveryRequested(),
            hasBases,
            s_engineStatusResolved.load(std::memory_order_relaxed),
            s_engineInGame.load(std::memory_order_relaxed),
            s_engineMenu.load(std::memory_order_relaxed), DATA_WORKER_HZ);
        s_dataWorkerTargetHz.store(hz, std::memory_order_relaxed);
        return std::chrono::microseconds(1000000 / hz);
    }

    void PlayerDataReader::DataWorkerLoop(std::stop_token stopToken)
    {
        using Clock = std::chrono::steady_clock;
        s_dataWorkerRunning.store(true, std::memory_order_relaxed);
        struct WorkerExitGuard {
            ~WorkerExitGuard() noexcept
            {
                try {
                    std::shared_lock<std::shared_timed_mutex> lifecycleLock(
                        s_dmaLifecycleMutex);
                    CloseReaderScatterHandles(mem);
                } catch (const std::exception& ex) {
                    app::diagnostics::WriteFallbackError(
                        "Unable to close data-worker scatter handles",
                        ex.what());
                } catch (...) {
                    app::diagnostics::WriteFallbackError(
                        "Unable to close data-worker scatter handles");
                }
                s_dataWorkerUpdateInFlight.store(false, std::memory_order_release);
                s_dataWorkerRunning.store(false, std::memory_order_relaxed);
                s_dataWorkerTargetHz.store(0, std::memory_order_relaxed);
            }
        } workerExitGuard;
        auto tickInterval = DataWorkerTickInterval();
        auto nextTick = Clock::now();
        auto previousCycleStart = Clock::time_point::min();
        auto recentPeakWindowStart = nextTick;
        std::array<uint64_t, esp::worker::kCycleLatencyUpperBoundsUs.size()>
            cycleLatencyBuckets = {};
        uint64_t cycleLatencySamples = 0;
        s_dataWorkerRecentWindowStartUs.store(
            TickNowUs(),
            std::memory_order_relaxed);
        auto lastRecoveryAttempt = Clock::now() - std::chrono::seconds(10);
        uint64_t lastSupervisorRunUs = 0;
        auto nonLiveSignOnSince = Clock::time_point::max();
        auto lastNonLiveSignOnSpan = Clock::duration::zero();
        int previousSignOnState = -1;
        auto zeroPlayerSince = Clock::time_point::max();
        bool zeroPlayerWatchdogLogged = false;
        auto zeroPopulationSince = Clock::time_point::max();
        uint8_t zeroPopulationStage = 0;
        uint64_t lastZeroPopulationHardUs = 0;
        uint64_t lastHighPopulationUs = 0;
        int previousActivePlayers = 0;
        uint64_t lastLoopExceptionLogUs = 0;
        auto handleLoopException = [&](const char* detail) {
            s_dataWorkerLastLoopEndUs.store(TickNowUs(), std::memory_order_relaxed);
            s_dataWorkerUpdateInFlight.store(false, std::memory_order_release);
            s_dmaConsecutiveFailures.fetch_add(1, std::memory_order_relaxed);
            s_dmaTotalFailures.fetch_add(1, std::memory_order_relaxed);
            const uint64_t nowUs = TickNowUs();
            if (esp::worker::IsWorkerCooldownElapsed(
                    lastLoopExceptionLogUs,
                    nowUs,
                    esp::worker::kWorkerStallLogCooldownUs)) {
                lastLoopExceptionLogUs = nowUs;
                DmaLogPrintf(
                    "[ERROR] DataWorkerLoop exception: %s",
                    detail ? detail : "unknown");
            }
            RequestDmaRecovery("data_worker_loop_exception");
        };

        while (!stopToken.stop_requested()) {
            try {
                const auto cycleStart = Clock::now();
                if (previousCycleStart != Clock::time_point::min() &&
                    esp::worker::IsWorkerScheduleSkipped(
                        previousCycleStart,
                        cycleStart,
                        tickInterval)) {
                    s_dataWorkerDeadlineMissCount.fetch_add(
                        1,
                        std::memory_order_relaxed);
                }
                previousCycleStart = cycleStart;
                const uint64_t cycleStartUs = TickNowUs();
                s_dataWorkerLastLoopStartUs.store(cycleStartUs, std::memory_order_relaxed);
                s_dataWorkerInFlightSinceUs.store(cycleStartUs, std::memory_order_relaxed);
                s_dataWorkerUpdateInFlight.store(true, std::memory_order_release);

                ProcessPendingCacheRefreshRequest();
                esp::UpdateData();
                UpdateReadQualityTelemetry(TickNowUs());

                const auto cycleEnd = Clock::now();
                const uint64_t cycleEndUs = TickNowUs();
                const uint64_t cycleUs = static_cast<uint64_t>(
                    std::chrono::duration_cast<std::chrono::microseconds>(cycleEnd - cycleStart).count());

                s_dataWorkerLastLoopEndUs.store(cycleEndUs, std::memory_order_relaxed);
                s_dataWorkerUpdateInFlight.store(false, std::memory_order_release);
                s_dataWorkerCycleUs.store(cycleUs, std::memory_order_relaxed);

                
                uint64_t prevMax = s_dataWorkerMaxCycleUs.load(std::memory_order_relaxed);
                while (cycleUs > prevMax &&
                       !s_dataWorkerMaxCycleUs.compare_exchange_weak(prevMax, cycleUs, std::memory_order_relaxed))
                    ;

                const bool resetRecentWindow =
                    cycleEnd - recentPeakWindowStart >=
                    std::chrono::microseconds(esp::worker::kWorkerRecentPeakWindowUs);
                if (resetRecentWindow) {
                    recentPeakWindowStart = cycleEnd;
                    cycleLatencyBuckets.fill(0);
                    cycleLatencySamples = 0;
                    s_dataWorkerRecentWindowStartUs.store(
                        cycleEndUs,
                        std::memory_order_relaxed);
                    s_dataWorkerRecentMaxCycleUs.store(cycleUs, std::memory_order_relaxed);
                    s_dataWorkerDeadlineMissCount.store(0, std::memory_order_relaxed);
                    s_dataWorkerCycleSampleCount.store(0, std::memory_order_relaxed);
                    s_dataWorkerCycleOverBudgetCount.store(0, std::memory_order_relaxed);
                    s_dataWorkerCycleOver5msCount.store(0, std::memory_order_relaxed);
                    s_dataWorkerCycleOver16msCount.store(0, std::memory_order_relaxed);
                    s_playerCoreAnomalyCount.store(0, std::memory_order_relaxed);
                    s_playerCoreRecoveredCount.store(0, std::memory_order_relaxed);
                    s_playerCoreGlobalRefreshAvoidedCount.store(0, std::memory_order_relaxed);
                    s_playerCoreBatchHoldCount.store(0, std::memory_order_relaxed);
                    s_playerUnexpectedEvictionCount.store(0, std::memory_order_relaxed);
                    s_playerExpectedEvictionCount.store(0, std::memory_order_relaxed);
                    s_dmaManualRefreshRecentPeakUs.store(0, std::memory_order_relaxed);
                    s_dmaManualRefreshRecentCount.store(0, std::memory_order_relaxed);
                    s_dmaManualRefreshQueuedRecentCount.store(0, std::memory_order_relaxed);
                    s_dmaManualRefreshSuppressedRecentCount.store(0, std::memory_order_relaxed);
                    s_dmaManualRefreshAvoidedRecentCount.store(0, std::memory_order_relaxed);
                } else {
                    UpdatePeak(s_dataWorkerRecentMaxCycleUs, cycleUs);
                }
                s_dataWorkerCycleSampleCount.fetch_add(1, std::memory_order_relaxed);
                if (cycleUs > (1000000u / DATA_WORKER_HZ))
                    s_dataWorkerCycleOverBudgetCount.fetch_add(1, std::memory_order_relaxed);
                if (cycleUs > 5000u)
                    s_dataWorkerCycleOver5msCount.fetch_add(1, std::memory_order_relaxed);
                if (cycleUs > 16667u)
                    s_dataWorkerCycleOver16msCount.fetch_add(1, std::memory_order_relaxed);
                ++cycleLatencyBuckets[esp::worker::LatencyBucketIndex(cycleUs)];
                ++cycleLatencySamples;
                if (cycleLatencySamples == 1u || (cycleLatencySamples % 64u) == 0u) {
                    s_dataWorkerCycleP50Us.store(
                        esp::worker::LatencyPercentileUpperBound(
                            cycleLatencyBuckets, cycleLatencySamples, 50u),
                        std::memory_order_relaxed);
                    s_dataWorkerCycleP95Us.store(
                        esp::worker::LatencyPercentileUpperBound(
                            cycleLatencyBuckets, cycleLatencySamples, 95u),
                        std::memory_order_relaxed);
                    s_dataWorkerCycleP99Us.store(
                        esp::worker::LatencyPercentileUpperBound(
                            cycleLatencyBuckets, cycleLatencySamples, 99u),
                        std::memory_order_relaxed);
                }
            } catch (const std::exception& exception) {
                handleLoopException(exception.what());
            } catch (...) {
                handleLoopException("non-standard exception");
            }

            const auto now = Clock::now();
            const uint64_t supervisorNowUs = TickNowUs();
            if (!esp::worker::IsDataSupervisorDue(
                    lastSupervisorRunUs,
                    supervisorNowUs)) {
                tickInterval = DataWorkerTickInterval();
                const auto scheduleNow = Clock::now();
                nextTick = esp::worker::NextWorkerDeadline(
                    nextTick,
                    scheduleNow,
                    tickInterval);
                PreciseSleepUntil<Clock>(nextTick);
                continue;
            }
            lastSupervisorRunUs = supervisorNowUs;
            const auto signOnState = s_engineSignOnState.load(std::memory_order_relaxed);
            if (signOnState != 6) {
                if (nonLiveSignOnSince == Clock::time_point::max())
                    nonLiveSignOnSince = now;
            } else if (nonLiveSignOnSince != Clock::time_point::max()) {
                lastNonLiveSignOnSpan = now - nonLiveSignOnSince;
                nonLiveSignOnSince = Clock::time_point::max();
            }

            
            
            
            
            
            
            
            
            
            if (previousSignOnState != 6 &&
                signOnState == 6 &&
                lastNonLiveSignOnSpan >= std::chrono::milliseconds(500)) {
                DmaLogPrintf("[INFO] signOnState re-entered live state (%d -> 6) after %lld ms; keeping caches intact",
                    previousSignOnState,
                    static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(lastNonLiveSignOnSpan).count()));
            }
            previousSignOnState = signOnState;

            const bool recoveryRequested = IsDmaRecoveryRequested();
            const bool missingBases = !g::clientBase || !g::engine2Base;
            const bool engineResolved = s_engineStatusResolved.load(std::memory_order_relaxed);
            const bool engineInGame = s_engineInGame.load(std::memory_order_relaxed);
            const bool engineMenu = s_engineMenu.load(std::memory_order_relaxed);
            
            
            
            
            
            
            
            
            
            
            
            
            
            
            
            
            
            
            
            
            
            
            bool populationWatchdogRecovery = false;
            {
                const uint64_t watchdogNowUs = TickNowUs();
                const uint64_t sceneResetUs = s_lastSceneResetUs.load(std::memory_order_relaxed);
                const uint64_t warmupEnteredUs = s_sceneWarmupEnteredUs.load(std::memory_order_relaxed);
                const bool localIdentityMissing =
                    s_localPawn == 0 &&
                    !s_localMaskResolved;
                const bool looksInGame =
                    engineResolved &&
                    engineInGame &&
                    signOnState == 6 &&
                    g::clientBase &&
                    g::engine2Base;
                const bool inGameLongEnough =
                    sceneResetUs > 0 &&
                    watchdogNowUs > sceneResetUs &&
                    (watchdogNowUs - sceneResetUs) >= 8000000u;

                const bool localIdentityMissingUnexpected =
                    looksInGame &&
                    inGameLongEnough &&
                    localIdentityMissing;

                if (localIdentityMissingUnexpected) {
                    if (zeroPlayerSince == Clock::time_point::max())
                        zeroPlayerSince = now;
                } else {
                    zeroPlayerSince = Clock::time_point::max();
                    zeroPlayerWatchdogLogged = false;
                }

                const int activePlayers = std::clamp(s_activePlayerCount.load(std::memory_order_relaxed), 0, 64);
                const int highestEntityIndex = std::max(0, s_highestEntityIdxStat.load(std::memory_order_relaxed));
                const int playerSlotBudget = std::clamp(s_playerSlotScanLimitStat.load(std::memory_order_relaxed), 0, 64);
                const int maxClients = std::clamp(s_engineMaxClients.load(std::memory_order_relaxed), 0, 256);
                const uint64_t sceneAgeUs =
                    sceneResetUs > 0 && watchdogNowUs >= sceneResetUs
                    ? watchdogNowUs - sceneResetUs
                    : 0;
                const uint64_t warmupAgeUs =
                    warmupEnteredUs > 0 && watchdogNowUs >= warmupEnteredUs
                    ? watchdogNowUs - warmupEnteredUs
                    : 0;
                const uint64_t lastLiveMapNameSeenUs =
                    s_lastLiveMapNameSeenUs.load(std::memory_order_relaxed);
                const bool liveMapRecentlySeen =
                    esp::worker::IsRecentWorkerTimestamp(
                        lastLiveMapNameSeenUs,
                        watchdogNowUs,
                        esp::worker::kLiveHistoryWindowUs);
                const bool liveByEngine =
                    engineResolved &&
                    engineInGame &&
                    !engineMenu &&
                    (signOnState == 6 || (maxClients >= 2 && playerSlotBudget >= 32));
                const bool definitelyMenu =
                    engineResolved &&
                    engineMenu &&
                    !engineInGame;
                const bool liveByShape =
                    !definitelyMenu &&
                    playerSlotBudget >= 32 &&
                    (maxClients >= 2 || highestEntityIndex >= 64) &&
                    g::clientBase &&
                    g::engine2Base;
                const bool entityShapeReady =
                    playerSlotBudget >= 32 &&
                    highestEntityIndex >= 64;
                const bool engineTransitionLimbo =
                    !definitelyMenu &&
                    !engineInGame &&
                    maxClients <= 1 &&
                    highestEntityIndex >= 0 &&
                    highestEntityIndex < 64;
                const bool liveByMap =
                    !definitelyMenu &&
                    liveMapRecentlySeen &&
                    playerSlotBudget >= 32 &&
                    highestEntityIndex >= 64 &&
                    g::clientBase &&
                    g::engine2Base;
                if (activePlayers >= 5)
                    lastHighPopulationUs = watchdogNowUs;
                if (engineTransitionLimbo) {
                    lastHighPopulationUs = 0;
                    previousActivePlayers = 0;
                }
                const bool liveByRecentHistory =
                    !definitelyMenu &&
                    !engineTransitionLimbo &&
                    (engineInGame || maxClients >= 2 || entityShapeReady) &&
                    esp::worker::IsRecentWorkerTimestamp(
                        lastHighPopulationUs,
                        watchdogNowUs,
                        esp::worker::kLiveHistoryWindowUs) &&
                    g::clientBase &&
                    g::engine2Base;
                const bool liveByEarlyEngine =
                    !definitelyMenu &&
                    !engineTransitionLimbo &&
                    engineResolved &&
                    engineInGame &&
                    !engineMenu &&
                    g::clientBase &&
                    g::engine2Base;
                const bool livePopulationExpected =
                    !definitelyMenu &&
                    !engineTransitionLimbo &&
                    (liveByEngine || liveByShape || liveByMap || liveByRecentHistory || liveByEarlyEngine) &&
                    g::clientBase &&
                    g::engine2Base;
                const bool suspiciousFlatEntityRange =
                    maxClients >= 2 &&
                    playerSlotBudget >= 32 &&
                    highestEntityIndex > 0 &&
                    highestEntityIndex < 32;
                const bool suddenDropFromLive =
                    previousActivePlayers >= 2 &&
                    activePlayers == 0 &&
                    liveByRecentHistory;
                const bool zeroPopulationObserved =
                    livePopulationExpected &&
                    activePlayers == 0 &&
                    s_playerHierarchyHighWaterSlot.load(std::memory_order_relaxed) == 0;
                const bool zeroPopulationGraceElapsed =
                    esp::data::IsZeroPopulationGraceElapsed(
                        sceneAgeUs,
                        warmupAgeUs,
                        liveMapRecentlySeen,
                        liveByRecentHistory,
                        suspiciousFlatEntityRange,
                        localIdentityMissing);

                if (engineTransitionLimbo) {
                    zeroPopulationSince = Clock::time_point::max();
                    zeroPopulationStage = 0;
                    lastZeroPopulationHardUs = 0;
                } else if (zeroPopulationObserved) {
                    if (zeroPopulationSince == Clock::time_point::max())
                        zeroPopulationSince = now;

                    const auto zeroAge = now - zeroPopulationSince;
                    const bool canAct = zeroPopulationGraceElapsed && !s_dmaRecovering.load(std::memory_order_relaxed);
                    if (esp::data::ShouldRunZeroPopulationProbe(
                            canAct,
                            zeroPopulationStage,
                            zeroAge,
                            suddenDropFromLive)) {
                        zeroPopulationStage = 1u;
                        SetSceneWarmupState(esp::SceneWarmupState::HierarchyWarming, watchdogNowUs);
                        RefreshDmaCaches(
                            suspiciousFlatEntityRange ? "zero_players_flat_entity_probe" : "zero_players_live_probe",
                            DmaRefreshTier::Probe);
                    }
                    if (esp::data::ShouldRunZeroPopulationRepair(
                            canAct,
                            zeroPopulationStage,
                            zeroAge)) {
                        zeroPopulationStage = 2u;
                        const auto reset = esp::recovery::EvaluateResetPolicy(
                            esp::recovery::ResetTrigger::ZeroPlayersRepair);
                        ResetRuntimeStateSoft(reset.reason);
                        SetSceneWarmupState(esp::SceneWarmupState::HierarchyWarming, watchdogNowUs);
                        RefreshDmaCaches(
                            suspiciousFlatEntityRange ? "zero_players_flat_entity_repair" : "zero_players_live_repair",
                            DmaRefreshTier::Repair,
                            false);
                    }
                    if (esp::data::ShouldRunZeroPopulationFull(
                            canAct,
                            zeroPopulationStage,
                            zeroAge)) {
                        zeroPopulationStage = 3u;
                        const auto reset = esp::recovery::EvaluateResetPolicy(
                            esp::recovery::ResetTrigger::ZeroPlayersFull);
                        ResetRuntimeStateSoft(reset.reason);
                        SetSceneWarmupState(
                            suspiciousFlatEntityRange ? esp::SceneWarmupState::HierarchyWarming : esp::SceneWarmupState::Recovery,
                            watchdogNowUs);
                        RefreshDmaCaches(
                            suspiciousFlatEntityRange ? "zero_players_flat_entity_full" : "zero_players_live_full",
                            DmaRefreshTier::Full,
                            false);
                        if (!suspiciousFlatEntityRange) {
                            RequestDmaRecovery("zero_players_live_persistent");
                            populationWatchdogRecovery = true;
                        }
                        lastZeroPopulationHardUs = watchdogNowUs;
                    } else if (esp::data::ShouldRetryZeroPopulationRecovery(
                                   canAct,
                                   suspiciousFlatEntityRange,
                                   zeroPopulationStage,
                                   zeroAge,
                                   lastZeroPopulationHardUs,
                                   watchdogNowUs)) {
                        lastZeroPopulationHardUs = watchdogNowUs;
                        SetSceneWarmupState(esp::SceneWarmupState::Recovery, watchdogNowUs);
                        RequestDmaRecovery("zero_players_live_retry");
                        populationWatchdogRecovery = true;
                    }
                } else {
                    zeroPopulationSince = Clock::time_point::max();
                    zeroPopulationStage = 0;
                    if (activePlayers > 0)
                        lastZeroPopulationHardUs = 0;
                }
                previousActivePlayers = activePlayers;
            }
            const bool localIdentityWatchdog =
                zeroPlayerSince != Clock::time_point::max() &&
                (now - zeroPlayerSince) > std::chrono::seconds(8);

            const bool recoveryRequestedNow = recoveryRequested || populationWatchdogRecovery || IsDmaRecoveryRequested();
            if (recoveryRequestedNow || missingBases || localIdentityWatchdog) {
                if (localIdentityWatchdog) {
                    if (!zeroPlayerWatchdogLogged) {
                        DmaLogPrintf("[INFO] Local identity missing for 8s, re-attaching DMA...");
                        zeroPlayerWatchdogLogged = true;
                    }
                    zeroPlayerSince = Clock::time_point::max();
                }
                const auto recoveryInterval =
                    missingBases
                        ? std::chrono::milliseconds(250)
                        : recoveryRequestedNow
                            ? std::chrono::milliseconds(3000)
                            : std::chrono::milliseconds(2000);
                if (now - lastRecoveryAttempt > recoveryInterval) {
                    lastRecoveryAttempt = now;
                    if (TryRecoverDma()) {
                        ClearDmaRecoveryRequest();
                        const auto reset = esp::recovery::EvaluateResetPolicy(
                            esp::recovery::ResetTrigger::DmaRecoverySuccess);
                        ResetRuntimeStateHard(reset.reason, reset.publishClearedSnapshot);
                    }
                }
            }

            tickInterval = DataWorkerTickInterval();
            const auto scheduleNow = Clock::now();
            nextTick = esp::worker::NextWorkerDeadline(nextTick, scheduleNow, tickInterval);
            PreciseSleepUntil<Clock>(nextTick);
        }

    }
