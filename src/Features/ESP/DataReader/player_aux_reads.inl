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

    const bool anyPlayerAuxDue = playerIdentityAuxDue || playerMoneyAuxDue ||
        playerDefuserAuxDue || playerSpectatorAuxDue;
    deferredFairness.Observe(DeferredLane::Auxiliary, anyPlayerAuxDue, playerAuxNowUs);
    _playerAuxActiveTick = anyPlayerAuxDue && !_playerHierarchyActiveTick &&
        (forcedDeferredLane == DeferredLane::Auxiliary ||
            (forcedDeferredLane == DeferredLane::None && !prioritizeBoneLane));
    if (_playerAuxActiveTick) deferredFairness.Served(DeferredLane::Auxiliary);

    auto markDynamicPawnsCached = [&]() {
        memcpy(s_cachedDynamicPawns, pawns, sizeof(s_cachedDynamicPawns));
    };

    
    
    
    
    
    
    if (_playerAuxActiveTick) {
        bool queuedPrimary = false;
        auto& moneyServiceRefreshMask = s_playerReadScratch.moneyServiceRefreshMask;
        auto& itemServiceRefreshMask = s_playerReadScratch.itemServiceRefreshMask;

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
                        sizeof(int), &moneyBytesRead[i]);
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
            logUpdateDataIssue("scatter_aux_primary", "player_aux_primary_scatter_failed");
        } else {
            if (playerIdentityAuxDue) {
                for (int i = 0; i < 64; ++i)
                    names[i][sizeof(names[i]) - 1u] = '\0';
            }

            
            bool queuedChainedRefresh = false;


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
                            sizeof(int), &moneyBytesRead[i]);
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


            
            if (queuedChainedRefresh) {
                if (!mem.ExecuteReadScatter(handle)) {
                    
                    for (int i = 0; i < 64; ++i) {
                        if (moneyServiceRefreshMask[i] && moneyServices[i]) {
                            moneys[i] = s_cachedPlayerMoneys[i];
                            moneyBytesRead[i] = 0;
                        }
                        if (itemServiceRefreshMask[i] && itemServices[i])
                            hasDefuserFlags[i] = s_cachedHasDefuserFlags[i];
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
                for (int i=0;i<64;++i) {
                    if (moneyBytesRead[i]==sizeof(int) && moneys[i]>=0 && moneys[i]<=1000000)
                        s_moneySampleUs[i]=playerAuxNowUs;
                    else
                        moneys[i]=s_cachedPlayerMoneys[i];
                }
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
            
            if (playerDefuserAuxDue)
                markDynamicPawnsCached();
        }

        
        if (playerIdentityAuxDue)   s_lastPlayerIdentityAuxUs   = playerAuxNowUs;
        if (playerMoneyAuxDue)      s_lastPlayerMoneyAuxUs      = playerAuxNowUs;
        if (playerDefuserAuxDue)    s_lastPlayerDefuserAuxUs    = playerAuxNowUs;
    }

    static esp::data::ObserverState observerStates[64] = {};
    static uint64_t observerResetSerial = 0;
    const auto currentObserverSerial = s_sceneResetSerial.load(std::memory_order_relaxed);
    if (observerResetSerial != currentObserverSerial || !wantsPlayerSpectatorAux) {
        observerResetSerial = currentObserverSerial;
        for (auto& state : observerStates)
            state = {};
    }
    for (int i = 0; i < 64; ++i) {
        const bool alive = coreReadPlausible[i] && healths[i] > 0 && lifeStates[i] == 0 && teams[i] != 1;
        if (alive || !esp::data::IsObserverStateFresh(observerStates[i], controllers[i], playerAuxNowUs))
            observerStates[i] = {};
    }
    if (_playerAuxActiveTick && playerSpectatorAuxDue) {
        esp::data::ObserverSample samples[64] = {};
        for (int i = 0; i < 64; ++i) {
            const bool alive = coreReadPlausible[i] && healths[i] > 0 && lifeStates[i] == 0 && teams[i] != 1;
            if (!alive)
                samples[i].controller = controllers[i];
        }
        const esp::data::ObserverOffsets observerOffsets = {
            ofs.CCSPlayerController_m_hObserverPawn, ofs.CBasePlayerController_m_hPawn,
            ofs.CCSPlayerController_m_hPlayerPawn, ofs.C_BasePlayerPawn_m_pObserverServices,
            ofs.CPlayer_ObserverServices_m_iObserverMode, ofs.CPlayer_ObserverServices_m_hObserverTarget
        };
        esp::data::ReadObserverBatch(samples, observerOffsets,
            [&](uintptr_t address, void* destination, size_t bytes, esp::data::ObserverReadBytes* completed) {
                mem.AddScatterReadRequest(handle, address, destination, bytes, completed);
            }, [&]() { return mem.ExecuteReadScatter(handle); },
            resolveHandleCached, isLikelyGamePointer);
        for (int i = 0; i < 64; ++i)
            esp::data::CommitObserverSample(observerStates[i], samples[i], playerAuxNowUs);
        s_lastPlayerSpectatorAuxUs = playerAuxNowUs;
    }

    auto& resolvedSpectators = s_playerReadScratch.resolvedSpectators;
    int resolvedSpectatorCount = 0;
    if (wantsPlayerSpectatorAux) {
        int localIdx = -1;
        for (int i = 0; i < 64; ++i) {
            if (isLocalPlayerIndex(i)) {
                localIdx = i;
                break;
            }
        }
        const bool localDead = s_localIsDead || s_localTeam == 1;
        uint32_t watchedHandle = localControllerPawnHandle;
        if (localIdx >= 0) {
            watchedHandle = localDead
                ? (esp::data::IsFollowingObserver(observerStates[localIdx].mode)
                    ? observerStates[localIdx].target : 0u)
                : pawnHandles[localIdx];
        } else if (localDead) {
            watchedHandle = 0u;
        }
        if (esp::data::IsObserverHandle(watchedHandle)) {
            for (int i = 0; i < 64; ++i) {
                const auto& state = observerStates[i];
                if (isLocalPlayerIndex(i) ||
                    !esp::data::IsObserverStateFresh(state, controllers[i], playerAuxNowUs) ||
                    !esp::data::IsFollowingObserver(state.mode) || state.target != watchedHandle)
                    continue;
                auto& out = resolvedSpectators[resolvedSpectatorCount++];
                out = {};
                out.valid = true;
                const char* name = names[i][0] ? names[i] : s_cachedPlayerNames[i];
                if (name[0])
                    strncpy_s(out.name, sizeof(out.name), name, _TRUNCATE);
                else
                    std::snprintf(out.name, sizeof(out.name), "Player %d", i + 1);
                out.targetIsLocal = !localDead;
                const char* targetName = !localDead ? s_localName : nullptr;
                if (localDead) {
                    for (int j = 0; j < 64; ++j) {
                        if (pawns[j] && pawnHandles[j] == watchedHandle) {
                            targetName = names[j][0] ? names[j] : s_cachedPlayerNames[j];
                            break;
                        }
                    }
                }
                if (targetName && targetName[0])
                    strncpy_s(out.targetName, sizeof(out.targetName), targetName, _TRUNCATE);
            }
        }
    }
    s_spectatorCount = resolvedSpectatorCount;
    memcpy(s_spectators, resolvedSpectators, sizeof(s_spectators));
