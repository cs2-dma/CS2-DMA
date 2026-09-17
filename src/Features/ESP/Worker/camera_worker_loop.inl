    void PlayerDataReader::CameraWorkerLoop(std::stop_token stopToken)
    {
        using Clock = std::chrono::steady_clock;
        auto tickInterval = std::chrono::microseconds(1000000 / CAMERA_WORKER_HZ);
        auto nextTick = Clock::now() - (tickInterval / 2);
        auto previousCycleStart = Clock::time_point::min();
        auto recentPeakWindowStart = nextTick;
        std::array<uint64_t, esp::worker::kCycleLatencyUpperBoundsUs.size()>
            cycleLatencyBuckets = {};
        uint64_t cycleLatencySamples = 0;
        s_cameraWorkerRecentWindowStartUs.store(
            TickNowUs(),
            std::memory_order_relaxed);
        uint32_t consecutiveViewMisses = 0;
        uint32_t consecutiveLocalPosMisses = 0;
        uint64_t lastSuccessfulViewReadUs = 0;
        uint64_t lastCameraSelfHealUs = 0;
        uint64_t cameraSelfHealSessionGeneration = 0;
        uint64_t lastCameraStallLogUs = 0;
        uint64_t lastCameraExceptionLogUs = 0;
        uintptr_t cachedLocalPawn = 0;
        uintptr_t cachedLocalSceneNode = 0;
        uint64_t cachedSceneSerial = 0;
        bool idleSnapshotCleared = false;
        VMMDLL_SCATTER_HANDLE cameraHandle = nullptr;
        struct CameraHandleGuard {
            VMMDLL_SCATTER_HANDLE& handle;

            ~CameraHandleGuard()
            {
                if (!handle)
                    return;
                std::shared_lock<std::shared_timed_mutex> lifecycleLock(
                    s_dmaLifecycleMutex);
                CloseTrackedScatterHandle(mem, handle);
                handle = nullptr;
            }
        } cameraHandleGuard{cameraHandle};
        bool cameraHandleInvalidatedByRecovery = false;
        s_cameraWorkerRunning.store(true, std::memory_order_relaxed);

        while (!stopToken.stop_requested()) {
            try {
                if (s_cameraWorkerPauseRequests.load(std::memory_order_acquire) != 0u) {
                    if (RecoverOrphanedCameraWorkerPauseRequest())
                        continue;
                    {
                        std::shared_lock<std::shared_timed_mutex> lifecycleLock(s_dmaLifecycleMutex);
                        if (cameraHandle) {
                            CloseTrackedScatterHandle(mem, cameraHandle);
                            cameraHandle = nullptr;
                        }
                    }
                    s_cameraWorkerPaused.store(true, std::memory_order_release);
                    while (s_cameraWorkerPauseRequests.load(std::memory_order_acquire) != 0u &&
                           !stopToken.stop_requested()) {
                        if (RecoverOrphanedCameraWorkerPauseRequest())
                            break;
                        std::this_thread::sleep_for(std::chrono::microseconds(500));
                    }
                    s_cameraWorkerPaused.store(false, std::memory_order_release);
                    previousCycleStart = Clock::time_point::min();
                    continue;
                }

                std::shared_lock<std::shared_timed_mutex> lifecycleLock(s_dmaLifecycleMutex);
                if (s_cameraWorkerPauseRequests.load(std::memory_order_acquire) != 0u)
                    continue;

                const auto cycleStart = Clock::now();
                const bool readCamera = esp::worker::ShouldReadLiveCamera(
                    g::clientBase.load(std::memory_order_relaxed) != 0,
                    s_engineStatusResolved.load(std::memory_order_relaxed),
                    s_engineInGame.load(std::memory_order_relaxed),
                    s_engineMenu.load(std::memory_order_relaxed),
                    s_dmaRecovering.load(std::memory_order_relaxed));
                const int targetHz = readCamera ? CAMERA_WORKER_HZ : esp::worker::kCameraIdlePollHz;
                const auto selectedInterval = std::chrono::microseconds(1000000 / targetHz);
                if (selectedInterval != tickInterval) {
                    tickInterval = selectedInterval;
                    previousCycleStart = Clock::time_point::min();
                    nextTick = cycleStart;
                }
                s_cameraWorkerTargetHz.store(targetHz, std::memory_order_relaxed);
                s_cameraReadsEnabled.store(readCamera, std::memory_order_relaxed);
                if (previousCycleStart != Clock::time_point::min() &&
                    esp::worker::IsWorkerScheduleSkipped(
                        previousCycleStart,
                        cycleStart,
                        tickInterval)) {
                    s_cameraWorkerDeadlineMissCount.fetch_add(
                        1,
                        std::memory_order_relaxed);
                }
                previousCycleStart = cycleStart;
                const auto& ofs = runtime_offsets::Get();
                const uintptr_t clientBase = g::clientBase;
                const uint64_t nowUs = TickNowUs();
                const uint64_t sampleSceneSerial =
                    s_sceneResetSerial.load(std::memory_order_relaxed);
                if (cachedSceneSerial != sampleSceneSerial) {
                    idleSnapshotCleared = false;
                    cachedSceneSerial = sampleSceneSerial;
                    cachedLocalPawn = 0;
                    cachedLocalSceneNode = 0;
                    consecutiveLocalPosMisses = 0;
                    consecutiveViewMisses = 0;
                    lastSuccessfulViewReadUs = 0;
                }
                const bool dmaRecoveringNow = s_dmaRecovering.load(std::memory_order_relaxed);

                if (dmaRecoveringNow) {
                    if (cameraHandle) {
                        CloseTrackedScatterHandle(mem, cameraHandle);
                        cameraHandle = nullptr;
                    }
                    consecutiveViewMisses = 0;
                    consecutiveLocalPosMisses = 0;
                    lastSuccessfulViewReadUs = 0;
                    cachedLocalPawn = 0;
                    cachedLocalSceneNode = 0;
                    cameraHandleInvalidatedByRecovery = true;
                    SetSubsystemUnknown(RuntimeSubsystem::CameraView);
                    ResetCameraSnapshot();
                } else if (readCamera && clientBase) {
                    idleSnapshotCleared = false;
                    if (cameraHandleInvalidatedByRecovery) {
                        if (cameraHandle)
                            CloseTrackedScatterHandle(mem, cameraHandle);
                        cameraHandle = CreateTrackedScatterHandle(mem);
                        cameraHandleInvalidatedByRecovery = false;
                    }
                    view_matrix_t liveViewMatrix = {};
                    Vector3 liveViewAngles = {};
                    uintptr_t liveLocalPawn = 0;
                    uintptr_t liveLocalSceneNode = 0;
                    Vector3 liveLocalPos = {};
                    uintptr_t rawLocalPawn = 0;
                    uintptr_t rawCachedPawnSceneNode = 0;
                    Vector3 cachedSceneNodePos = {};
                    DWORD liveViewMatrixBytesRead = 0;
                    DWORD liveViewAnglesBytesRead = 0;
                    DWORD localPawnBytesRead = 0;
                    DWORD cachedSceneNodeBytesRead = 0;
                    DWORD cachedPositionBytesRead = 0;

                    bool gotMatrix = false;
                    bool gotAngles = false;
                    bool gotLocalPos = false;
                    bool localPawnReadComplete = false;
                    bool sceneNodeReadComplete = false;
                    auto recreateCameraHandle = [&]() {
                        if (cameraHandle)
                            CloseTrackedScatterHandle(mem, cameraHandle);
                        cameraHandle = CreateTrackedScatterHandle(mem);
                    };
                    auto isValidLivePos = [](const Vector3& pos) -> bool {
                        return
                            std::isfinite(pos.x) &&
                            std::isfinite(pos.y) &&
                            std::isfinite(pos.z) &&
                            (std::fabs(pos.x) + std::fabs(pos.y) + std::fabs(pos.z) > 1.0f);
                    };
                    auto sanitizeCameraPointer = [](uintptr_t value) -> uintptr_t {
                        return app::memory_address::SanitizeGamePointer(value);
                    };
                    
                    
                    
                    
                    
                    
                    
                    auto isLikelyViewMatrix = [](const view_matrix_t& matrix) -> bool {
                        return esp::IsLikelyViewMatrix(matrix);
                    };

                    if (!cameraHandle)
                        cameraHandle = CreateTrackedScatterHandle(mem);

                    bool primaryScatterOk = false;
                    if (cameraHandle) {
                        bool queuedPrimary = false;
                        if (ofs.dwViewMatrix > 0) {
                            mem.AddScatterReadRequest(cameraHandle, clientBase + ofs.dwViewMatrix, &liveViewMatrix, sizeof(view_matrix_t), &liveViewMatrixBytesRead);
                            queuedPrimary = true;
                        }
                        if (ofs.dwViewAngles > 0) {
                            mem.AddScatterReadRequest(cameraHandle, clientBase + ofs.dwViewAngles, &liveViewAngles, sizeof(Vector3), &liveViewAnglesBytesRead);
                            queuedPrimary = true;
                        }
                        if (ofs.dwLocalPlayerPawn > 0) {
                            mem.AddScatterReadRequest(cameraHandle, clientBase + ofs.dwLocalPlayerPawn, &rawLocalPawn, sizeof(uintptr_t), &localPawnBytesRead);
                            queuedPrimary = true;
                        }
                        if (cachedLocalPawn && ofs.C_BaseEntity_m_pGameSceneNode > 0) {
                            mem.AddScatterReadRequest(
                                cameraHandle,
                                cachedLocalPawn + ofs.C_BaseEntity_m_pGameSceneNode,
                                &rawCachedPawnSceneNode,
                                sizeof(uintptr_t),
                                &cachedSceneNodeBytesRead);
                            queuedPrimary = true;
                        }
                        if (cachedLocalSceneNode && ofs.CGameSceneNode_m_vecAbsOrigin > 0) {
                            mem.AddScatterReadRequest(
                                cameraHandle,
                                cachedLocalSceneNode + ofs.CGameSceneNode_m_vecAbsOrigin,
                                &cachedSceneNodePos,
                                sizeof(Vector3),
                                &cachedPositionBytesRead);
                            queuedPrimary = true;
                        }
                        primaryScatterOk = queuedPrimary && mem.ExecuteReadScatter(cameraHandle);
                        if (!primaryScatterOk)
                            recreateCameraHandle();
                    }

                    if (primaryScatterOk) {
                        gotMatrix =
                            (ofs.dwViewMatrix > 0) &&
                            liveViewMatrixBytesRead == sizeof(liveViewMatrix) &&
                            isLikelyViewMatrix(liveViewMatrix);
                        gotAngles = (ofs.dwViewAngles > 0) &&
                            liveViewAnglesBytesRead == sizeof(liveViewAngles) &&
                            std::isfinite(liveViewAngles.x) &&
                            std::isfinite(liveViewAngles.y) &&
                            std::isfinite(liveViewAngles.z) &&
                            std::fabs(liveViewAngles.x) <= 180.0f &&
                            std::fabs(liveViewAngles.y) <= 720.0f &&
                            std::fabs(liveViewAngles.z) <= 90.0f;
                        localPawnReadComplete =
                            app::memory_address::IsCompleteGamePointerSample(
                                rawLocalPawn, localPawnBytesRead);
                        if (localPawnReadComplete)
                            liveLocalPawn = sanitizeCameraPointer(rawLocalPawn);
                        const uintptr_t freshCachedSceneNode = sanitizeCameraPointer(rawCachedPawnSceneNode);
                        if (liveLocalPawn && liveLocalPawn == cachedLocalPawn) {
                            sceneNodeReadComplete =
                                app::memory_address::IsCompleteGamePointerSample(
                                    rawCachedPawnSceneNode, cachedSceneNodeBytesRead);
                            if (sceneNodeReadComplete)
                                liveLocalSceneNode = freshCachedSceneNode;
                            if (liveLocalSceneNode &&
                                liveLocalSceneNode == cachedLocalSceneNode &&
                                cachedPositionBytesRead == sizeof(cachedSceneNodePos) &&
                                isValidLivePos(cachedSceneNodePos)) {
                                liveLocalPos = cachedSceneNodePos;
                                gotLocalPos = true;
                            }
                        }
                    }

                    if (!sceneNodeReadComplete &&
                        liveLocalPawn &&
                        ofs.C_BaseEntity_m_pGameSceneNode > 0 &&
                        cameraHandle) {
                        uintptr_t rawLiveSceneNode = 0;
                        DWORD liveSceneNodeBytesRead = 0;
                        mem.AddScatterReadRequest(
                            cameraHandle,
                            liveLocalPawn + ofs.C_BaseEntity_m_pGameSceneNode,
                            &rawLiveSceneNode,
                            sizeof(uintptr_t),
                            &liveSceneNodeBytesRead);
                        if (mem.ExecuteReadScatter(cameraHandle)) {
                            const uintptr_t sanitized = sanitizeCameraPointer(rawLiveSceneNode);
                            sceneNodeReadComplete =
                                app::memory_address::IsCompleteGamePointerSample(
                                    rawLiveSceneNode, liveSceneNodeBytesRead);
                            if (sceneNodeReadComplete)
                                liveLocalSceneNode = sanitized;
                        } else {
                            recreateCameraHandle();
                        }
                    }

                    if (!gotLocalPos &&
                        liveLocalSceneNode &&
                        ofs.CGameSceneNode_m_vecAbsOrigin > 0 &&
                        cameraHandle) {
                        DWORD livePositionBytesRead = 0;
                        mem.AddScatterReadRequest(
                            cameraHandle,
                            liveLocalSceneNode + ofs.CGameSceneNode_m_vecAbsOrigin,
                            &liveLocalPos,
                            sizeof(Vector3),
                            &livePositionBytesRead);
                        if (mem.ExecuteReadScatter(cameraHandle)) {
                            gotLocalPos = livePositionBytesRead == sizeof(liveLocalPos) &&
                                isValidLivePos(liveLocalPos);
                        } else {
                            recreateCameraHandle();
                        }
                    }

                    if (localPawnReadComplete && liveLocalPawn != cachedLocalPawn) {
                        cachedLocalPawn = liveLocalPawn;
                        cachedLocalSceneNode = 0;
                    }
                    if (sceneNodeReadComplete)
                        cachedLocalSceneNode = liveLocalSceneNode;
                    if (localPawnReadComplete && !liveLocalPawn)
                        cachedLocalSceneNode = 0;

                    if (gotMatrix) {
                        consecutiveViewMisses = 0;
                        lastSuccessfulViewReadUs = nowUs;
                    } else {
                        ++consecutiveViewMisses;
                    }

                    if (gotLocalPos)
                        consecutiveLocalPosMisses = 0;
                    else
                        ++consecutiveLocalPosMisses;

                    {
                        CameraFrame frame = {};
                        ReadCameraFrame(frame);
                        const uint64_t currentSceneSerial =
                            s_sceneResetSerial.load(std::memory_order_relaxed);
                        if (frame.sceneSerial != sampleSceneSerial)
                            frame = {};
                        // A confirmed identity change invalidates the held position
                        // immediately. An incomplete read keeps its original age.
                        if (localPawnReadComplete && frame.localPosPawn != liveLocalPawn) {
                            frame.localPos = {};
                            frame.localPosPawn = 0;
                            frame.localPosValid = false;
                            frame.localPosUpdatedUs = 0;
                        }
                        if (gotMatrix)
                            memcpy(&frame.viewMatrix, &liveViewMatrix, sizeof(view_matrix_t));
                        if (gotAngles)
                            frame.viewAngles = liveViewAngles;
                        if (gotLocalPos)
                            frame.localPos = liveLocalPos;

                        if (gotMatrix) {
                            frame.viewValid = true;
                            frame.viewUpdatedUs = nowUs;
                        } else if (consecutiveViewMisses >= kCameraInvalidateMissThreshold) {
                            frame.viewValid = false;
                        }
                        if (gotAngles) {
                            frame.viewAnglesValid = true;
                            frame.viewAnglesUpdatedUs = nowUs;
                        } else if (!frame.viewAnglesValid ||
                                   frame.viewAnglesUpdatedUs == 0 ||
                                   nowUs < frame.viewAnglesUpdatedUs ||
                                   nowUs - frame.viewAnglesUpdatedUs >
                                       kLiveCameraFreshnessUs) {
                            // Keep the last complete angle sample across a
                            // transient scatter miss, but never refresh its
                            // timestamp. Target's 20-35 ms age/skew gates
                            // still reject genuinely stale camera data.
                            frame.viewAnglesValid = false;
                            frame.viewAnglesUpdatedUs = 0;
                        }

                        if (gotLocalPos) {
                            frame.localPosPawn = liveLocalPawn;
                            frame.localPosValid = true;
                            frame.localPosUpdatedUs = nowUs;
                        } else if (consecutiveLocalPosMisses >= kCameraInvalidateMissThreshold) {
                            frame.localPosValid = false;
                        }
                        frame.sceneSerial = sampleSceneSerial;
                        // Never label a read started in the previous scene as new.
                        if (sampleSceneSerial != currentSceneSerial ||
                            !s_engineStatusResolved.load(std::memory_order_relaxed) ||
                            !s_engineInGame.load(std::memory_order_relaxed) ||
                            s_engineMenu.load(std::memory_order_relaxed)) {
                            frame = {};
                            frame.sceneSerial = currentSceneSerial;
                        }
                        PublishCameraFrame(frame);
                    }

                    const auto sceneWarmupState =
                        static_cast<esp::SceneWarmupState>(s_sceneWarmupState.load(std::memory_order_relaxed));
                    const bool cameraRecoveryAllowed =
                        sceneWarmupState == esp::SceneWarmupState::Stable &&
                        sampleSceneSerial == s_sceneResetSerial.load(std::memory_order_relaxed) &&
                        s_engineStatusResolved.load(std::memory_order_relaxed) &&
                        !s_engineMenu.load(std::memory_order_relaxed) &&
                        s_engineInGame.load(std::memory_order_relaxed);
                    if (!cameraRecoveryAllowed) {
                        SetSubsystemUnknown(RuntimeSubsystem::CameraView);
                    } else if (gotMatrix) {
                        // The view matrix is the required camera signal. Local position is
                        // optional while dead or spectating and must not degrade rendering.
                        MarkSubsystemHealthy(RuntimeSubsystem::CameraView, nowUs);
                    } else if (consecutiveViewMisses >= kCameraInvalidateMissThreshold) {
                        MarkSubsystemFailed(RuntimeSubsystem::CameraView, nowUs);
                    } else {
                        MarkSubsystemDegraded(RuntimeSubsystem::CameraView, nowUs);
                    }
                    const bool cameraSampleFresh =
                        esp::worker::IsCameraSampleFresh(
                            lastSuccessfulViewReadUs,
                            nowUs);
                    if (consecutiveViewMisses >= kCameraRecoveryMissThreshold &&
                        cameraRecoveryAllowed) {
                        const uint64_t currentSessionGeneration =
                            s_dmaSessionGeneration.load(std::memory_order_relaxed);
                        if (cameraSelfHealSessionGeneration !=
                            currentSessionGeneration) {
                            cameraSelfHealSessionGeneration =
                                currentSessionGeneration;
                            lastCameraSelfHealUs = 0;
                        }
                        if (esp::worker::ShouldRunCameraSelfHeal(
                                consecutiveViewMisses,
                                kCameraRecoveryMissThreshold,
                                cameraRecoveryAllowed,
                                lastCameraSelfHealUs,
                                nowUs)) {
                            lastCameraSelfHealUs = nowUs;
                            recreateCameraHandle();
                            RefreshDmaCaches(
                                "camera_self_heal",
                                DmaRefreshTier::Probe,
                                false);
                        }

                        if (esp::worker::ShouldRequestPersistentCameraRecovery(
                                cameraSampleFresh,
                                consecutiveViewMisses,
                                kCameraRecoveryMissThreshold)) {
                            RequestDmaRecovery("camera_snapshot_stalled_persistent");
                        }
                    }
                } else {
                    if (cameraHandle) {
                        CloseTrackedScatterHandle(mem, cameraHandle);
                        cameraHandle = nullptr;
                    }
                    consecutiveViewMisses = 0;
                    consecutiveLocalPosMisses = 0;
                    lastSuccessfulViewReadUs = 0;
                    cachedLocalPawn = 0;
                    cachedLocalSceneNode = 0;
                    SetSubsystemUnknown(RuntimeSubsystem::CameraView);
                    if (!idleSnapshotCleared) {
                        ResetCameraSnapshot();
                        idleSnapshotCleared = true;
                    }
                }

                const auto cycleEnd = Clock::now();
                const uint64_t cycleUs = static_cast<uint64_t>(
                    std::chrono::duration_cast<std::chrono::microseconds>(cycleEnd - cycleStart).count());
                s_cameraWorkerCycleUs.store(cycleUs, std::memory_order_relaxed);
                uint64_t prevMax = s_cameraWorkerMaxCycleUs.load(std::memory_order_relaxed);
                while (cycleUs > prevMax &&
                       !s_cameraWorkerMaxCycleUs.compare_exchange_weak(prevMax, cycleUs, std::memory_order_relaxed))
                    ;
                const bool resetRecentWindow =
                    cycleEnd - recentPeakWindowStart >=
                    std::chrono::microseconds(esp::worker::kWorkerRecentPeakWindowUs);
                if (resetRecentWindow) {
                    recentPeakWindowStart = cycleEnd;
                    cycleLatencyBuckets.fill(0);
                    cycleLatencySamples = 0;
                    s_cameraWorkerRecentWindowStartUs.store(
                        nowUs,
                        std::memory_order_relaxed);
                    s_cameraWorkerRecentMaxCycleUs.store(cycleUs, std::memory_order_relaxed);
                    s_cameraWorkerDeadlineMissCount.store(0, std::memory_order_relaxed);
                    s_cameraWorkerCycleSampleCount.store(0, std::memory_order_relaxed);
                    s_cameraWorkerCycleOverBudgetCount.store(0, std::memory_order_relaxed);
                    s_cameraWorkerCycleOver5msCount.store(0, std::memory_order_relaxed);
                    s_cameraWorkerCycleOver16msCount.store(0, std::memory_order_relaxed);
                } else {
                    UpdatePeak(s_cameraWorkerRecentMaxCycleUs, cycleUs);
                }
                s_cameraWorkerCycleSampleCount.fetch_add(1, std::memory_order_relaxed);
                if (cycleUs > (1000000u / CAMERA_WORKER_HZ))
                    s_cameraWorkerCycleOverBudgetCount.fetch_add(1, std::memory_order_relaxed);
                if (cycleUs > 5000u)
                    s_cameraWorkerCycleOver5msCount.fetch_add(1, std::memory_order_relaxed);
                if (cycleUs > 16667u)
                    s_cameraWorkerCycleOver16msCount.fetch_add(1, std::memory_order_relaxed);
                ++cycleLatencyBuckets[esp::worker::LatencyBucketIndex(cycleUs)];
                ++cycleLatencySamples;
                if (cycleLatencySamples == 1u || (cycleLatencySamples % 64u) == 0u) {
                    s_cameraWorkerCycleP50Us.store(
                        esp::worker::LatencyPercentileUpperBound(
                            cycleLatencyBuckets, cycleLatencySamples, 50u),
                        std::memory_order_relaxed);
                    s_cameraWorkerCycleP95Us.store(
                        esp::worker::LatencyPercentileUpperBound(
                            cycleLatencyBuckets, cycleLatencySamples, 95u),
                        std::memory_order_relaxed);
                    s_cameraWorkerCycleP99Us.store(
                        esp::worker::LatencyPercentileUpperBound(
                            cycleLatencyBuckets, cycleLatencySamples, 99u),
                        std::memory_order_relaxed);
                }

                if (esp::worker::ShouldLogWorkerStall(cycleUs, lastCameraStallLogUs, nowUs)) {
                    lastCameraStallLogUs = nowUs;
                    DmaLogPrintf("[PERF] Camera Stall detected! total=%.2f ms", static_cast<double>(cycleUs) / 1000.0);
                }
            } catch (...) {
                consecutiveViewMisses = kCameraRecoveryMissThreshold;
                consecutiveLocalPosMisses = kCameraInvalidateMissThreshold;
                ResetCameraSnapshot();
                {
                    std::shared_lock<std::shared_timed_mutex> lifecycleLock(s_dmaLifecycleMutex);
                    if (cameraHandle)
                        CloseTrackedScatterHandle(mem, cameraHandle);
                    cameraHandle = CreateTrackedScatterHandle(mem);
                }
                const uint64_t nowUs = TickNowUs();
                if (esp::worker::IsWorkerCooldownElapsed(
                        lastCameraExceptionLogUs,
                        nowUs,
                        esp::worker::kWorkerStallLogCooldownUs)) {
                    lastCameraExceptionLogUs = nowUs;
                    DmaLogPrintf(
                        "[ERROR] CameraWorkerLoop: camera read exception caught, continuing");
                }
                s_dmaConsecutiveFailures.fetch_add(1, std::memory_order_relaxed);
                s_dmaTotalFailures.fetch_add(1, std::memory_order_relaxed);
                const auto sceneWarmupState =
                    static_cast<esp::SceneWarmupState>(s_sceneWarmupState.load(std::memory_order_relaxed));
                const bool cameraSampleFresh =
                    esp::worker::IsCameraSampleFresh(
                        lastSuccessfulViewReadUs,
                        nowUs);
                if (esp::worker::ShouldRequestCameraExceptionRecovery(
                        s_engineInGame.load(std::memory_order_relaxed),
                        sceneWarmupState == esp::SceneWarmupState::Stable,
                        cameraSampleFresh))
                    RequestDmaRecovery("camera_worker_exception_persistent");
                MarkSubsystemFailed(RuntimeSubsystem::CameraView, nowUs);
            }

            const auto scheduleNow = Clock::now();
            nextTick = esp::worker::NextWorkerDeadline(nextTick, scheduleNow, tickInterval);
            PreciseSleepUntil<Clock>(nextTick);
        }

        {
            std::shared_lock<std::shared_timed_mutex> lifecycleLock(s_dmaLifecycleMutex);
            if (cameraHandle) {
                CloseTrackedScatterHandle(mem, cameraHandle);
                cameraHandle = nullptr;
            }
        }
        s_cameraWorkerRunning.store(false, std::memory_order_relaxed);
        s_cameraReadsEnabled.store(false, std::memory_order_relaxed);
        s_cameraWorkerTargetHz.store(0, std::memory_order_relaxed);
    }
