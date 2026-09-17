    static uint64_t s_lastPlayerIdentityAuxUs = 0;

    auto resolveHandleCached = [&](uint32_t handleValue) -> uintptr_t {
        if (!isValidEntityHandle(handleValue) || !entityList)
            return 0;
        const uint32_t entityIndex = handleValue & kEntityHandleMask;
        const uint32_t block = entityIndex >> 9;
        const uint32_t slot = entityIndex & kEntitySlotMask;
        if (block >= EntityBlockCache::kBlockCount)
            return 0;
        uintptr_t entry = s_entityBlockCache.Get(block);
        if (!entry) {
            if (readPointer(entityList + 0x10 + 8ull * static_cast<uintptr_t>(block), &entry))
                s_entityBlockCache.Store(block, entry);
        }
        if (!entry)
            return 0;
        uintptr_t entity = 0;
        readPointer(entry + static_cast<uintptr_t>(s_activeEntitySlotSize) * slot, &entity);
        if (!isLikelyGamePointer(entity)) {
            uint32_t alternateSize = (s_activeEntitySlotSize == kEntitySlotSize) ? kEntitySlotSizeFallback : kEntitySlotSize;
            if (alternateSize != s_activeEntitySlotSize) {
                readPointer(entry + static_cast<uintptr_t>(alternateSize) * slot, &entity);
            }
        }
        if (!isLikelyGamePointer(entity))
            s_entityBlockCache.Invalidate(block);
        return isLikelyGamePointer(entity) ? entity : 0;
    };
    static uint64_t s_lastPlayerMoneyAuxUs = 0;
    static uint64_t s_lastPlayerDefuserAuxUs = 0;
    static uint64_t s_lastPlayerSpectatorAuxUs = 0;
    static uint64_t s_playerAuxTickResetSerial = 0;
    {
        const uint64_t auxResetSerial = s_sceneResetSerial.load(std::memory_order_relaxed);
        if (s_playerAuxTickResetSerial != auxResetSerial) {
            s_playerAuxTickResetSerial = auxResetSerial;
            s_lastPlayerIdentityAuxUs = 0;
            s_lastPlayerMoneyAuxUs = 0;
            s_lastPlayerDefuserAuxUs = 0;
            s_lastPlayerSpectatorAuxUs = 0;
        }
    }

    static uintptr_t s_cachedObserverPawns[64] = {};
    static uintptr_t s_cachedObserverServices[64] = {};
    static uint32_t s_cachedObserverTargets[64] = {};
    static uint32_t s_cachedObserverModes[64] = {};
    static uint32_t s_cachedObserverPawnHandles[64] = {};
    static uint64_t s_playerObserverCacheResetSerial = 0;
    {
        const uint64_t observerResetSerial = s_sceneResetSerial.load(std::memory_order_relaxed);
        if (s_playerObserverCacheResetSerial != observerResetSerial) {
            s_playerObserverCacheResetSerial = observerResetSerial;
            memset(s_cachedObserverPawns, 0, sizeof(s_cachedObserverPawns));
            memset(s_cachedObserverServices, 0, sizeof(s_cachedObserverServices));
            memset(s_cachedObserverTargets, 0, sizeof(s_cachedObserverTargets));
            memset(s_cachedObserverModes, 0, sizeof(s_cachedObserverModes));
            memset(s_cachedObserverPawnHandles, 0, sizeof(s_cachedObserverPawnHandles));
        }
    }

    auto& observerServices = s_playerReadScratch.observerServices;
    auto& observerTargets = s_playerReadScratch.observerTargets;
    auto& observerModes = s_playerReadScratch.observerModes;
    auto& observerPawnHandles = s_playerReadScratch.observerPawnHandles;
    auto& observerPlayerPawnHandles = s_playerReadScratch.observerPlayerPawnHandles;
    auto& observerBasePawnHandles = s_playerReadScratch.observerBasePawnHandles;
    memcpy(observerServices, s_cachedObserverServices, sizeof(observerServices));
    memcpy(observerTargets, s_cachedObserverTargets, sizeof(observerTargets));
    memcpy(observerModes, s_cachedObserverModes, sizeof(observerModes));
    auto resetObserverSlot = [&](int slot) {
        s_cachedObserverPawns[slot] = 0;
        s_cachedObserverPawnHandles[slot] = 0;
        s_cachedObserverServices[slot] = 0;
        s_cachedObserverTargets[slot] = 0;
        s_cachedObserverModes[slot] = 0;
        observerServices[slot] = 0;
        observerTargets[slot] = 0;
        observerModes[slot] = 0;
    };

    auto isLocalPlayerIndex = [&](int idx) -> bool {
        if (idx < 0 || idx >= 64) return false;
        const bool isLocalByIndex =
            localControllerMaskBit > 0 &&
            localControllerMaskBit <= 64 &&
            idx == (localControllerMaskBit - 1);
        const bool isLocalByPawn =
            (localPawn != 0 && pawns[idx] == localPawn) ||
            (s_localPawn != 0 && pawns[idx] == s_localPawn);
        const bool isLocalByController =
            localController != 0 && controllers[idx] == localController;
        return isLocalByIndex || isLocalByPawn || isLocalByController;
    };

    const uint64_t playerAuxNowUs = TickNowUs();
    const bool wantsPlayerIdentityAux =
        webRadarDemandActive ||
        wantsEspName ||
        wantsRadarSpectatorList;
    const bool wantsPlayerMoneyAux =
        webRadarDemandActive ||
        (wantsEspFlags && wantsEspFlagMoney);
    const bool wantsPlayerDefuserAux =
        webRadarDemandActive ||
        wantsTargetWeaponState ||
        wantsEspBombInfo ||
        wantsRadarShowBomb ||
        (wantsEspFlags && wantsEspFlagKit);
    const bool hasObserverOffsets =
        ofs.C_BasePlayerPawn_m_pObserverServices > 0 &&
        ofs.CPlayer_ObserverServices_m_iObserverMode > 0 &&
        ofs.CPlayer_ObserverServices_m_hObserverTarget > 0;
    const bool wantsPlayerSpectatorAux =
        wantsRadarSpectatorList &&
        hasObserverOffsets;

    const bool playerIdentityAuxDue =
        wantsPlayerIdentityAux &&
        (s_lastPlayerIdentityAuxUs == 0 ||
         (playerAuxNowUs - s_lastPlayerIdentityAuxUs) >= esp::intervals::kPlayerIdentityAuxUs);
    const bool playerMoneyAuxDue =
        wantsPlayerMoneyAux &&
        (s_lastPlayerMoneyAuxUs == 0 ||
         (playerAuxNowUs - s_lastPlayerMoneyAuxUs) >= esp::intervals::kPlayerMoneyAuxUs);
    const bool playerDefuserAuxDue =
        wantsPlayerDefuserAux &&
        (s_lastPlayerDefuserAuxUs == 0 ||
         (playerAuxNowUs - s_lastPlayerDefuserAuxUs) >= esp::intervals::kPlayerDefuserAuxUs);
    const bool playerSpectatorDue = 
        wantsPlayerSpectatorAux &&
        (s_lastPlayerSpectatorAuxUs == 0 ||
         (playerAuxNowUs - s_lastPlayerSpectatorAuxUs) >= esp::intervals::kPlayerSpectatorAuxUs);
    const bool playerSpectatorAuxDue = playerSpectatorDue;

    _playerAuxActiveTick =
        !_playerHierarchyActiveTick &&
        (playerIdentityAuxDue ||
         playerMoneyAuxDue ||
         playerDefuserAuxDue ||
         playerSpectatorAuxDue);

    auto markDynamicPawnsCached = [&]() {
        memcpy(s_cachedDynamicPawns, pawns, sizeof(s_cachedDynamicPawns));
    };

    
    
    
    
    
    
    if (_playerAuxActiveTick) {
        bool queuedPrimary = false;
        auto& moneyServiceRefreshMask = s_playerReadScratch.moneyServiceRefreshMask;
        auto& itemServiceRefreshMask = s_playerReadScratch.itemServiceRefreshMask;
        auto& observerServiceRefreshMask = s_playerReadScratch.observerServiceRefreshMask;

        for (int i = 0; i < 64; ++i) {
            if (playerIdentityAuxDue && controllers[i]) {
                if (ofs.CBasePlayerController_m_iszPlayerName > 0) {
                    mem.AddScatterReadRequest(
                        handle,
                        controllers[i] + ofs.CBasePlayerController_m_iszPlayerName,
                        &names[i],
                        sizeof(names[i]));
                    queuedPrimary = true;
                }
                if (webRadarDemandActive && ofs.CBasePlayerController_m_iPing > 0) {
                    mem.AddScatterReadRequest(
                        handle,
                        controllers[i] + ofs.CBasePlayerController_m_iPing,
                        &pings[i],
                        sizeof(uint32_t));
                    queuedPrimary = true;
                }
            }

            if (playerMoneyAuxDue && controllers[i]) {
                if (controllers[i] != s_cachedMoneyControllers[i] || !s_cachedMoneyServices[i]) {
                    if (ofs.CCSPlayerController_m_pInGameMoneyServices > 0) {
                        mem.AddScatterReadRequest(
                            handle,
                            controllers[i] + ofs.CCSPlayerController_m_pInGameMoneyServices,
                            &moneyServices[i],
                            sizeof(uintptr_t));
                        moneyServiceRefreshMask[i] = true;
                        queuedPrimary = true;
                    }
                } else if (ofs.CCSPlayerController_InGameMoneyServices_m_iAccount > 0) {
                    mem.AddScatterReadRequest(
                        handle,
                        s_cachedMoneyServices[i] + ofs.CCSPlayerController_InGameMoneyServices_m_iAccount,
                        &moneys[i],
                        sizeof(int));
                    queuedPrimary = true;
                }
            }

            if (playerDefuserAuxDue && pawns[i]) {
                if (pawns[i] != s_cachedDynamicPawns[i] || !s_cachedItemServices[i]) {
                    if (ofs.C_CSPlayerPawnBase_m_pItemServices > 0) {
                        mem.AddScatterReadRequest(
                            handle,
                            pawns[i] + ofs.C_CSPlayerPawnBase_m_pItemServices,
                            &itemServices[i],
                            sizeof(uintptr_t));
                        itemServiceRefreshMask[i] = true;
                        queuedPrimary = true;
                    }
                } else if (ofs.CCSPlayer_ItemServices_m_bHasDefuser > 0) {
                    hasDefuserFlags[i] = esp::data::kInvalidPlayerFlagSample;
                    mem.AddScatterReadRequest(
                        handle,
                        s_cachedItemServices[i] + ofs.CCSPlayer_ItemServices_m_bHasDefuser,
                        &hasDefuserFlags[i],
                        sizeof(uint8_t));
                    queuedPrimary = true;
                }
            }

            if (playerSpectatorAuxDue && controllers[i]) {
                const bool isSpecOrDead = (pawns[i] == 0) || (lifeStates[i] != 0) || (teams[i] == 1);
                if (!isSpecOrDead) {
                    resetObserverSlot(i);
                } else {
                    
                    if (ofs.CCSPlayerController_m_hObserverPawn > 0) {
                        mem.AddScatterReadRequest(
                            handle,
                            controllers[i] + ofs.CCSPlayerController_m_hObserverPawn,
                            &observerPawnHandles[i],
                            sizeof(uint32_t));
                    }
                    if (ofs.CCSPlayerController_m_hPlayerPawn > 0) {
                        mem.AddScatterReadRequest(
                            handle,
                            controllers[i] + ofs.CCSPlayerController_m_hPlayerPawn,
                            &observerPlayerPawnHandles[i],
                            sizeof(uint32_t));
                    }
                    if (ofs.CBasePlayerController_m_hPawn > 0) {
                        mem.AddScatterReadRequest(
                            handle,
                            controllers[i] + ofs.CBasePlayerController_m_hPawn,
                            &observerBasePawnHandles[i],
                            sizeof(uint32_t));
                    }

                    
                    if (s_cachedObserverPawns[i]) {
                        if (s_cachedObserverServices[i]) {
                            mem.AddScatterReadRequest(
                                handle,
                                s_cachedObserverServices[i] + ofs.CPlayer_ObserverServices_m_iObserverMode,
                                &observerModes[i],
                                sizeof(uint32_t));
                            mem.AddScatterReadRequest(
                                handle,
                                s_cachedObserverServices[i] + ofs.CPlayer_ObserverServices_m_hObserverTarget,
                                &observerTargets[i],
                                sizeof(uint32_t));
                        } else {
                            mem.AddScatterReadRequest(
                                handle,
                                s_cachedObserverPawns[i] + ofs.C_BasePlayerPawn_m_pObserverServices,
                                &observerServices[i],
                                sizeof(uintptr_t));
                            observerServiceRefreshMask[i] = true;
                        }
                    }
                    queuedPrimary = true;
                }
            }
        }

        
        bool primaryScatterOk = true;
        if (queuedPrimary)
            primaryScatterOk = mem.ExecuteReadScatter(handle);

        if (!primaryScatterOk) {
            if (playerIdentityAuxDue) {
                memcpy(names, s_cachedPlayerNames, sizeof(names));
                memcpy(pings, s_cachedPlayerPings, sizeof(pings));
            }
            if (playerMoneyAuxDue) {
                memcpy(moneyServices, s_cachedMoneyServices, sizeof(moneyServices));
                memcpy(moneys, s_cachedPlayerMoneys, sizeof(moneys));
            }
            if (playerDefuserAuxDue) {
                memcpy(itemServices, s_cachedItemServices, sizeof(itemServices));
                memcpy(hasDefuserFlags, s_cachedHasDefuserFlags, sizeof(hasDefuserFlags));
            }
            if (playerSpectatorAuxDue) {
                memcpy(observerServices, s_cachedObserverServices, sizeof(observerServices));
                memcpy(observerTargets, s_cachedObserverTargets, sizeof(observerTargets));
                memcpy(observerModes, s_cachedObserverModes, sizeof(observerModes));
            }
            logUpdateDataIssue("scatter_aux_primary", "player_aux_primary_scatter_failed");
        } else {
            if (playerIdentityAuxDue) {
                for (int i = 0; i < 64; ++i)
                    names[i][sizeof(names[i]) - 1u] = '\0';
            }

            
            bool queuedChainedRefresh = false;

            if (playerSpectatorAuxDue) {
                for (int i = 0; i < 64; ++i) {
                    const bool isSpecOrDead = (pawns[i] == 0) || (lifeStates[i] != 0) || (teams[i] == 1);
                    if (!controllers[i] || !isSpecOrDead)
                        continue;

                    uint32_t bestHandle = 0;
                    if (isValidEntityHandle(observerBasePawnHandles[i])) {
                        bestHandle = observerBasePawnHandles[i];
                    } else if (isValidEntityHandle(observerPlayerPawnHandles[i])) {
                        bestHandle = observerPlayerPawnHandles[i];
                    } else if (isValidEntityHandle(observerPawnHandles[i])) {
                        bestHandle = observerPawnHandles[i];
                    }

                    
                    if (bestHandle != s_cachedObserverPawnHandles[i]) {
                        resetObserverSlot(i);
                    }

                    if (bestHandle) {
                        uintptr_t resolvedPawn = s_cachedObserverPawns[i];
                        if (!resolvedPawn) {
                            resolvedPawn = resolveHandleCached(bestHandle);
                            if (resolvedPawn) {
                                const bool isLocal = isLocalPlayerIndex(i);
                                if (isLocal || (resolvedPawn != localPawn && (s_localPawn == 0 || resolvedPawn != s_localPawn))) {
                                    s_cachedObserverPawns[i] = resolvedPawn;
                                    s_cachedObserverPawnHandles[i] = bestHandle;

                                    
                                    mem.AddScatterReadRequest(
                                        handle,
                                        resolvedPawn + ofs.C_BasePlayerPawn_m_pObserverServices,
                                        &observerServices[i],
                                        sizeof(uintptr_t));
                                    queuedChainedRefresh = true;
                                }
                            }
                        }
                    }
                }
            }

            if (playerMoneyAuxDue) {
                for (int i = 0; i < 64; ++i) {
                    if (!moneyServiceRefreshMask[i])
                        continue;
                    if (!isLikelyGamePointer(moneyServices[i])) {
                        moneyServices[i] = 0;
                        continue;
                    }
                    if (ofs.CCSPlayerController_InGameMoneyServices_m_iAccount > 0) {
                        mem.AddScatterReadRequest(
                            handle,
                            moneyServices[i] + ofs.CCSPlayerController_InGameMoneyServices_m_iAccount,
                            &moneys[i],
                            sizeof(int));
                        queuedChainedRefresh = true;
                    }
                }
            }

            if (playerDefuserAuxDue) {
                for (int i = 0; i < 64; ++i) {
                    if (!itemServiceRefreshMask[i])
                        continue;
                    if (!isLikelyGamePointer(itemServices[i])) {
                        itemServices[i] = 0;
                        continue;
                    }
                    if (ofs.CCSPlayer_ItemServices_m_bHasDefuser > 0) {
                        hasDefuserFlags[i] = esp::data::kInvalidPlayerFlagSample;
                        mem.AddScatterReadRequest(
                            handle,
                            itemServices[i] + ofs.CCSPlayer_ItemServices_m_bHasDefuser,
                            &hasDefuserFlags[i],
                            sizeof(uint8_t));
                        queuedChainedRefresh = true;
                    }
                }
            }

            if (playerSpectatorAuxDue) {
                for (int i = 0; i < 64; ++i) {
                    if (!observerServiceRefreshMask[i])
                        continue;
                    if (!isLikelyGamePointer(observerServices[i])) {
                        observerServices[i] = 0;
                        observerTargets[i] = 0;
                        observerModes[i] = 0;
                        continue;
                    }
                    mem.AddScatterReadRequest(
                        handle,
                        observerServices[i] + ofs.CPlayer_ObserverServices_m_iObserverMode,
                        &observerModes[i],
                        sizeof(uint32_t));
                    mem.AddScatterReadRequest(
                        handle,
                        observerServices[i] + ofs.CPlayer_ObserverServices_m_hObserverTarget,
                        &observerTargets[i],
                        sizeof(uint32_t));
                    queuedChainedRefresh = true;
                }
            }

            
            if (queuedChainedRefresh) {
                if (!mem.ExecuteReadScatter(handle)) {
                    
                    for (int i = 0; i < 64; ++i) {
                        if (moneyServiceRefreshMask[i] && moneyServices[i])
                            moneys[i] = s_cachedPlayerMoneys[i];
                        if (itemServiceRefreshMask[i] && itemServices[i])
                            hasDefuserFlags[i] = s_cachedHasDefuserFlags[i];
                        if (observerServiceRefreshMask[i] && observerServices[i]) {
                            observerTargets[i] = s_cachedObserverTargets[i];
                            observerModes[i] = s_cachedObserverModes[i];
                        }
                    }
                    logUpdateDataIssue("scatter_aux_chain", "player_aux_chained_refresh_failed");
                }
            }

            
            if (playerIdentityAuxDue) {
                memcpy(s_cachedPlayerNames, names, sizeof(s_cachedPlayerNames));
                memcpy(s_cachedPlayerPings, pings, sizeof(s_cachedPlayerPings));
                memcpy(s_cachedIdentityControllers, controllers, sizeof(s_cachedIdentityControllers));
            }
            if (playerMoneyAuxDue) {
                memcpy(s_cachedMoneyControllers, controllers, sizeof(s_cachedMoneyControllers));
                memcpy(s_cachedMoneyServices, moneyServices, sizeof(s_cachedMoneyServices));
                memcpy(s_cachedPlayerMoneys, moneys, sizeof(s_cachedPlayerMoneys));
            }
            if (playerDefuserAuxDue) {
                for (int i = 0; i < 64; ++i) {
                    if (!esp::data::IsValidPlayerFlagSample(hasDefuserFlags[i]))
                        hasDefuserFlags[i] = 0;
                }
                memcpy(s_cachedItemServices, itemServices, sizeof(s_cachedItemServices));
                memcpy(s_cachedHasDefuserFlags, hasDefuserFlags, sizeof(s_cachedHasDefuserFlags));
            }
            if (playerSpectatorAuxDue) {
                memcpy(s_cachedObserverServices, observerServices, sizeof(s_cachedObserverServices));
                memcpy(s_cachedObserverTargets, observerTargets, sizeof(s_cachedObserverTargets));
                memcpy(s_cachedObserverModes, observerModes, sizeof(s_cachedObserverModes));
            }
            
            if (playerDefuserAuxDue)
                markDynamicPawnsCached();
        }

        
        if (playerIdentityAuxDue)   s_lastPlayerIdentityAuxUs   = playerAuxNowUs;
        if (playerMoneyAuxDue)      s_lastPlayerMoneyAuxUs      = playerAuxNowUs;
        if (playerDefuserAuxDue)    s_lastPlayerDefuserAuxUs    = playerAuxNowUs;
        if (playerSpectatorAuxDue)  s_lastPlayerSpectatorAuxUs  = playerAuxNowUs;
    }

    if (!wantsPlayerSpectatorAux) {
        if (s_spectatorCount != 0) {
            s_spectatorCount = 0;
            memset(s_spectators, 0, sizeof(s_spectators));
        }
    } else if (playerSpectatorAuxDue) {
        auto& resolvedSpectators = s_playerReadScratch.resolvedSpectators;
        int resolvedSpectatorCount = 0;



        int localIdx = -1;
        for (int idx = 0; idx < 64; ++idx) {
            if (isLocalPlayerIndex(idx)) {
                localIdx = idx;
                break;
            }
        }

        const bool localIsDeadResolved = s_localIsDead || (s_localTeam == 1);

        auto getSlotIndexFromHandle = [&](uint32_t handleVal) -> int {
            if (handleVal == 0u || handleVal == 0xFFFFFFFFu)
                return -1;
            const uint32_t index = handleVal & kEntityHandleMask;
            if (index >= 1 && index <= 64) {
                return static_cast<int>(index - 1);
            }
            for (int j = 0; j < 64; ++j) {
                if (pawns[j] != 0 && (pawnHandles[j] & kEntityHandleMask) == index) {
                    return j;
                }
            }
            return -1;
        };

        int localTargetSlot = -1;
        if (localIdx != -1) {
            if (!localIsDeadResolved) {
                localTargetSlot = localIdx;
            } else {
                localTargetSlot = getSlotIndexFromHandle(observerTargets[localIdx]);
            }
        } else {
            localTargetSlot = getSlotIndexFromHandle(localControllerPawnHandle);
        }

        for (int i = 0; i < 64; ++i) {
            if (!controllers[i])
                continue;
            if (isLocalPlayerIndex(i))
                continue;

            const bool isSpecOrDead = (pawns[i] == 0) || (lifeStates[i] != 0) || (teams[i] == 1);
            if (!isSpecOrDead)
                continue;

            const uint32_t liveObserverMode = observerModes[i];
            const uint32_t liveObserverTarget = observerTargets[i];

            if (liveObserverMode <= 2)
                continue;

            // Check if they are spectating the local player directly or sharing spectator target
            bool isSpectatingLocal = false;
            
            // 1. Check direct pawn handles
            if (localControllerPawnHandle != 0u && localControllerPawnHandle != 0xFFFFFFFFu &&
                (liveObserverTarget & kEntityHandleMask) == (localControllerPawnHandle & kEntityHandleMask)) {
                isSpectatingLocal = true;
            }
            if (!isSpectatingLocal && localIdx != -1 && pawnHandles[localIdx] != 0 &&
                (liveObserverTarget & kEntityHandleMask) == (pawnHandles[localIdx] & kEntityHandleMask)) {
                isSpectatingLocal = true;
            }
            
            // 2. Check resolved pawn address comparison
            if (!isSpectatingLocal && entityList) {
                uintptr_t resolvedObsTarget = resolveHandleCached(liveObserverTarget);
                if (resolvedObsTarget != 0 && (resolvedObsTarget == localPawn || (s_localPawn != 0 && resolvedObsTarget == s_localPawn))) {
                    isSpectatingLocal = true;
                }
            }
            
            // 3. Or check slot index matching (works when local player is alive or dead/spectating others)
            int spectatorTargetSlot = getSlotIndexFromHandle(liveObserverTarget);
            bool slotMatch = (spectatorTargetSlot != -1 && spectatorTargetSlot == localTargetSlot);
            
            if (!isSpectatingLocal && !slotMatch)
                continue;

            SpectatorEntry& out = resolvedSpectators[resolvedSpectatorCount++];
            out.valid = true;

            const char* name = names[i][0] ? names[i] : (s_cachedPlayerNames[i][0] ? s_cachedPlayerNames[i] : nullptr);
            if (name && name[0]) {
                strncpy_s(out.name, sizeof(out.name), name, _TRUNCATE);
            } else {
                std::snprintf(out.name, sizeof(out.name), "Player %d", i + 1);
            }

            const char* targetName = nullptr;
            bool targetIsLocal = false;

            const uint32_t targetIndex = liveObserverTarget & kEntityHandleMask;

            if (localControllerPawnHandle != 0u &&
                localControllerPawnHandle != 0xFFFFFFFFu &&
                targetIndex == (localControllerPawnHandle & kEntityHandleMask)) {
                targetIsLocal = true;
                targetName = s_localName[0] ? s_localName : "Local Player";
            }

            if (!targetName && localIdx != -1 && (pawnHandles[localIdx] & kEntityHandleMask) == targetIndex) {
                targetIsLocal = true;
                targetName = s_localName[0] ? s_localName : "Local Player";
            }

            if (!targetName) {
                for (int j = 0; j < 64; ++j) {
                    if (pawns[j] != 0 && (pawnHandles[j] & kEntityHandleMask) == targetIndex) {
                        targetName = names[j][0] ? names[j] : (s_cachedPlayerNames[j][0] ? s_cachedPlayerNames[j] : nullptr);
                        break;
                    }
                }
            }

            out.targetIsLocal = targetIsLocal;
            if (targetName && targetName[0]) {
                strncpy_s(out.targetName, sizeof(out.targetName), targetName, _TRUNCATE);
            } else {
                out.targetName[0] = '\0';
            }

            if (resolvedSpectatorCount >= 64)
                break;
        }

        s_spectatorCount = resolvedSpectatorCount;
        memcpy(s_spectators, resolvedSpectators, sizeof(s_spectators));
    }
