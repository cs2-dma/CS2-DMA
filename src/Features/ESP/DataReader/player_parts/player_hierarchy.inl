        auto isValidPawnHandle = [&](uint32_t handleValue) -> bool {
            const uint32_t slot = handleValue & kEntityHandleMask;
            return handleValue != 0u &&
                   handleValue != 0xFFFFFFFFu &&
                   slot != 0u &&
                   slot != kEntityHandleMask;
        };
        
        
        static uintptr_t s_cachedControllers[64] = {};
        static uint32_t s_cachedPawnHandles[64] = {};
        static uintptr_t s_cachedPawnEntries[64] = {};
        static uintptr_t s_cachedPawns[64] = {};
        auto findHighestHierarchySlot = [&]() {
            for (int i = 63; i >= 0; --i) {
                if (controllers[i] ||
                    isValidPawnHandle(pawnHandles[i]) ||
                    pawns[i] ||
                    s_cachedControllers[i] ||
                    s_cachedPawns[i]) {
                    return i + 1;
                }
            }
            return 0;
        };

        
        
        static uint64_t s_hierarchyCacheResetSerial = 0;
        static bool s_controllerCacheWarmed = false;
        static uint64_t s_lastHierarchyRefreshUs = 0;
        static int s_playerDiscoveryCursor = 0;
        static bool s_entityStrideValidated = false;
        static uint64_t s_zeroControllerSinceUs = 0;
        static esp::data::EntityHierarchyRecoveryAction s_zeroControllerRecoveryStage =
            esp::data::EntityHierarchyRecoveryAction::None;
        static uint16_t s_hierarchyMissingStreaks[64] = {};
        static uint64_t s_hierarchyLastPresentUs[64] = {};
        static uint64_t s_bulkRecoveryStartUs = 0;
        const uint64_t controllerResetSerial = s_sceneResetSerial.load(std::memory_order_relaxed);
        if (s_hierarchyCacheResetSerial != controllerResetSerial) {
            s_hierarchyCacheResetSerial = controllerResetSerial;
            s_controllerCacheWarmed = false;
            s_lastHierarchyRefreshUs = 0;
            s_playerDiscoveryCursor = 0;
            s_entityStrideValidated = false;
            s_activeEntitySlotSize = kEntitySlotSize;
            s_zeroControllerSinceUs = 0;
            s_zeroControllerRecoveryStage =
                esp::data::EntityHierarchyRecoveryAction::None;
            s_bulkRecoveryStartUs = 0;
            s_lastBulkEvictionUs.store(0, std::memory_order_relaxed);
            s_playerHierarchyHeldSlotCount.store(0, std::memory_order_relaxed);
            memset(s_cachedControllers, 0, sizeof(s_cachedControllers));
            memset(s_cachedPawnHandles, 0, sizeof(s_cachedPawnHandles));
            memset(s_cachedPawnEntries, 0, sizeof(s_cachedPawnEntries));
            memset(s_cachedPawns, 0, sizeof(s_cachedPawns));
            memset(s_hierarchyMissingStreaks, 0, sizeof(s_hierarchyMissingStreaks));
            memset(s_hierarchyLastPresentUs, 0, sizeof(s_hierarchyLastPresentUs));
            s_playerDuplicateIdentityFilteredStat.store(0, std::memory_order_relaxed);
            s_playerBacklinkMismatchStat.store(0, std::memory_order_relaxed);
        }

        auto currentSceneWarmupState =
            static_cast<esp::SceneWarmupState>(s_sceneWarmupState.load(std::memory_order_relaxed));
        if (currentSceneWarmupState != esp::SceneWarmupState::Stable) {
            s_controllerCacheWarmed = false;
        }
        const bool hierarchyWarmupActive =
            !s_controllerCacheWarmed ||
            sceneSettling ||
            currentSceneWarmupState != esp::SceneWarmupState::Stable;
        const bool liveHierarchyContext =
            s_engineInGame.load(std::memory_order_relaxed) &&
            !s_engineMenu.load(std::memory_order_relaxed) &&
            s_engineSignOnState.load(std::memory_order_relaxed) == 6;

        const uint64_t hierarchyNowUs = TickNowUs();
        
        
        
        
        
        const uint64_t hierarchyRefreshIntervalUs =
            hierarchyWarmupActive
            ? esp::intervals::kHierarchyWarmupRefreshUs
            : esp::intervals::kHierarchySteadyRefreshUs;
        const bool hierarchyRefreshDue =
            !s_controllerCacheWarmed ||
            s_lastHierarchyRefreshUs == 0 ||
            (hierarchyNowUs - s_lastHierarchyRefreshUs) >= hierarchyRefreshIntervalUs;

        auto& prevCachedControllers = s_playerReadScratch.prevCachedControllers;
        auto& prevCachedPawnEntries = s_playerReadScratch.prevCachedPawnEntries;
        auto& prevCachedPawnHandles = s_playerReadScratch.prevCachedPawnHandles;
        auto& prevCachedPawns = s_playerReadScratch.prevCachedPawns;
        memcpy(prevCachedControllers, s_cachedControllers, sizeof(prevCachedControllers));
        memcpy(prevCachedPawnEntries, s_cachedPawnEntries, sizeof(prevCachedPawnEntries));
        memcpy(prevCachedPawnHandles, s_cachedPawnHandles, sizeof(prevCachedPawnHandles));
        memcpy(prevCachedPawns, s_cachedPawns, sizeof(prevCachedPawns));
        memcpy(controllers, s_cachedControllers, sizeof(controllers));
        memcpy(pawnHandles, s_cachedPawnHandles, sizeof(pawnHandles));
        memcpy(pawnEntries, s_cachedPawnEntries, sizeof(pawnEntries));
        memcpy(pawns, s_cachedPawns, sizeof(pawns));

        auto& playerRefreshSlots = s_playerReadScratch.playerRefreshSlots;
        int playerRefreshSlotCount = 0;
        auto& playerRefreshSlotMask = s_playerReadScratch.playerRefreshSlotMask;
        auto pushRefreshSlot = [&](int idx) {
            if (idx < 0 || idx >= 64 || playerRefreshSlotMask[idx] || playerRefreshSlotCount >= 64)
                return;
            playerRefreshSlotMask[idx] = true;
            playerRefreshSlots[playerRefreshSlotCount++] = idx;
        };
        for (int i = 0; i < 64; ++i) {
            const bool trackedSlot =
                s_players[i].valid ||
                s_players[i].pawn != 0 ||
                s_webRadarPlayers[i].valid ||
                s_webRadarPlayers[i].pawn != 0 ||
                s_cachedControllers[i] != 0 ||
                s_cachedPawns[i] != 0;
            if (trackedSlot)
                pushRefreshSlot(i);
        }
        if (localPlayerSlotHint > 0)
            pushRefreshSlot(localPlayerSlotHint - 1);

        constexpr int kPlayerDiscoverySlotLimit = 64;
        if (kPlayerDiscoverySlotLimit > 0) {
            const int discoveryBudget =
                esp::data::SelectHierarchyDiscoveryBudget(
                    hierarchyWarmupActive,
                    forceFullPlayerDiscoverySweep);
            int scanned = 0;
            while (scanned < kPlayerDiscoverySlotLimit &&
                   playerRefreshSlotCount < 64 &&
                   scanned < discoveryBudget) {
                const int idx = (s_playerDiscoveryCursor + scanned) % kPlayerDiscoverySlotLimit;
                if (!playerRefreshSlotMask[idx])
                    pushRefreshSlot(idx);
                ++scanned;
            }
            s_playerDiscoveryCursor = (s_playerDiscoveryCursor + std::max(1, scanned)) % kPlayerDiscoverySlotLimit;
        } else {
            s_playerDiscoveryCursor = 0;
        }

        if (hierarchyRefreshDue && playerRefreshSlotCount > 0) {
            _playerHierarchyActiveTick = true;
            int hierarchyHeldSlotCount = 0;
            auto validateControllersForRefresh = [&]() {
                for (int slotIdx = 0; slotIdx < playerRefreshSlotCount; ++slotIdx) {
                    const int i = playerRefreshSlots[slotIdx];
                    if (!isLikelyGamePointer(controllers[i]))
                        controllers[i] = 0;
                }
            };
            auto retryMissingControllersWithFallback = [&]() {
                if (kEntitySlotSizeFallback == kEntitySlotSize ||
                    !esp::data::ShouldProbeFallbackEntityStride(
                        s_entityStrideValidated)) {
                    return;
                }
                bool queuedFallback = false;
                for (int slotIdx = 0; slotIdx < playerRefreshSlotCount; ++slotIdx) {
                    const int i = playerRefreshSlots[slotIdx];
                    if (controllers[i])
                        continue;
                    const int controllerSlot = i + 1;
                    mem.AddScatterReadRequest(handle,
                        listEntry + static_cast<uintptr_t>(kEntitySlotSizeFallback) *
                            static_cast<uintptr_t>(controllerSlot & kEntitySlotMask),
                        &controllers[i], sizeof(uintptr_t));
                    queuedFallback = true;
                }
                if (queuedFallback) {
                    if (!executeOptionalScatterRead())
                        logUpdateDataIssue("scatter_5_6_fallback", "controller_array_fallback_stride_failed");
                    validateControllersForRefresh();
                }
            };
            auto validatePawnsForRefresh = [&]() {
                for (int slotIdx = 0; slotIdx < playerRefreshSlotCount; ++slotIdx) {
                    const int i = playerRefreshSlots[slotIdx];
                    if (!isLikelyGamePointer(pawns[i]))
                        pawns[i] = 0;
                }
            };
            auto retryMissingPawnsWithFallback = [&]() {
                if (kEntitySlotSizeFallback == kEntitySlotSize ||
                    !esp::data::ShouldProbeFallbackEntityStride(
                        s_entityStrideValidated)) {
                    return;
                }
                bool queuedFallback = false;
                for (int slotIdx = 0; slotIdx < playerRefreshSlotCount; ++slotIdx) {
                    const int i = playerRefreshSlots[slotIdx];
                    if (pawns[i] || !pawnEntries[i] || !isValidPawnHandle(pawnHandles[i]))
                        continue;
                    const uint32_t slot = pawnHandles[i] & kEntitySlotMask;
                    mem.AddScatterReadRequest(handle,
                        pawnEntries[i] + static_cast<uintptr_t>(kEntitySlotSizeFallback) *
                            static_cast<uintptr_t>(slot),
                        &pawns[i], sizeof(uintptr_t));
                    queuedFallback = true;
                }
                if (queuedFallback) {
                    if (!executeOptionalScatterRead())
                        logUpdateDataIssue("scatter_9_fallback", "pawn_pointer_fallback_stride_failed");
                    validatePawnsForRefresh();
                }
            };

            // Read the contiguous controller slot window in one DMA request when possible.
            bool bulkReadDone = false;
            alignas(8) uint8_t preferredBulkBuf[120 * 65] = {};
            alignas(8) uint8_t alternateBulkBuf[120 * 65] = {};
            const uint32_t preferredStride =
                s_activeEntitySlotSize == kEntitySlotSizeFallback
                    ? kEntitySlotSizeFallback
                    : kEntitySlotSize;
            const uint32_t alternateStride =
                preferredStride == kEntitySlotSize
                    ? kEntitySlotSizeFallback
                    : kEntitySlotSize;
            uint32_t activeStride = preferredStride;
            const uint8_t* selectedBulkBuf = nullptr;
            bool readSuccess = false;

            auto probeControllerWindow = [&](uint32_t stride, uint8_t* buffer) {
                esp::data::ControllerWindowProbe result = {};
                if (!listEntry ||
                    !mem.Read(listEntry, buffer, static_cast<size_t>(stride) * 65u)) {
                    return result;
                }

                result.read = true;
                for (int slot = 1; slot <= 64; ++slot) {
                    uintptr_t value = 0;
                    memcpy(
                        &value,
                        &buffer[static_cast<size_t>(stride) * static_cast<size_t>(slot)],
                        sizeof(value));
                    if (isLikelyGamePointer(value))
                        ++result.validPointers;
                    if (localController != 0 && value == localController)
                        result.localMatched = true;
                }
                return result;
            };

            const esp::data::ControllerWindowProbe preferredProbe =
                probeControllerWindow(preferredStride, preferredBulkBuf);
            esp::data::ControllerWindowProbe alternateProbe = {};
            if (esp::data::ShouldProbeAlternateControllerWindow(
                    s_entityStrideValidated,
                    preferredProbe,
                    localController != 0) &&
                alternateStride != preferredStride) {
                alternateProbe =
                    probeControllerWindow(alternateStride, alternateBulkBuf);
            }

            const int selectedProbe =
                esp::data::SelectControllerWindowProbe(
                    preferredProbe,
                    alternateProbe);
            if (selectedProbe == 0) {
                activeStride = preferredStride;
                selectedBulkBuf = preferredBulkBuf;
            } else if (selectedProbe == 1) {
                activeStride = alternateStride;
                selectedBulkBuf = alternateBulkBuf;
            }

            readSuccess = selectedBulkBuf != nullptr;
            if (readSuccess) {
                if ((preferredProbe.read && alternateProbe.read) ||
                    preferredProbe.localMatched ||
                    alternateProbe.localMatched) {
                    s_entityStrideValidated = true;
                }
                s_activeEntitySlotSize = activeStride;
                for (int i = 0; i < 64; ++i) {
                    const int controllerSlot = i + 1;
                    uintptr_t val = 0;
                    memcpy(
                        &val,
                        &selectedBulkBuf[
                            static_cast<size_t>(s_activeEntitySlotSize) *
                            static_cast<size_t>(controllerSlot)],
                        sizeof(uintptr_t));
                    if (isLikelyGamePointer(val)) {
                        controllers[i] = val;
                    } else {
                        controllers[i] = 0;
                    }
                }
                bulkReadDone = true;
            }
            if (bulkReadDone) {
                // The contiguous window already discovered every live
                // controller. Add only those slots to the dependent pawn
                // chain, while tracked slots remain present for eviction.
                for (int i = 0; i < 64; ++i) {
                    if (controllers[i])
                        pushRefreshSlot(i);
                }
            }
            bool hadCachedControllerReads = false;
            bool queuedInitialHierarchyReads = false;
            for (int slotIdx = 0; slotIdx < playerRefreshSlotCount; ++slotIdx) {
                const int i = playerRefreshSlots[slotIdx];
                if (!bulkReadDone) {
                    const int controllerSlot = i + 1;
                    mem.AddScatterReadRequest(handle, listEntry + s_activeEntitySlotSize * (controllerSlot & kEntitySlotMask), &controllers[i], sizeof(uintptr_t));
                    queuedInitialHierarchyReads = true;
                }
                if (!prevCachedControllers[i])
                    continue;
                mem.AddScatterReadRequest(handle, prevCachedControllers[i] + ofs.CCSPlayerController_m_hPlayerPawn, &pawnHandles[i], sizeof(uint32_t));
                hadCachedControllerReads = true;
                queuedInitialHierarchyReads = true;
            }
            if (queuedInitialHierarchyReads &&
                !mem.ExecuteReadScatter(handle)) {
                logUpdateDataIssue("scatter_5_6", "controller_array_unavailable_using_cached_controllers");
            }

            validateControllersForRefresh();
            bool anyControllerRetry = false;
            for (int slotIdx = 0; slotIdx < playerRefreshSlotCount; ++slotIdx) {
                const int i = playerRefreshSlots[slotIdx];
                if (controllers[i] ||
                    !esp::data::ShouldRetryMissingControllerSlot(
                        bulkReadDone,
                        s_controllerCacheWarmed,
                        forceFullPlayerDiscoverySweep,
                        prevCachedControllers[i] != 0)) {
                    continue;
                }
                const int controllerSlot = i + 1;
                mem.AddScatterReadRequest(
                    handle,
                    listEntry +
                        s_activeEntitySlotSize *
                            (controllerSlot & kEntitySlotMask),
                    &controllers[i],
                    sizeof(uintptr_t));
                anyControllerRetry = true;
            }
            if (anyControllerRetry) {
                executeOptionalScatterRead();
                validateControllersForRefresh();
            }
            retryMissingControllersWithFallback();

            {
                bool anyNew = false;
                for (int i = 0; i < 64; ++i) {
                    s_cachedControllers[i] = controllers[i];
                    if (controllers[i]) {
                        anyNew = true;
                    }
                }

                if (!anyNew) {
                    if (s_zeroControllerSinceUs == 0)
                        s_zeroControllerSinceUs = hierarchyNowUs;
                    const bool liveHierarchyExpected =
                        s_engineInGame.load(std::memory_order_relaxed) &&
                        !s_engineMenu.load(std::memory_order_relaxed) &&
                        entityList != 0 &&
                        listEntry != 0;
                    if (!sceneSettling && liveHierarchyExpected) {
                        const auto zeroControllerAction =
                            esp::data::SelectZeroControllerRecovery(
                                s_zeroControllerSinceUs,
                                s_zeroControllerRecoveryStage,
                                hierarchyNowUs);
                        if (zeroControllerAction !=
                            esp::data::EntityHierarchyRecoveryAction::None) {
                            s_zeroControllerRecoveryStage = zeroControllerAction;
                            if (zeroControllerAction ==
                                esp::data::EntityHierarchyRecoveryAction::Full) {
                                refreshDmaCaches("stale_tlb_no_controllers_full", DmaRefreshTier::Full);
                            } else if (zeroControllerAction ==
                                       esp::data::EntityHierarchyRecoveryAction::Repair) {
                                refreshDmaCaches("stale_tlb_no_controllers_repair", DmaRefreshTier::Repair);
                            } else {
                                refreshDmaCaches("stale_tlb_no_controllers_probe", DmaRefreshTier::Probe);
                            }
                        }
                    }
                } else {
                    s_zeroControllerSinceUs = 0;
                    s_zeroControllerRecoveryStage =
                        esp::data::EntityHierarchyRecoveryAction::None;
                }
            }

            {
                bool controllersChanged = !hadCachedControllerReads;
                if (!controllersChanged) {
                    for (int slotIdx = 0; slotIdx < playerRefreshSlotCount; ++slotIdx) {
                        const int i = playerRefreshSlots[slotIdx];
                        if (controllers[i] != prevCachedControllers[i] ||
                            (controllers[i] && pawnHandles[i] != s_cachedPawnHandles[i]) ||
                            (controllers[i] && !isValidPawnHandle(pawnHandles[i]))) {
                            controllersChanged = true;
                            break;
                        }
                    }
                }
                if (controllersChanged) {
                    bool queuedFreshControllerReads = false;
                    for (int slotIdx = 0; slotIdx < playerRefreshSlotCount; ++slotIdx) {
                        const int i = playerRefreshSlots[slotIdx];
                        if (!controllers[i])
                            continue;
                        mem.AddScatterReadRequest(handle, controllers[i] + ofs.CCSPlayerController_m_hPlayerPawn, &pawnHandles[i], sizeof(uint32_t));
                        queuedFreshControllerReads = true;
                    }
                    if (queuedFreshControllerReads && !mem.ExecuteReadScatter(handle))
                        logUpdateDataIssue("scatter_6_refresh", "controller_pawn_data_refresh_failed");
                }
            }

            
            
            
            
            for (int slotIdx = 0; slotIdx < playerRefreshSlotCount; ++slotIdx) {
                const int i = playerRefreshSlots[slotIdx];
                if (controllers[i])
                    continue;
                pawnHandles[i] = 0;
                pawnEntries[i] = 0;
                pawns[i] = 0;
            }

            {
                bool anyPawnHandleRepair = false;
                for (int slotIdx = 0; slotIdx < playerRefreshSlotCount; ++slotIdx) {
                    const int i = playerRefreshSlots[slotIdx];
                    if (!controllers[i] || isValidPawnHandle(pawnHandles[i]))
                        continue;
                    mem.AddScatterReadRequest(handle,
                        controllers[i] + ofs.CCSPlayerController_m_hPlayerPawn,
                        &pawnHandles[i], sizeof(uint32_t));
                    anyPawnHandleRepair = true;
                }
                if (anyPawnHandleRepair)
                    executeOptionalScatterRead();
            }

            for (int slotIdx = 0; slotIdx < playerRefreshSlotCount; ++slotIdx) {
                const int i = playerRefreshSlots[slotIdx];
                if (isValidPawnHandle(pawnHandles[i]))
                    continue;
                pawnEntries[i] = 0;
                pawns[i] = 0;
            }

            bool queuedMergedPawnReads = false;
            for (int slotIdx = 0; slotIdx < playerRefreshSlotCount; ++slotIdx) {
                const int i = playerRefreshSlots[slotIdx];
                if (!isValidPawnHandle(pawnHandles[i]))
                    continue;
                const uint32_t block = (pawnHandles[i] & kEntityHandleMask) >> 9;
                mem.AddScatterReadRequest(handle, entityList + 0x10 + 8 * block, &pawnEntries[i], sizeof(uintptr_t));
                queuedMergedPawnReads = true;
            }
            for (int slotIdx = 0; slotIdx < playerRefreshSlotCount; ++slotIdx) {
                const int i = playerRefreshSlots[slotIdx];
                if (!prevCachedPawnEntries[i] || !isValidPawnHandle(pawnHandles[i]))
                    continue;
                const uint32_t slot = pawnHandles[i] & kEntitySlotMask;
                mem.AddScatterReadRequest(handle, prevCachedPawnEntries[i] + s_activeEntitySlotSize * slot, &pawns[i], sizeof(uintptr_t));
                queuedMergedPawnReads = true;
            }
            if (queuedMergedPawnReads && !mem.ExecuteReadScatter(handle))
                logUpdateDataIssue("scatter_8_9", "entries_pawns_unavailable");

            for (int slotIdx = 0; slotIdx < playerRefreshSlotCount; ++slotIdx) {
                const int i = playerRefreshSlots[slotIdx];
                if (!isLikelyGamePointer(pawnEntries[i]))
                    pawnEntries[i] = 0;
            }

            {
                bool anyPawnEntryRepair = false;
                for (int slotIdx = 0; slotIdx < playerRefreshSlotCount; ++slotIdx) {
                    const int i = playerRefreshSlots[slotIdx];
                    if (pawnEntries[i] || !isValidPawnHandle(pawnHandles[i]))
                        continue;
                    const uint32_t block = (pawnHandles[i] & kEntityHandleMask) >> 9;
                    mem.AddScatterReadRequest(handle,
                        entityList + 0x10 + 8 * block,
                        &pawnEntries[i], sizeof(uintptr_t));
                    anyPawnEntryRepair = true;
                }
                if (anyPawnEntryRepair) {
                    executeOptionalScatterRead();
                    for (int slotIdx = 0; slotIdx < playerRefreshSlotCount; ++slotIdx) {
                        const int i = playerRefreshSlots[slotIdx];
                        if (!isLikelyGamePointer(pawnEntries[i]))
                            pawnEntries[i] = 0;
                    }
                }
            }

            for (int slotIdx = 0; slotIdx < playerRefreshSlotCount; ++slotIdx) {
                const int i = playerRefreshSlots[slotIdx];
                if (pawnEntries[i])
                    continue;
                pawns[i] = 0;
            }

            {
                bool entriesChanged = false;
                for (int slotIdx = 0; slotIdx < playerRefreshSlotCount; ++slotIdx) {
                    const int i = playerRefreshSlots[slotIdx];
                    if (pawnEntries[i] != prevCachedPawnEntries[i]) {
                        entriesChanged = true;
                        break;
                    }
                }
                if (entriesChanged) {
                    bool queuedFreshPawnReads = false;
                    for (int slotIdx = 0; slotIdx < playerRefreshSlotCount; ++slotIdx) {
                        const int i = playerRefreshSlots[slotIdx];
                        if (!pawnEntries[i] || !isValidPawnHandle(pawnHandles[i]))
                            continue;
                        const uint32_t slot = pawnHandles[i] & kEntitySlotMask;
                        mem.AddScatterReadRequest(handle, pawnEntries[i] + s_activeEntitySlotSize * slot, &pawns[i], sizeof(uintptr_t));
                        queuedFreshPawnReads = true;
                    }
                    if (queuedFreshPawnReads && !mem.ExecuteReadScatter(handle))
                        logUpdateDataIssue("scatter_9_refresh", "pawn_pointers_refresh_failed");
                }
            }

            validatePawnsForRefresh();
            {
                bool anyPawnRepair = false;
                for (int slotIdx = 0; slotIdx < playerRefreshSlotCount; ++slotIdx) {
                    const int i = playerRefreshSlots[slotIdx];
                    if (pawns[i] || !pawnEntries[i] || !isValidPawnHandle(pawnHandles[i]))
                        continue;
                    const uint32_t slot = pawnHandles[i] & kEntitySlotMask;
                    mem.AddScatterReadRequest(handle,
                        pawnEntries[i] + s_activeEntitySlotSize * slot,
                        &pawns[i], sizeof(uintptr_t));
                    anyPawnRepair = true;
                }
                if (anyPawnRepair) {
                    executeOptionalScatterRead();
                    validatePawnsForRefresh();
                }
            }
            retryMissingPawnsWithFallback();

            {
                
                
                
                
                
                
                
                
                
                
                
                
                
                
                
                
                const uint64_t lastBulkEvictUs =
                    s_lastBulkEvictionUs.load(std::memory_order_relaxed);
                const bool inBulkRecovery =
                    esp::data::IsWithinBulkRecoveryStaleWindow(lastBulkEvictUs, hierarchyNowUs);
                const uint64_t recentResetUs = s_lastSceneResetUs.load(std::memory_order_relaxed);
                const bool recentStructuralReset =
                    esp::data::IsRecentStructuralReset(recentResetUs, hierarchyNowUs);
                const auto hierarchyMissingPolicy =
                    esp::data::SelectHierarchyMissingPolicy(
                        recentStructuralReset,
                        currentSceneWarmupState == esp::SceneWarmupState::Stable,
                        inBulkRecovery);
                const uint64_t missingHoldUs = hierarchyMissingPolicy.holdUs;
                const uint16_t missingThreshold = hierarchyMissingPolicy.threshold;

                
                
                
                
                
                
                
                
                auto& wouldEvict = s_playerReadScratch.hierarchyWouldEvict;
                int  wouldEvictCount = 0;
                int  pawnOnlyLossCount = 0;
                int  controllerLossCount = 0;
                int  invalidLiveSlotsCount = 0;  
                auto& refreshable = s_playerReadScratch.hierarchyRefreshable;
                auto& prevHadAnything = s_playerReadScratch.hierarchyPrevHadAnything;
                auto& controllerCompatibleArr = s_playerReadScratch.hierarchyControllerCompatible;
                auto& holdAgeAllowedArr = s_playerReadScratch.hierarchyHoldAgeAllowed;
                for (int slotIdx = 0; slotIdx < playerRefreshSlotCount; ++slotIdx) {
                    const int i = playerRefreshSlots[slotIdx];
                    refreshable[i] = true;
                    const bool liveHierarchyReady =
                        controllers[i] != 0 &&
                        isValidPawnHandle(pawnHandles[i]) &&
                        pawnEntries[i] != 0 &&
                        pawns[i] != 0;
                    if (liveHierarchyReady)
                        continue;
                    const bool previousHierarchyReady =
                        prevCachedControllers[i] != 0 &&
                        isValidPawnHandle(prevCachedPawnHandles[i]) &&
                        prevCachedPawnEntries[i] != 0 &&
                        prevCachedPawns[i] != 0;
                    if (!previousHierarchyReady)
                        continue;
                    prevHadAnything[i] = true;
                    ++invalidLiveSlotsCount;
                    if (controllers[i] == 0)
                        ++controllerLossCount;
                    else if (!isValidPawnHandle(pawnHandles[i]) || pawns[i] == 0)
                        ++pawnOnlyLossCount;
                    controllerCompatibleArr[i] =
                        controllers[i] == 0 ||
                        controllers[i] == prevCachedControllers[i];
                    holdAgeAllowedArr[i] =
                        esp::data::IsHierarchyHoldAgeAllowed(
                            s_hierarchyLastPresentUs[i],
                            hierarchyNowUs,
                            missingHoldUs);
                    const uint16_t projectedStreak =
                        esp::data::ProjectHierarchyMissingStreak(s_hierarchyMissingStreaks[i]);
                    const bool willEvict =
                        esp::data::ShouldEvictMissingHierarchy(
                            controllerCompatibleArr[i],
                            holdAgeAllowedArr[i],
                            projectedStreak,
                            missingThreshold);
                    if (willEvict) {
                        wouldEvict[i] = true;
                        ++wouldEvictCount;
                    }
                }

                
                
                
                
                
                
                
                
                const bool bulkEvictionDetected =
                    esp::data::IsHierarchyBulkEvictionDetected(
                        wouldEvictCount,
                        pawnOnlyLossCount,
                        controllerLossCount);
                if (bulkEvictionDetected) {
                    
                    
                    if (!inBulkRecovery)
                        s_bulkRecoveryStartUs = hierarchyNowUs;
                    s_lastBulkEvictionUs.store(hierarchyNowUs, std::memory_order_relaxed);
                    RecordEspEvent({
                        .type = EspEventType::BulkRecoveryEntered,
                        .slot = static_cast<uint8_t>(std::min(wouldEvictCount, 255)),
                        .param = 0
                    });
                } else if (esp::data::IsHierarchyBulkRecoveryExtensionCandidate(
                               inBulkRecovery,
                               invalidLiveSlotsCount,
                               pawnOnlyLossCount,
                               controllerLossCount)) {
                    
                    
                    if (s_bulkRecoveryStartUs == 0)
                        s_bulkRecoveryStartUs = lastBulkEvictUs;
                    const uint64_t recoveryAge =
                        esp::data::ElapsedSinceOrZero(hierarchyNowUs, s_bulkRecoveryStartUs);
                    if (esp::data::ShouldExtendHierarchyBulkRecoveryWindow(recoveryAge)) {
                        s_lastBulkEvictionUs.store(hierarchyNowUs, std::memory_order_relaxed);
                    }
                } else if (!inBulkRecovery) {
                    s_bulkRecoveryStartUs = 0;
                }

                for (int slotIdx = 0; slotIdx < playerRefreshSlotCount; ++slotIdx) {
                    const int i = playerRefreshSlots[slotIdx];
                    if (!refreshable[i])
                        continue;
                    const bool liveHierarchyReady =
                        controllers[i] != 0 &&
                        isValidPawnHandle(pawnHandles[i]) &&
                        pawnEntries[i] != 0 &&
                        pawns[i] != 0;
                    if (liveHierarchyReady) {
                        s_hierarchyMissingStreaks[i] = 0;
                        s_hierarchyLastPresentUs[i] = hierarchyNowUs;
                        continue;
                    }
                    if (!prevHadAnything[i]) {
                        s_hierarchyMissingStreaks[i] = 0;
                        s_hierarchyLastPresentUs[i] = 0;
                        continue;
                    }
                    if (s_hierarchyMissingStreaks[i] < 0xFFFFu)
                        ++s_hierarchyMissingStreaks[i];

                    const bool wantsEvict =
                        esp::data::ShouldEvictMissingHierarchy(
                            controllerCompatibleArr[i],
                            holdAgeAllowedArr[i],
                            s_hierarchyMissingStreaks[i],
                            missingThreshold);
                    const bool effectiveHold =
                        esp::data::ShouldHoldMissingHierarchy(wantsEvict, bulkEvictionDetected);

                    if (effectiveHold) {
                        ++hierarchyHeldSlotCount;
                        controllers[i] = prevCachedControllers[i];
                        pawnHandles[i] = prevCachedPawnHandles[i];
                        pawnEntries[i] = prevCachedPawnEntries[i];
                        pawns[i] = prevCachedPawns[i];
                        if (bulkEvictionDetected && wantsEvict) {
                            
                            
                            s_hierarchyMissingStreaks[i] = 0;
                        }
                    } else {
                        if (s_hierarchyLastPresentUs[i] != 0 &&
                            liveHierarchyContext &&
                            !hierarchyWarmupActive) {
                            const uint64_t holdAgeUs =
                                esp::data::ElapsedSinceOrZero(hierarchyNowUs, s_hierarchyLastPresentUs[i]);
                            RecordEspEvent({
                                .type = EspEventType::SlotEvictedHierarchy,
                                .slot = static_cast<uint8_t>(i),
                                .param = static_cast<uint16_t>(
                                    std::min<uint64_t>(holdAgeUs / 1000u, 0xFFFFu))
                            });
                        }
                        s_hierarchyLastPresentUs[i] = 0;
                    }
                }
                s_playerHierarchyHeldSlotCount.store(
                    hierarchyHeldSlotCount,
                    std::memory_order_relaxed);
            }

            int duplicateIdentityFiltered = 0;
            int backlinkMismatchCount = 0;
            bool queuedControllerBacklinks = false;
            if (ofs.C_BasePlayerPawn_m_hController > 0) {
                for (int slotIdx = 0; slotIdx < playerRefreshSlotCount; ++slotIdx) {
                    const int i = playerRefreshSlots[slotIdx];
                    if (!controllers[i] || !pawns[i])
                        continue;
                    pawnControllerHandles[i] = 0xFFFFFFFFu;
                    mem.AddScatterReadRequest(
                        handle,
                        pawns[i] + ofs.C_BasePlayerPawn_m_hController,
                        &pawnControllerHandles[i],
                        sizeof(uint32_t));
                    queuedControllerBacklinks = true;
                }
            }
            const bool controllerBacklinksRead =
                queuedControllerBacklinks && executeOptionalScatterRead();
            if (!controllerBacklinksRead)
                memset(pawnControllerHandles, 0, sizeof(pawnControllerHandles));

            auto isCompleteHierarchyIdentity = [&](int idx) {
                return idx >= 0 &&
                       idx < 64 &&
                       controllers[idx] != 0 &&
                       isValidPawnHandle(pawnHandles[idx]) &&
                       pawnEntries[idx] != 0 &&
                       pawns[idx] != 0;
            };
            auto isLocalHierarchyIdentity = [&](int idx) {
                if (idx < 0 || idx >= 64)
                    return false;
                if (localController != 0 && controllers[idx] == localController)
                    return true;
                if (localPawn != 0 && pawns[idx] == localPawn)
                    return true;
                return localControllerPawnHandle != 0u &&
                       localControllerPawnHandle != 0xFFFFFFFFu &&
                       pawnHandles[idx] == localControllerPawnHandle;
            };
            auto makeIdentityCandidate = [&](int idx) {
                return esp::data::ResolvedPlayerIdentityCandidate{
                    .slot = idx,
                    .controller = controllers[idx],
                    .pawn = pawns[idx],
                    .pawnHandle = pawnHandles[idx],
                    .pawnControllerHandle = pawnControllerHandles[idx],
                    .localIdentity = isLocalHierarchyIdentity(idx),
                    .committedIdentity =
                        s_players[idx].valid &&
                        s_players[idx].pawn == pawns[idx],
                };
            };
            auto rejectHierarchyIdentity = [&](int idx) {
                if (idx < 0 || idx >= 64)
                    return;
                controllers[idx] = 0;
                pawnHandles[idx] = 0;
                pawnEntries[idx] = 0;
                pawns[idx] = 0;
                hierarchyIdentityRejected[idx] = true;
                s_hierarchyMissingStreaks[idx] = 0;
                s_hierarchyLastPresentUs[idx] = 0;
            };

            int backlinkMatchCount = 0;
            bool localBacklinkMatched = false;
            if (controllerBacklinksRead) {
                for (int i = 0; i < 64; ++i) {
                    if (!isCompleteHierarchyIdentity(i))
                        continue;
                    const auto state = esp::data::EvaluateControllerBacklink(
                        i,
                        pawnControllerHandles[i],
                        kEntityHandleMask);
                    if (state != esp::data::ControllerBacklinkState::Match)
                        continue;
                    ++backlinkMatchCount;
                    localBacklinkMatched =
                        localBacklinkMatched || isLocalHierarchyIdentity(i);
                }
            }
            const bool controllerBacklinksTrusted =
                backlinkMatchCount >= 2 || localBacklinkMatched;

            int canonicalSlots[64] = {};
            int canonicalSlotCount = 0;
            for (int i = 0; i < 64; ++i) {
                if (!isCompleteHierarchyIdentity(i))
                    continue;

                const auto candidate = makeIdentityCandidate(i);
                int duplicateAt = -1;
                for (int canonicalIdx = 0;
                     canonicalIdx < canonicalSlotCount;
                     ++canonicalIdx) {
                    const int existingSlot = canonicalSlots[canonicalIdx];
                    if (esp::data::IsSameResolvedPawnIdentity(
                            makeIdentityCandidate(existingSlot),
                            candidate,
                            kEntityHandleMask,
                            controllerBacklinksTrusted)) {
                        duplicateAt = canonicalIdx;
                        break;
                    }
                }

                if (duplicateAt < 0) {
                    canonicalSlots[canonicalSlotCount++] = i;
                    continue;
                }

                const int existingSlot = canonicalSlots[duplicateAt];
                const bool preferCandidate =
                    esp::data::PreferSecondResolvedPlayerIdentity(
                        makeIdentityCandidate(existingSlot),
                        candidate,
                        kEntityHandleMask,
                        controllerBacklinksTrusted);
                const int rejectedSlot = preferCandidate ? existingSlot : i;
                rejectHierarchyIdentity(rejectedSlot);
                if (preferCandidate)
                    canonicalSlots[duplicateAt] = i;
                ++duplicateIdentityFiltered;
            }

            if (controllerBacklinksRead) {
                for (int canonicalIdx = 0;
                     canonicalIdx < canonicalSlotCount;
                     ++canonicalIdx) {
                    const int i = canonicalSlots[canonicalIdx];
                    if (!isCompleteHierarchyIdentity(i))
                        continue;

                    const auto backlinkState =
                        esp::data::EvaluateControllerBacklink(
                            i,
                            pawnControllerHandles[i],
                            kEntityHandleMask);
                    if (backlinkState ==
                        esp::data::ControllerBacklinkState::Mismatch) {
                        ++backlinkMismatchCount;
                    }
                }
            }
            s_playerDuplicateIdentityFilteredStat.store(
                duplicateIdentityFiltered,
                std::memory_order_relaxed);
            s_playerBacklinkMismatchStat.store(
                backlinkMismatchCount,
                std::memory_order_relaxed);

            for (int slotIdx = 0; slotIdx < playerRefreshSlotCount; ++slotIdx) {
                const int i = playerRefreshSlots[slotIdx];

                s_cachedControllers[i] = controllers[i];
                s_cachedPawns[i] = pawns[i];
                s_cachedPawnHandles[i] = pawnHandles[i];
                s_cachedPawnEntries[i] = pawnEntries[i];
            }
            s_lastHierarchyRefreshUs = hierarchyNowUs;

            {
                const int highestHierarchySlot = findHighestHierarchySlot();
                s_playerHierarchyHighWaterSlot.store(highestHierarchySlot, std::memory_order_relaxed);
                const int resolvedPlayerSlotLimit = std::max(playerSlotScanLimit, highestHierarchySlot);
                int controllerCount = 0;
                int pawnHandleCount = 0;
                int pawnCount = 0;
                for (int i = 0; i < resolvedPlayerSlotLimit; ++i) {
                    if (controllers[i])
                        ++controllerCount;
                    if (isValidPawnHandle(pawnHandles[i]))
                        ++pawnHandleCount;
                    if (pawns[i])
                        ++pawnCount;
                }

                bool localControllerReady = localController == 0;
                bool localPawnReady = localPawn == 0;
                for (int i = 0; i < resolvedPlayerSlotLimit; ++i) {
                    if (localController != 0 && controllers[i] == localController)
                        localControllerReady = true;
                    if (localPawn != 0 && pawns[i] == localPawn)
                        localPawnReady = true;
                }
                const uint64_t hierarchySceneAgeUs =
                    esp::data::ElapsedSinceOrZero(
                        hierarchyNowUs,
                        s_lastSceneResetUs.load(std::memory_order_relaxed));
                const bool hierarchyWarmupSatisfied =
                    esp::data::IsHierarchyWarmupSatisfied(
                        hierarchySceneAgeUs,
                        controllerCount,
                        pawnHandleCount,
                        pawnCount,
                        localControllerReady,
                        localPawnReady);
                if (!s_controllerCacheWarmed) {
                    if (hierarchyWarmupSatisfied) {
                        s_controllerCacheWarmed = true;
                        if (currentSceneWarmupState != esp::SceneWarmupState::Stable) {
                            setSceneWarmupState(esp::SceneWarmupState::Stable);
                            currentSceneWarmupState = esp::SceneWarmupState::Stable;
                        }
                    } else if (currentSceneWarmupState != esp::SceneWarmupState::HierarchyWarming) {
                        setSceneWarmupState(esp::SceneWarmupState::HierarchyWarming);
                        currentSceneWarmupState = esp::SceneWarmupState::HierarchyWarming;
                    }
                }
            }
        }

        const int highestHierarchySlot = findHighestHierarchySlot();
        s_playerHierarchyHighWaterSlot.store(highestHierarchySlot, std::memory_order_relaxed);
        const int resolvedPlayerSlotLimit = std::max(playerSlotScanLimit, highestHierarchySlot);

        for (int i = 0; i < resolvedPlayerSlotLimit; ++i) {
            if (pawns[i] == localPawn) {
                localMaskBit = i;
                localMaskSlotBit = i + 1;
                const int handleSlot = static_cast<int>(pawnHandles[i] & kEntitySlotMask);
                localHandleSlotBit = handleSlot;
                break;
            }
        }
        if (localMaskBit < 0) {
            for (int i = resolvedPlayerSlotLimit; i < 64; ++i) {
                if (pawns[i] == localPawn) {
                    localMaskBit = i;
                    localMaskSlotBit = i + 1;
                    const int handleSlot = static_cast<int>(pawnHandles[i] & kEntitySlotMask);
                    localHandleSlotBit = handleSlot;
                    break;
                }
            }
        }
        if (localController) {
            for (int i = 0; i < resolvedPlayerSlotLimit; ++i) {
                if (controllers[i] == localController) {
                    localControllerMaskBit = i + 1;
                    break;
                }
            }
            if (localControllerMaskBit < 0) {
                for (int i = resolvedPlayerSlotLimit; i < 64; ++i) {
                    if (controllers[i] == localController) {
                        localControllerMaskBit = i + 1;
                        break;
                    }
                }
            }
        }
        localMaskResolved =
            (localMaskBit >= 0) ||
            (localMaskSlotBit >= 0) ||
            (localHandleSlotBit >= 0) ||
            (localControllerMaskBit >= 0);

        int resolvedControllerCount = 0;
        for (int i = 0; i < 64; ++i) {
            if (controllers[i])
                ++resolvedControllerCount;
        }
        s_playerControllerSlotCountStat.store(
            resolvedControllerCount,
            std::memory_order_relaxed);
        s_entitySlotStrideStat.store(
            s_activeEntitySlotSize,
            std::memory_order_relaxed);

        for (int i = 0; i < resolvedPlayerSlotLimit; ++i) {
            if (!controllers[i] ||
                !isValidPawnHandle(pawnHandles[i]) ||
                !pawnEntries[i] ||
                !pawns[i]) {
                continue;
            }
            playerResolvedSlots[playerResolvedSlotCount++] = i;
        }
