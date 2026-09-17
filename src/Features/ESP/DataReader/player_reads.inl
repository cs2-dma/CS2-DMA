    std::memset(&s_playerReadScratch, 0, sizeof(s_playerReadScratch));
    std::memset(&s_boneReadScratch, 0, sizeof(s_boneReadScratch));
    s_entityBlockCache.Synchronize(
        entityList,
        s_sceneResetSerial.load(std::memory_order_relaxed));
    auto& controllers = s_playerReadScratch.controllers;
    auto& pawnHandles = s_playerReadScratch.pawnHandles;
    auto& names = s_playerReadScratch.names;
    auto& pings = s_playerReadScratch.pings;
    auto& moneyServices = s_playerReadScratch.moneyServices;
    auto& moneys = s_playerReadScratch.moneys;
    auto& pawnEntries = s_playerReadScratch.pawnEntries;
    auto& pawns = s_playerReadScratch.pawns;
    int localMaskBit = -1;
    int localMaskSlotBit = -1;
    int localHandleSlotBit = -1;
    int localControllerMaskBit = -1;
    bool localMaskResolved = false;
    const uint64_t _playerHierarchyStartUs = TickNowUs();
    uint64_t _playerHierarchyUs = 0;
    uint64_t _playerCoreUs = 0;
    uint64_t _playerRepairUs = 0;
    auto& pawnControllerHandles = s_playerReadScratch.pawnControllerHandles;
    auto& hierarchyIdentityRejected = s_playerReadScratch.hierarchyIdentityRejected;
    auto& playerResolvedSlots = s_playerReadScratch.playerResolvedSlots;
    int playerResolvedSlotCount = 0;

    if (canReadEntityHierarchy) {
        #include "player_parts/player_hierarchy.inl"
    } else {
        s_playerControllerSlotCountStat.store(0, std::memory_order_relaxed);
        s_playerHierarchyHeldSlotCount.store(0, std::memory_order_relaxed);
    }
    _playerHierarchyUs = TickNowUs() - _playerHierarchyStartUs;
    const uint64_t _playerCoreStartUs = TickNowUs();

    auto& healths = s_playerReadScratch.healths;
    auto& armors = s_playerReadScratch.armors;
    auto& teams = s_playerReadScratch.teams;
    auto& liveTeamReads = s_playerReadScratch.liveTeamReads;
    auto& coreArmorReadsQueued = s_playerReadScratch.coreArmorReadsQueued;
    auto& coreHealthBytesRead = s_playerReadScratch.coreHealthBytesRead;
    auto& coreArmorBytesRead = s_playerReadScratch.coreArmorBytesRead;
    auto& coreTeamBytesRead = s_playerReadScratch.coreTeamBytesRead;
    auto& coreLifeStateBytesRead = s_playerReadScratch.coreLifeStateBytesRead;
    auto& corePositionBytesRead = s_playerReadScratch.corePositionBytesRead;
    auto& coreVelocityBytesRead = s_playerReadScratch.coreVelocityBytesRead;
    auto& lifeStates = s_playerReadScratch.lifeStates;
    auto& positions = s_playerReadScratch.positions;
    auto& coreReadFresh = s_playerReadScratch.coreReadFresh;
    auto& coreReadPlausible = s_playerReadScratch.coreReadPlausible;
    auto& coreReadAlive = s_playerReadScratch.coreReadAlive;
    auto& sceneNodes = s_playerReadScratch.sceneNodes;
    auto& weaponServices = s_playerReadScratch.weaponServices;
    auto& activeWeaponHandles = s_playerReadScratch.activeWeaponHandles;
    auto& activeWeaponEntries = s_playerReadScratch.activeWeaponEntries;
    auto& activeWeapons = s_playerReadScratch.activeWeapons;
    auto& inventoryWeaponCounts = s_playerReadScratch.inventoryWeaponCounts;
    auto& inventoryWeaponHandleArrays = s_playerReadScratch.inventoryWeaponHandleArrays;
    auto& inventoryWeaponHandles = s_playerReadScratch.inventoryWeaponHandles;
    auto& inventoryWeaponEntries = s_playerReadScratch.inventoryWeaponEntries;
    auto& inventoryWeapons = s_playerReadScratch.inventoryWeapons;
    auto& inventoryWeaponIds = s_playerReadScratch.inventoryWeaponIds;
    auto& inventoryHasBombBySlot = s_playerReadScratch.inventoryHasBombBySlot;
    auto& weaponIds = s_playerReadScratch.weaponIds;
    auto& ammoClips = s_playerReadScratch.ammoClips;
    auto& bombCarrierBySlot = s_playerReadScratch.bombCarrierBySlot;
    auto getInventorySlotCount = [&](int idx) -> int {
        if (idx < 0 || idx >= 64)
            return 0;
        return std::clamp(inventoryWeaponCounts[idx], 0, kMaxInventoryWeapons);
    };
    
    int weaponC4OwnerPlayerIndex = -1;
    auto isValidEntityHandle = [&](uint32_t handleValue) -> bool {
        const uint32_t slot = handleValue & kEntityHandleMask;
        return handleValue != 0u &&
               handleValue != 0xFFFFFFFFu &&
               slot != 0u &&
               slot != kEntityHandleMask;
    };
    auto resolveEntityFromHandle = [&](uint32_t handleValue) -> uintptr_t {
        if (!isValidEntityHandle(handleValue) || !entityList)
            return 0;

        const uint32_t entityIndex = handleValue & kEntityHandleMask;
        const uint32_t block = entityIndex >> 9;
        const uint32_t slot = entityIndex & kEntitySlotMask;
        uintptr_t entry = 0;
        if (!readPointer(entityList + 0x10 + 8ull * static_cast<uintptr_t>(block), &entry))
            return 0;

        uintptr_t entity = 0;
        readPointer(entry + s_activeEntitySlotSize * static_cast<uintptr_t>(slot), &entity);
        if (!isLikelyGamePointer(entity)) {
            uint32_t alternateSize = (s_activeEntitySlotSize == kEntitySlotSize) ? kEntitySlotSizeFallback : kEntitySlotSize;
            if (alternateSize != s_activeEntitySlotSize) {
                uintptr_t fallbackEntity = 0;
                readPointer(entry + alternateSize * static_cast<uintptr_t>(slot), &fallbackEntity);
                if (isLikelyGamePointer(fallbackEntity))
                    entity = fallbackEntity;
            }
        }

        return entity;
    };
    auto findPlayerIndexByEntityHandle = [&](uint32_t handleValue) -> int {
        if (!isValidEntityHandle(handleValue))
            return -1;

        for (int i = 0; i < 64; ++i) {
            if (pawns[i] &&
                esp::data::DoesEntityHandleReferenceIndex(
                    pawnHandles[i],
                    handleValue,
                    kEntityHandleMask)) {
                return i;
            }
        }

        const uintptr_t resolvedEntity = resolveEntityFromHandle(handleValue);
        for (int i = 0; i < 64; ++i) {
            if (resolvedEntity && (pawns[i] == resolvedEntity || controllers[i] == resolvedEntity))
                return i;
        }

        if (resolvedEntity && ofs.C_BasePlayerPawn_m_hController > 0) {
            uint32_t controllerHandle = 0;
            if (readValue(resolvedEntity + ofs.C_BasePlayerPawn_m_hController, &controllerHandle, sizeof(controllerHandle)) &&
                isValidEntityHandle(controllerHandle)) {
                const uintptr_t controller = resolveEntityFromHandle(controllerHandle);
                if (controller) {
                    for (int i = 0; i < 64; ++i) {
                        if (controllers[i] == controller)
                            return i;
                    }
                }
            }
        }

        if (resolvedEntity && ofs.CCSPlayerController_m_hPlayerPawn > 0) {
            uint32_t pawnHandle = 0;
            if (readValue(resolvedEntity + ofs.CCSPlayerController_m_hPlayerPawn, &pawnHandle, sizeof(pawnHandle)) &&
                isValidEntityHandle(pawnHandle)) {
                const uintptr_t pawn = resolveEntityFromHandle(pawnHandle);
                if (pawn) {
                    for (int i = 0; i < 64; ++i) {
                        if (pawns[i] == pawn || pawnHandles[i] == pawnHandle)
                            return i;
                    }
                }
            }
        }

        return -1;
    };
    int localCrosshairTargetSlot = -1;
    if (localCrosshairReadValid &&
        localCrosshairEntityIndex > 0 &&
        localCrosshairEntityIndex < 4096) {
        localCrosshairTargetSlot =
            findPlayerIndexByEntityHandle(static_cast<uint32_t>(localCrosshairEntityIndex));
    }
    auto& itemServices = s_playerReadScratch.itemServices;
    auto& hasDefuserFlags = s_playerReadScratch.hasDefuserFlags;
    auto& gunGameImmunityFlags = s_playerReadScratch.gunGameImmunityFlags;
    auto& gunGameImmunityBytesRead = s_playerReadScratch.gunGameImmunityBytesRead;
    auto& gunGameImmunityReadFresh = s_playerReadScratch.gunGameImmunityReadFresh;
    auto& scopedFlags = s_playerReadScratch.scopedFlags;
    auto& defusingFlags = s_playerReadScratch.defusingFlags;
    auto& flashBangTimes = s_playerReadScratch.flashBangTimes;
    auto& flashDurations = s_playerReadScratch.flashDurations;
    auto& scopedFlagBytesRead = s_playerReadScratch.scopedFlagBytesRead;
    auto& defusingFlagBytesRead = s_playerReadScratch.defusingFlagBytesRead;
    auto& flashBangTimeBytesRead = s_playerReadScratch.flashBangTimeBytesRead;
    auto& flashDurationBytesRead = s_playerReadScratch.flashDurationBytesRead;
    auto& scopedReadFresh = s_playerReadScratch.scopedReadFresh;
    auto& defusingReadFresh = s_playerReadScratch.defusingReadFresh;
    auto& flashReadFresh = s_playerReadScratch.flashReadFresh;
    auto& eyeAnglesPerPlayer = s_playerReadScratch.eyeAnglesPerPlayer;
    auto& velocities = s_playerReadScratch.velocities;
    auto& velocityReadFresh = s_playerReadScratch.velocityReadFresh;
    auto& spottedMasks = s_playerReadScratch.spottedMasks;
    auto& spottedMaskBytesRead = s_playerReadScratch.spottedMaskBytesRead;
    auto& spottedReadFresh = s_playerReadScratch.spottedReadFresh;
    auto& allBones = s_boneReadScratch;
    auto& hasBoneData = s_playerReadScratch.hasBoneData;
    auto& boneSampleTimeUs = s_playerReadScratch.boneSampleTimeUs;
    auto& allHitboxes = s_playerReadScratch.hitboxes;
    auto& hitboxCounts = s_playerReadScratch.hitboxCounts;
    auto& hasHitboxData = s_playerReadScratch.hasHitboxData;
    auto& hitboxSampleTimeUs = s_playerReadScratch.hitboxSampleTimeUs;
    static char s_cachedPlayerNames[64][128] = {};
    static uint32_t s_cachedPlayerPings[64] = {};
    static uintptr_t s_cachedIdentityControllers[64] = {};
    static uintptr_t s_cachedMoneyControllers[64] = {};
    static uintptr_t s_cachedMoneyServices[64] = {};
    static int s_cachedPlayerMoneys[64] = {};
    static uintptr_t s_cachedDynamicPawns[64] = {};
    static uintptr_t s_cachedItemServices[64] = {};
    static uint8_t s_cachedHasDefuserFlags[64] = {};
    static uint8_t s_cachedScopedFlags[64] = {};
    static uint8_t s_cachedDefusingFlags[64] = {};
    static float s_cachedFlashDurations[64] = {};
    static Vector3 s_cachedEyeAnglesPerPlayer[64] = {};
    static uint32_t s_cachedSpottedMasks[64][2] = {};
    static uint64_t s_playerAuxCacheResetSerial = 0;
    {
        const uint64_t auxResetSerial = s_sceneResetSerial.load(std::memory_order_relaxed);
        if (s_playerAuxCacheResetSerial != auxResetSerial) {
            s_playerAuxCacheResetSerial = auxResetSerial;
            memset(s_cachedPlayerNames, 0, sizeof(s_cachedPlayerNames));
            memset(s_cachedPlayerPings, 0, sizeof(s_cachedPlayerPings));
            memset(s_cachedIdentityControllers, 0, sizeof(s_cachedIdentityControllers));
            memset(s_cachedMoneyControllers, 0, sizeof(s_cachedMoneyControllers));
            memset(s_cachedMoneyServices, 0, sizeof(s_cachedMoneyServices));
            memset(s_cachedPlayerMoneys, 0, sizeof(s_cachedPlayerMoneys));
            memset(s_cachedDynamicPawns, 0, sizeof(s_cachedDynamicPawns));
            memset(s_cachedItemServices, 0, sizeof(s_cachedItemServices));
            memset(s_cachedHasDefuserFlags, 0, sizeof(s_cachedHasDefuserFlags));
            memset(s_cachedScopedFlags, 0, sizeof(s_cachedScopedFlags));
            memset(s_cachedDefusingFlags, 0, sizeof(s_cachedDefusingFlags));
            memset(s_cachedFlashDurations, 0, sizeof(s_cachedFlashDurations));
            memset(s_cachedEyeAnglesPerPlayer, 0, sizeof(s_cachedEyeAnglesPerPlayer));
            memset(s_cachedSpottedMasks, 0, sizeof(s_cachedSpottedMasks));
        }
    }
    
    
    
    memcpy(names, s_cachedPlayerNames, sizeof(names));
    memcpy(pings, s_cachedPlayerPings, sizeof(pings));
    memcpy(moneyServices, s_cachedMoneyServices, sizeof(moneyServices));
    memcpy(moneys, s_cachedPlayerMoneys, sizeof(moneys));
    memcpy(itemServices, s_cachedItemServices, sizeof(itemServices));
    memcpy(hasDefuserFlags, s_cachedHasDefuserFlags, sizeof(hasDefuserFlags));
    memcpy(scopedFlags, s_cachedScopedFlags, sizeof(scopedFlags));
    memcpy(defusingFlags, s_cachedDefusingFlags, sizeof(defusingFlags));
    memcpy(flashDurations, s_cachedFlashDurations, sizeof(flashDurations));
    memset(scopedReadFresh, 0, sizeof(scopedReadFresh));
    memset(defusingReadFresh, 0, sizeof(defusingReadFresh));
    memset(flashReadFresh, 0, sizeof(flashReadFresh));
    memcpy(eyeAnglesPerPlayer, s_cachedEyeAnglesPerPlayer, sizeof(eyeAnglesPerPlayer));
    memcpy(spottedMasks, s_cachedSpottedMasks, sizeof(spottedMasks));
    static int s_cachedCoreTeams[64] = {};
    static int s_cachedCoreArmors[64] = {};
    static uintptr_t s_cachedCoreArmorPawns[64] = {};
    static uint64_t s_lastCoreArmorReadUs[64] = {};
    static uintptr_t s_cachedCoreTeamPawns[64] = {};
    static uint64_t s_lastCoreTeamReadUs[64] = {};
    static uint64_t s_coreTeamCacheResetSerial = 0;
    {
        const uint64_t coreTeamResetSerial = s_sceneResetSerial.load(std::memory_order_relaxed);
        if (s_coreTeamCacheResetSerial != coreTeamResetSerial) {
            s_coreTeamCacheResetSerial = coreTeamResetSerial;
            memset(s_cachedCoreTeams, 0, sizeof(s_cachedCoreTeams));
            memset(s_cachedCoreArmors, 0, sizeof(s_cachedCoreArmors));
            memset(s_cachedCoreArmorPawns, 0, sizeof(s_cachedCoreArmorPawns));
            memset(s_lastCoreArmorReadUs, 0, sizeof(s_lastCoreArmorReadUs));
            memset(s_cachedCoreTeamPawns, 0, sizeof(s_cachedCoreTeamPawns));
            memset(s_lastCoreTeamReadUs, 0, sizeof(s_lastCoreTeamReadUs));
        }
    }
    memcpy(armors, s_cachedCoreArmors, sizeof(armors));
    const int playerCacheSlotLimit = std::max(
        playerSlotScanLimit,
        std::clamp(s_playerHierarchyHighWaterSlot.load(std::memory_order_relaxed), 0, 64));
    for (int i = 0; i < playerCacheSlotLimit; ++i) {
        if (controllers[i] != s_cachedIdentityControllers[i]) {
            memset(names[i], 0, sizeof(names[i]));
            pings[i] = 0;
        }
        if (controllers[i] != s_cachedMoneyControllers[i]) {
            moneyServices[i] = 0;
            moneys[i] = 0;
        }
        if (pawns[i] != s_cachedDynamicPawns[i]) {
            itemServices[i] = 0;
            hasDefuserFlags[i] = 0;
            scopedFlags[i] = 0;
            defusingFlags[i] = 0;
            flashBangTimes[i] = 0.0f;
            flashDurations[i] = 0.0f;
            eyeAnglesPerPlayer[i] = {};
            spottedMasks[i][0] = 0;
            spottedMasks[i][1] = 0;
        }
        if (!pawns[i]) {
            teams[i] = 0;
            armors[i] = 0;
            s_cachedCoreArmors[i] = 0;
            s_cachedCoreArmorPawns[i] = 0;
            s_lastCoreArmorReadUs[i] = 0;
            s_cachedCoreTeamPawns[i] = 0;
            s_lastCoreTeamReadUs[i] = 0;
            continue;
        }
        if (s_cachedCoreArmorPawns[i] != pawns[i]) {
            armors[i] = 0;
            s_cachedCoreArmors[i] = 0;
            s_lastCoreArmorReadUs[i] = 0;
            s_cachedCoreArmorPawns[i] = pawns[i];
        }
        if (s_cachedCoreTeamPawns[i] != pawns[i]) {
            s_cachedCoreTeams[i] = 0;
            s_lastCoreTeamReadUs[i] = 0;
            s_cachedCoreTeamPawns[i] = pawns[i];
        }
        teams[i] = s_cachedCoreTeams[i];
    }
    const bool hasSpottedMaskOffset =
        ofs.C_CSPlayerPawn_m_entitySpottedState > 0 &&
        ofs.EntitySpottedState_t_m_bSpottedByMask > 0;
    const bool hasSpottedStateOffsets = hasSpottedMaskOffset;
    const bool wantsFastScopedFlag =
        webRadarDemandActive ||
        wantsTargetWeaponState ||
        (wantsEspFlags && wantsEspFlagScoped);
    const bool wantsFastDefusingFlag =
        webRadarDemandActive ||
        wantsEspBombInfo ||
        wantsRadarShowBomb ||
        (wantsEspFlags && wantsEspFlagDefusing);
    const bool wantsFastBlindFlag =
        webRadarDemandActive ||
        (wantsEspFlags && wantsEspFlagBlind);
    const bool wantsFastEyeAngles =
        webRadarDemandActive ||
        (wantsRadarEnabled && wantsRadarShowAngles);
    const bool wantsFastSpottedState =
        wantsPlayerVisibility;
    const bool wantsGunGameImmunity =
        wantsTargetWeaponState &&
        ofs.C_CSPlayerPawn_m_bGunGameImmunity > 0;
    auto teamLooksValid = [](int teamValue) -> bool {
        return teamValue == 1 || teamValue == 2 || teamValue == 3;
    };
    #include "player_parts/player_core_reads.inl"
    _playerCoreUs = TickNowUs() - _playerCoreStartUs;
    const uint64_t _playerRepairStartUs = TickNowUs();
    #include "player_parts/player_repair.inl"
    _playerRepairUs = TickNowUs() - _playerRepairStartUs;

    int plausibleCoreCount = 0;
    uint64_t incompleteCoreMask = 0;
    uint64_t invalidCoreMask = 0;
    for (int resolvedIdx = 0; resolvedIdx < playerResolvedSlotCount; ++resolvedIdx) {
        const int slot = playerResolvedSlots[resolvedIdx];
        if (coreReadPlausible[slot])
            ++plausibleCoreCount;
        else if (!coreReadCompleted(slot))
            incompleteCoreMask |= uint64_t{1} << slot;
        else
            invalidCoreMask |= uint64_t{1} << slot;
    }
    s_playerCoreIncompleteMask.store(incompleteCoreMask, std::memory_order_relaxed);
    s_playerCoreInvalidMask.store(invalidCoreMask, std::memory_order_relaxed);
    static uint64_t s_coreBatchResetSerial = 0;
    static uint64_t s_coreBatchPartialSinceUs = 0;
    static uint64_t s_coreBatchLastAcceptedUs = 0;
    const uint64_t playerCoreBatchCaptureUs = TickNowUs();
    const uint64_t coreBatchResetSerial =
        s_sceneResetSerial.load(std::memory_order_relaxed);
    if (s_coreBatchResetSerial != coreBatchResetSerial) {
        s_coreBatchResetSerial = coreBatchResetSerial;
        s_coreBatchPartialSinceUs = 0;
        s_coreBatchLastAcceptedUs = 0;
    }
    if (playerResolvedSlotCount > 0 &&
        plausibleCoreCount < playerResolvedSlotCount) {
        if (s_coreBatchPartialSinceUs == 0)
            s_coreBatchPartialSinceUs = playerCoreBatchCaptureUs;
    } else {
        s_coreBatchPartialSinceUs = 0;
    }
    const auto playerCoreBatchDecision =
        esp::data::SelectPlayerCoreBatchDecision(
            playerResolvedSlotCount,
            plausibleCoreCount,
            s_coreBatchPartialSinceUs,
            s_coreBatchLastAcceptedUs,
            playerCoreBatchCaptureUs);
    const bool coreRosterCommitAllowed =
        playerCoreBatchDecision != esp::data::PlayerCoreBatchDecision::Hold;
    if (coreRosterCommitAllowed) {
        s_coreBatchLastAcceptedUs = playerCoreBatchCaptureUs;
    } else if (playerResolvedSlotCount > 0 &&
               s_engineInGame.load(std::memory_order_relaxed)) {
        s_playerCoreBatchHoldCount.fetch_add(1, std::memory_order_relaxed);
    }
    const auto playersCoreHealth = esp::data::EvaluatePlayerCoreHealth(
        s_engineInGame.load(std::memory_order_relaxed),
        playerResolvedSlotCount,
        plausibleCoreCount,
        playersCoreHadFailure,
        playersCoreHardFailure);
    if (playersCoreHealth == esp::SubsystemHealthState::Unknown) {
        SetSubsystemUnknown(RuntimeSubsystem::PlayersCore);
    } else if (playersCoreHealth == esp::SubsystemHealthState::Failed) {
        MarkSubsystemFailed(RuntimeSubsystem::PlayersCore, TickNowUs());
    } else if (playersCoreHealth == esp::SubsystemHealthState::Degraded) {
        MarkSubsystemDegraded(RuntimeSubsystem::PlayersCore, TickNowUs());
    } else {
        MarkSubsystemHealthy(RuntimeSubsystem::PlayersCore, TickNowUs());
    }
    s_playerResolvedSlotCountStat.store(
        playerResolvedSlotCount,
        std::memory_order_relaxed);
    s_playerPlausibleCoreSlotCountStat.store(
        plausibleCoreCount,
        std::memory_order_relaxed);
