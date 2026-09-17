    int localTeam = s_localTeam;
    bool localTeamLiveResolved = false;
    esp::data::PlayerTeamSample localControllerTeam = 0;
    uint32_t localControllerPawnHandle = 0;
    int localPawnHealth = -1;
    DWORD localPawnHealthBytesRead = 0, localPawnLifeStateBytesRead = 0;
    DWORD localControllerTeamBytesRead = 0, liveLocalTeamBytesRead = 0;
    DWORD localFovSensitivityBytesRead = 0;
    uint8_t localPawnLifeState = 0xFF;
    int localCrosshairEntityIndex = -1;
    DWORD localCrosshairEntityIndexBytesRead = 0;
    DWORD fallbackViewMatrixBytesRead = 0;
    uint64_t fallbackViewMatrixSampleUs = 0;
    bool localCrosshairReadValid = false;
    bool localPawnCoreLiveResolved = false;
    Vector3 localPos = s_localPos;
    DWORD localPosBytesRead = 0;
    bool localPosReadValid = false;
    Vector3 localViewOffset = s_localViewOffset;
    DWORD localViewOffsetBytesRead = 0;
    bool localViewOffsetReadValid = false;
    Vector3 localAimPunch = {};
    int localShotsFired = 0;
    bool localAimPunchValid = false;
    bool localShotsFiredValid = false;
    float localFovSensitivityAdjust = s_fovSensitivityAdjust;
    float sensValue = s_sensitivity;
    Vector3 minimapMins = s_minimapMins;
    Vector3 minimapMaxs = s_minimapMaxs;
    bool minimapBoundsValid = s_hasMinimapBounds;
    bool bombPlantedByRules = false;
    bool bombDroppedByRules = false;
    bool bombRoundChangedThisTick = false;
    uint8_t bombTicking = 0;
    uint8_t bombBeingDefused = 0;
    uint8_t bombHasExploded = 0;
    uint8_t bombDefused = 0;
    uint8_t bombActivated = 0;
    uint32_t bombDefuserHandle = 0;
    float bombBlowTime = 0.0f;
    float bombTimerLength = 0.0f;
    float bombDefuseEndTime = 0.0f;
    float bombDefuseLength = 0.0f;
    float currentGameTime = s_lastStableGameTime;
    bool currentGameTimeFresh = false;
    float intervalPerTick = s_lastStableIntervalPerTick;
    highestEntityIndex = std::max(0, s_highestEntityIdxStat.load(std::memory_order_relaxed));
    uintptr_t bombSceneNode = 0;
    Vector3 bombWorldPos = { NAN, NAN, NAN };
    Vector3 bombCollisionMins = s_bombState.boundsMins;
    Vector3 bombCollisionMaxs = s_bombState.boundsMaxs;
    uintptr_t weaponC4SceneNode = 0;
    Vector3 weaponC4WorldPos = { NAN, NAN, NAN };
    Vector3 weaponC4Velocity = {};
    Vector3 weaponC4CollisionMins = {};
    Vector3 weaponC4CollisionMaxs = {};
    uint32_t weaponC4OwnerHandle = 0;
    uint32_t weaponC4DropTick = 0;
    uint8_t weaponC4CanBePickedUp = 0;
    bool weaponC4CanBePickedUpKnown = false;
    bool weaponC4RecentDropTickEdge = false;
    bool weaponC4RecentOwnerAttach = false;
    bool weaponC4DetachedCurrent = false;
    bool weaponC4PosValid = false;
    char liveMapNameRaw[64] = {};

    auto sanitizePointer = [&](uintptr_t value) -> uintptr_t {
        return isLikelyGamePointer(value) ? value : 0;
    };

    auto readValue = [&](uintptr_t address, void* outValue, size_t size) -> bool {
        if (!address || !outValue || size == 0)
            return false;
        return mem.Read(address, outValue, size);
    };

    auto readPointer = [&](uintptr_t address, uintptr_t* outValue) -> bool {
        uintptr_t value = 0;
        if (!readValue(address, &value, sizeof(value))) {
            if (outValue)
                *outValue = 0;
            return false;
        }
        value = sanitizePointer(value);
        if (outValue)
            *outValue = value;
        return value != 0;
    };

    auto tryReadSaneFloat = [&](uintptr_t baseAddress,
                                const std::ptrdiff_t* candidateOffsets,
                                size_t candidateCount,
                                float minValue,
                                float maxValue,
                                float* outValue) -> bool {
        if (!baseAddress || !candidateOffsets || !candidateCount || !outValue)
            return false;

        for (size_t i = 0; i < candidateCount; ++i) {
            const std::ptrdiff_t offset = candidateOffsets[i];
            if (offset <= 0)
                continue;

            float value = 0.0f;
            if (!readValue(baseAddress + static_cast<uintptr_t>(offset), &value, sizeof(value)))
                continue;
            if (!std::isfinite(value) || value < minValue || value > maxValue)
                continue;

            *outValue = value;
            return true;
        }

        return false;
    };

    
    
    
    
    
    
    static esp::data::BasePointerSample s_basePointerSamples[8] = {};
    static uintptr_t s_cachedEntityList = 0;
    static uintptr_t s_cachedListEntry = 0;
    static uintptr_t s_cbpLocalPawn = 0;
    static uintptr_t s_cbpLocalController = 0;
    static uintptr_t s_cbpGameRules = 0;
    static uintptr_t s_cbpGlobalVars = 0;
    static uintptr_t s_cbpSensPtr = 0;
    static uintptr_t s_cbpPlantedC4 = 0;
    static uintptr_t s_cachedAimPunchServices = 0;
    static bool s_cachedBombPlantedByRules = false;
    static bool s_cachedBombDroppedByRules = false;
    static uint8_t s_bombPlantedRulesFalseStreak = 0;
    static uint8_t s_bombDroppedRulesFalseStreak = 0;
    static uint64_t s_cbpResetSerial = 0;
    static uintptr_t s_pendingEntityList = 0;
    static uint32_t s_pendingEntityListConfirmCount = 0;
    static uint64_t s_pendingEntityListFirstSeenUs = 0;
    static uint64_t s_lastHighestEntityRefreshUs = 0;
    static uint64_t s_lastMinimapRefreshUs = 0;
    static uint64_t s_lastSensitivityRefreshUs = 0;
    static uint64_t s_lastIntervalRefreshUs = 0;
    static uint64_t s_lastViewFallbackRefreshUs = 0;
    static uint64_t s_lastGameTimeCandidateSweepUs = 0;
    static uintptr_t s_pendingGameRulesCandidate = 0;
    static uint32_t s_pendingGameRulesConfirmCount = 0;
    static uint32_t s_gameRulesSanityFailureStreak = 0;
    static uint64_t s_gameRulesSanityFailureSinceUs = 0;
    static uint64_t s_lastGameRulesSanityLogUs = 0;
    static uintptr_t s_bombRoundCounterGameRules = 0;
    static esp::data::BombRoundCounterTracker s_bombRoundCounterTracker;
    static bool s_entityHierarchyMissing = false;
    static uint32_t s_entityHierarchyMissingStreak = 0;
    static uint64_t s_entityHierarchyMissingSinceUs = 0;
    static uint64_t s_lastEntityHierarchyRecoveryUs = 0;
    static esp::data::EntityHierarchyRecoveryAction s_entityHierarchyRecoveryStage =
        esp::data::EntityHierarchyRecoveryAction::None;
    static uint32_t s_entityHierarchyFlatStreak = 0;
    static uint64_t s_entityHierarchyFlatSinceUs = 0;
    static uint64_t s_lastEntityHierarchyFlatRecoveryUs = 0;
    static esp::data::EntityHierarchyRecoveryAction s_entityHierarchyFlatRecoveryStage =
        esp::data::EntityHierarchyRecoveryAction::None;
    static uintptr_t s_cachedMatchmakingBase = 0;
    static uint64_t s_lastLiveMapNameRefreshUs = 0;
    static std::string s_cachedLiveMapKey;
    static size_t s_currentTimeCandidateIndex = static_cast<size_t>(-1);
    auto resetMissingHierarchyRecovery = [&]() {
        s_entityHierarchyMissing = false;
        s_entityHierarchyMissingStreak = 0;
        s_entityHierarchyMissingSinceUs = 0;
        s_lastEntityHierarchyRecoveryUs = 0;
        s_entityHierarchyRecoveryStage =
            esp::data::EntityHierarchyRecoveryAction::None;
    };
    auto resetFlatHierarchyRecovery = [&]() {
        s_entityHierarchyFlatStreak = 0;
        s_entityHierarchyFlatSinceUs = 0;
        s_lastEntityHierarchyFlatRecoveryUs = 0;
        s_entityHierarchyFlatRecoveryStage =
            esp::data::EntityHierarchyRecoveryAction::None;
    };
    auto resetEntityHierarchyRecovery = [&]() {
        resetMissingHierarchyRecovery();
        resetFlatHierarchyRecovery();
    };
    {
        const uint64_t cbpSceneSerial = s_sceneResetSerial.load(std::memory_order_relaxed);
        if (s_cbpResetSerial != cbpSceneSerial) {
            s_cbpResetSerial = cbpSceneSerial;
            for (auto& sample : s_basePointerSamples)
                sample = {};
            s_cbpLocalPawn = 0; s_cbpLocalController = 0;
            s_cbpGameRules = 0; s_cbpGlobalVars = 0; s_cbpSensPtr = 0;
            s_cbpPlantedC4 = 0;
            s_cachedAimPunchServices = 0;
            s_cachedBombPlantedByRules = false;
            s_cachedBombDroppedByRules = false;
            s_bombPlantedRulesFalseStreak = 0;
            s_bombDroppedRulesFalseStreak = 0;
            
            
            
            
            
            
            s_cachedEntityList = 0;
            s_cachedListEntry = 0;
            s_pendingEntityList = 0;
            s_pendingEntityListConfirmCount = 0;
            s_pendingEntityListFirstSeenUs = 0;
            s_lastHighestEntityRefreshUs = 0;
            s_lastMinimapRefreshUs = 0;
            s_lastSensitivityRefreshUs = 0;
            s_lastIntervalRefreshUs = 0;
            s_lastViewFallbackRefreshUs = 0;
            s_lastGameTimeCandidateSweepUs = 0;
            s_pendingGameRulesCandidate = 0;
            s_pendingGameRulesConfirmCount = 0;
            s_gameRulesSanityFailureStreak = 0;
            s_gameRulesSanityFailureSinceUs = 0;
            s_lastGameRulesSanityLogUs = 0;
            s_bombRoundCounterGameRules = 0;
            s_bombRoundCounterTracker.Reset();
            
            
            resetEntityHierarchyRecovery();
            s_cachedMatchmakingBase = 0;
            s_lastLiveMapNameRefreshUs = 0;
            s_cachedLiveMapKey.clear();
            s_currentTimeCandidateIndex = static_cast<size_t>(-1);
        }
    }
    {
        const uint64_t baseNowUs = TickNowUs();
        liveMapKey = s_cachedLiveMapKey;
        const int baseSignOnState = s_engineSignOnState.load(std::memory_order_relaxed);
        const bool engineMatchLikeBase =
            s_engineInGame.load(std::memory_order_relaxed) &&
            !s_engineMenu.load(std::memory_order_relaxed) &&
            baseSignOnState == 6;
        if (!engineMatchLikeBase &&
            s_engineStatusResolved.load(std::memory_order_relaxed) &&
            s_engineMenu.load(std::memory_order_relaxed))
            s_lastLiveMapNameSeenUs.store(0, std::memory_order_relaxed);
        static constexpr std::ptrdiff_t kIntervalCandidates[] = {
            0x14, 0x10, 0x18, 0x1C, 0x2C
        };
        static constexpr std::ptrdiff_t kCurrentTimeCandidates[] = {
            // Client simulation time used by the reference collector. Prefer
            // this to the adjacent clock; retain validated fallback candidates.
            0x30, 0x34, 0x2C, 0x20, 0x24
        };
        const bool highestEntityRefreshDue =
            s_lastHighestEntityRefreshUs == 0 ||
            (baseNowUs - s_lastHighestEntityRefreshUs) >= esp::intervals::kBaseHighestEntityRefreshUs;
        const bool minimapRefreshDue =
            !s_hasMinimapBounds ||
            s_lastMinimapRefreshUs == 0 ||
            (baseNowUs - s_lastMinimapRefreshUs) >= esp::intervals::kBaseMinimapRefreshUs;
        const bool sensitivityRefreshDue =
            s_sensitivity <= 0.0f ||
            s_lastSensitivityRefreshUs == 0 ||
            (baseNowUs - s_lastSensitivityRefreshUs) >= esp::intervals::kBaseSensitivityRefreshUs;
        const bool intervalRefreshDue =
            s_lastStableIntervalPerTick <= 0.0f ||
            s_lastIntervalRefreshUs == 0 ||
            (baseNowUs - s_lastIntervalRefreshUs) >= esp::intervals::kBaseIntervalRefreshUs;
        const bool viewFallbackRefreshDue =
            s_lastViewFallbackRefreshUs == 0 ||
            baseNowUs < s_lastViewFallbackRefreshUs ||
            (baseNowUs - s_lastViewFallbackRefreshUs) >=
                esp::intervals::kBaseViewFallbackRefreshUs;
        const bool gameTimeProgressStalled =
            s_lastStableGameTimeUs > 0 &&
            baseNowUs >= s_lastStableGameTimeUs &&
            (baseNowUs - s_lastStableGameTimeUs) >=
                esp::intervals::kBaseGameTimeStallProbeUs;
        const uint64_t gameTimeSweepIntervalUs =
            gameTimeProgressStalled
                ? esp::intervals::kBaseGameTimeStallProbeUs
                : esp::intervals::kBaseGameTimeSweepUs;
        const bool gameTimeCandidateSweepDue =
            s_currentTimeCandidateIndex >= std::size(kCurrentTimeCandidates) ||
            s_lastGameTimeCandidateSweepUs == 0 ||
            baseNowUs < s_lastGameTimeCandidateSweepUs ||
            (baseNowUs - s_lastGameTimeCandidateSweepUs) >=
                gameTimeSweepIntervalUs;
        const bool liveMapNameRefreshDue =
            s_lastLiveMapNameRefreshUs == 0 ||
            (baseNowUs - s_lastLiveMapNameRefreshUs) >= esp::intervals::kBaseMinimapRefreshUs;
        
        uintptr_t rawEntityList = 0, rawLocalPawn = 0, rawLocalController = 0;
        uintptr_t rawGameRules = 0, rawGlobalVars = 0;
        uintptr_t rawPlantedC4 = 0, rawWeaponC4 = 0, rawSensPtr = 0;
        DWORD rootBytesRead[8] = {};
        DWORD rawListEntryBytesRead = 0, highestEntityBytesRead = 0;
        DWORD liveSensitivityBytesRead = 0;
        DWORD minimapMinsBytesRead = 0, minimapMaxsBytesRead = 0;
        DWORD viewAnglesBytesRead = 0, localControllerPawnHandleBytesRead = 0;
        DWORD liveMapNameBytesRead = 0;
        DWORD aimPunchServicesBytesRead = 0;
        uintptr_t localVitalSamplePawn = s_cbpLocalPawn;
        uintptr_t rawLocalAimPunchServices = 0;
        const uintptr_t queuedAimPunchServices = s_cachedAimPunchServices;
        DWORD localAimPunchBytesRead = 0;
        DWORD localShotsFiredBytesRead = 0;
        
        uintptr_t rawListEntry = 0;
        esp::data::PlayerTeamSample liveLocalTeam = 0;
        uint8_t bombPlantedFlag = 0;
        uint8_t bombDroppedFlag = 0;
        esp::data::BombRoundStartCounter gameRulesRoundStartCount = 0;
        uint32_t gameRulesTotalRoundsPlayed = 0;
        DWORD bombPlantedFlagBytesRead = 0;
        DWORD bombDroppedFlagBytesRead = 0;
        DWORD gameRulesRoundStartCountBytesRead = 0;
        DWORD gameRulesTotalRoundsPlayedBytesRead = 0;
        uintptr_t gameRulesReadSource = 0;
        float liveSensitivity = 0.0f;
        float intervalCandidateValues[std::size(kIntervalCandidates)] = {};
        float currentTimeCandidateValues[std::size(kCurrentTimeCandidates)] = {};
        DWORD intervalCandidateReadBytes[std::size(kIntervalCandidates)] = {};
        DWORD currentTimeCandidateReadBytes[std::size(kCurrentTimeCandidates)] = {};
        bool fullGameTimeCandidateSweepQueued = false;
        auto queueCurrentTimeCandidates = [&](
            uintptr_t globalVarsAddress,
            bool forceFullSweep) {
            if (!globalVarsAddress)
                return;
            for (size_t i = 0; i < std::size(kCurrentTimeCandidates); ++i) {
                if (!forceFullSweep && i != s_currentTimeCandidateIndex)
                    continue;
                mem.AddScatterReadRequest(
                    handle,
                    globalVarsAddress +
                        static_cast<uintptr_t>(kCurrentTimeCandidates[i]),
                    &currentTimeCandidateValues[i],
                    sizeof(float),
                    &currentTimeCandidateReadBytes[i]);
            }
            fullGameTimeCandidateSweepQueued =
                fullGameTimeCandidateSweepQueued || forceFullSweep;
        };



        if (ofs.dwEntityList > 0)
            mem.AddScatterReadRequest(handle, g::clientBase + ofs.dwEntityList, &rawEntityList, sizeof(rawEntityList), &rootBytesRead[0]);
        if (ofs.dwLocalPlayerPawn > 0)
            mem.AddScatterReadRequest(handle, g::clientBase + ofs.dwLocalPlayerPawn, &rawLocalPawn, sizeof(rawLocalPawn), &rootBytesRead[1]);
        if (ofs.dwLocalPlayerController > 0)
            mem.AddScatterReadRequest(handle, g::clientBase + ofs.dwLocalPlayerController, &rawLocalController, sizeof(rawLocalController), &rootBytesRead[2]);
        if (engineMatchLikeBase && ofs.dwGameRules > 0)
            mem.AddScatterReadRequest(handle, g::clientBase + ofs.dwGameRules, &rawGameRules, sizeof(rawGameRules), &rootBytesRead[3]);
        if (ofs.dwGlobalVars > 0)
            mem.AddScatterReadRequest(handle, g::clientBase + ofs.dwGlobalVars, &rawGlobalVars, sizeof(rawGlobalVars), &rootBytesRead[4]);
        if (engineMatchLikeBase && ofs.dwPlantedC4 > 0)
            mem.AddScatterReadRequest(handle, g::clientBase + ofs.dwPlantedC4, &rawPlantedC4, sizeof(rawPlantedC4), &rootBytesRead[5]);
        if (engineMatchLikeBase && ofs.dwWeaponC4 > 0)
            mem.AddScatterReadRequest(handle, g::clientBase + ofs.dwWeaponC4, &rawWeaponC4, sizeof(rawWeaponC4), &rootBytesRead[6]);
        if (ofs.dwSensitivity > 0)
            mem.AddScatterReadRequest(handle, g::clientBase + ofs.dwSensitivity, &rawSensPtr, sizeof(rawSensPtr), &rootBytesRead[7]);
        if (viewFallbackRefreshDue && ofs.dwViewMatrix > 0) {
            fallbackViewMatrixSampleUs = TickNowUs();
            mem.AddScatterReadRequest(handle, g::clientBase + ofs.dwViewMatrix,
                &viewMatrix, sizeof(viewMatrix), &fallbackViewMatrixBytesRead);
        }
        if (viewFallbackRefreshDue && ofs.dwViewAngles > 0)
            mem.AddScatterReadRequest(handle, g::clientBase + ofs.dwViewAngles, &viewAngles, sizeof(viewAngles), &viewAnglesBytesRead);
        if (engineMatchLikeBase && liveMapNameRefreshDue && ofs.dwGameTypes > 0) {
            if (esp::data::ShouldResolveMatchmakingBase(s_cachedMatchmakingBase))
                s_cachedMatchmakingBase = mem.GetModuleBase("matchmaking.dll");
            std::ptrdiff_t mapNameRva = ofs.dwGameTypes + static_cast<std::ptrdiff_t>(0x120);
            if (ofs.dwGameTypes_mapName > ofs.dwGameTypes)
                mapNameRva = ofs.dwGameTypes_mapName;
            else if (ofs.dwGameTypes_mapName > 0 && ofs.dwGameTypes_mapName < 0x10000)
                mapNameRva = ofs.dwGameTypes + ofs.dwGameTypes_mapName;
            if (s_cachedMatchmakingBase && mapNameRva > 0)
                mem.AddScatterReadRequest(handle, s_cachedMatchmakingBase + static_cast<uintptr_t>(mapNameRva), liveMapNameRaw, sizeof(liveMapNameRaw) - 1, &liveMapNameBytesRead);
        }

        
        const bool hasCachedPointers = s_cachedEntityList != 0;
        if (hasCachedPointers) {
            mem.AddScatterReadRequest(handle, s_cachedEntityList + 0x10, &rawListEntry, sizeof(rawListEntry), &rawListEntryBytesRead);
            if (highestEntityRefreshDue && ofs.dwGameEntitySystem_highestEntityIndex > 0)
                mem.AddScatterReadRequest(handle, s_cachedEntityList + static_cast<uintptr_t>(ofs.dwGameEntitySystem_highestEntityIndex), &highestEntityIndex, sizeof(highestEntityIndex), &highestEntityBytesRead);
        }
        if (s_cbpLocalController && ofs.CCSPlayerController_m_hPlayerPawn > 0)
            mem.AddScatterReadRequest(handle, s_cbpLocalController + ofs.CCSPlayerController_m_hPlayerPawn, &localControllerPawnHandle, sizeof(localControllerPawnHandle), &localControllerPawnHandleBytesRead);
        if (s_cbpLocalController && ofs.C_BaseEntity_m_iTeamNum > 0)
            mem.AddScatterReadRequest(handle, s_cbpLocalController + ofs.C_BaseEntity_m_iTeamNum, &localControllerTeam, sizeof(localControllerTeam), &localControllerTeamBytesRead);
        if (s_cbpLocalPawn) {
            if (ofs.C_BaseEntity_m_iTeamNum > 0)
                mem.AddScatterReadRequest(handle, s_cbpLocalPawn + ofs.C_BaseEntity_m_iTeamNum, &liveLocalTeam, sizeof(liveLocalTeam), &liveLocalTeamBytesRead);
            if (ofs.C_BaseEntity_m_iHealth > 0)
                mem.AddScatterReadRequest(handle, s_cbpLocalPawn + ofs.C_BaseEntity_m_iHealth, &localPawnHealth, sizeof(localPawnHealth), &localPawnHealthBytesRead);
            if (ofs.C_BaseEntity_m_lifeState > 0)
                mem.AddScatterReadRequest(handle, s_cbpLocalPawn + ofs.C_BaseEntity_m_lifeState, &localPawnLifeState, sizeof(localPawnLifeState), &localPawnLifeStateBytesRead);
            if (ofs.C_BasePlayerPawn_m_vOldOrigin > 0)
                mem.AddScatterReadRequest(handle, s_cbpLocalPawn + ofs.C_BasePlayerPawn_m_vOldOrigin, &localPos, sizeof(localPos), &localPosBytesRead);
            if (ofs.C_BaseModelEntity_m_vecViewOffset > 0)
                mem.AddScatterReadRequest(handle, s_cbpLocalPawn + ofs.C_BaseModelEntity_m_vecViewOffset, &localViewOffset, sizeof(localViewOffset), &localViewOffsetBytesRead);
            if (ofs.C_BasePlayerPawn_m_flFOVSensitivityAdjust > 0)
                mem.AddScatterReadRequest(handle, s_cbpLocalPawn + ofs.C_BasePlayerPawn_m_flFOVSensitivityAdjust, &localFovSensitivityAdjust, sizeof(localFovSensitivityAdjust), &localFovSensitivityBytesRead);
            if (ofs.C_CSPlayerPawnBase_m_iIDEntIndex > 0)
                mem.AddScatterReadRequest(handle, s_cbpLocalPawn + ofs.C_CSPlayerPawnBase_m_iIDEntIndex, &localCrosshairEntityIndex, sizeof(localCrosshairEntityIndex), &localCrosshairEntityIndexBytesRead);
            if (wantsTargetRecoil && ofs.C_CSPlayerPawn_m_pAimPunchServices > 0)
                mem.AddScatterReadRequest(handle, s_cbpLocalPawn + ofs.C_CSPlayerPawn_m_pAimPunchServices, &rawLocalAimPunchServices, sizeof(rawLocalAimPunchServices), &aimPunchServicesBytesRead);
            if (wantsTargetRecoil && ofs.C_CSPlayerPawn_m_iShotsFired > 0)
                mem.AddScatterReadRequest(handle, s_cbpLocalPawn + ofs.C_CSPlayerPawn_m_iShotsFired, &localShotsFired, sizeof(localShotsFired), &localShotsFiredBytesRead);
        }
        if (wantsTargetRecoil && queuedAimPunchServices &&
            ofs.CCSPlayer_AimPunchServices_m_predictableBaseAngle > 0) {
            mem.AddScatterReadRequest(
                handle,
                queuedAimPunchServices +
                    ofs.CCSPlayer_AimPunchServices_m_predictableBaseAngle,
                &localAimPunch,
                sizeof(localAimPunch),
                &localAimPunchBytesRead);
        }
        if (engineMatchLikeBase && s_cbpGameRules) {
            gameRulesReadSource = s_cbpGameRules;
            if (minimapRefreshDue && ofs.C_CSGameRules_m_vMinimapMins > 0)
                mem.AddScatterReadRequest(handle, s_cbpGameRules + ofs.C_CSGameRules_m_vMinimapMins, &minimapMins, sizeof(minimapMins), &minimapMinsBytesRead);
            if (minimapRefreshDue && ofs.C_CSGameRules_m_vMinimapMaxs > 0)
                mem.AddScatterReadRequest(handle, s_cbpGameRules + ofs.C_CSGameRules_m_vMinimapMaxs, &minimapMaxs, sizeof(minimapMaxs), &minimapMaxsBytesRead);
            if (ofs.C_CSGameRules_m_bBombPlanted > 0)
                mem.AddScatterReadRequest(handle, s_cbpGameRules + ofs.C_CSGameRules_m_bBombPlanted, &bombPlantedFlag, sizeof(bombPlantedFlag), &bombPlantedFlagBytesRead);
            if (ofs.C_CSGameRules_m_bBombDropped > 0)
                mem.AddScatterReadRequest(handle, s_cbpGameRules + ofs.C_CSGameRules_m_bBombDropped, &bombDroppedFlag, sizeof(bombDroppedFlag), &bombDroppedFlagBytesRead);
            if (ofs.C_CSGameRules_m_nRoundStartCount > 0)
                mem.AddScatterReadRequest(handle, s_cbpGameRules + ofs.C_CSGameRules_m_nRoundStartCount, &gameRulesRoundStartCount, sizeof(gameRulesRoundStartCount), &gameRulesRoundStartCountBytesRead);
            if (ofs.C_CSGameRules_m_totalRoundsPlayed > 0)
                mem.AddScatterReadRequest(handle, s_cbpGameRules + ofs.C_CSGameRules_m_totalRoundsPlayed, &gameRulesTotalRoundsPlayed, sizeof(gameRulesTotalRoundsPlayed), &gameRulesTotalRoundsPlayedBytesRead);
        }
        if (s_cbpSensPtr && sensitivityRefreshDue && ofs.dwSensitivity_sensitivity > 0)
            mem.AddScatterReadRequest(handle, s_cbpSensPtr + ofs.dwSensitivity_sensitivity, &liveSensitivity, sizeof(liveSensitivity), &liveSensitivityBytesRead);

        if (s_cbpGlobalVars) {
            for (size_t i = 0; i < std::size(kIntervalCandidates); ++i) {
                if (intervalRefreshDue && kIntervalCandidates[i] > 0)
                    mem.AddScatterReadRequest(handle, s_cbpGlobalVars + static_cast<uintptr_t>(kIntervalCandidates[i]), &intervalCandidateValues[i], sizeof(float), &intervalCandidateReadBytes[i]);
            }
            queueCurrentTimeCandidates(
                s_cbpGlobalVars,
                gameTimeCandidateSweepDue);
        }

        const bool baseScatterOk = executeOptionalScatterRead();
        if (!baseScatterOk) {
            logUpdateDataIssue("scatter_1", "base_merged_scatter_failed");
        } else if (viewFallbackRefreshDue &&
                   fallbackViewMatrixBytesRead == sizeof(viewMatrix)) {
            s_lastViewFallbackRefreshUs = baseNowUs;
        }
        if (viewAnglesBytesRead != sizeof(viewAngles) || !IsFiniteVec(viewAngles))
            viewAngles = s_viewAngles;
        liveMapNameRaw[sizeof(liveMapNameRaw) - 1] = '\0';
        if (liveMapNameRefreshDue && liveMapNameBytesRead > 0 &&
            liveMapNameBytesRead <= sizeof(liveMapNameRaw) - 1) {
            // Padding beyond the completed prefix is not a read terminator.
            const bool hasReadTerminator = std::memchr(liveMapNameRaw, '\0', liveMapNameBytesRead) != nullptr;
            std::string normalizedMap = hasReadTerminator
                ? radar::NormalizeMapName(liveMapNameRaw) : std::string{};
            if (normalizedMap.empty()) {
                uintptr_t mapNamePointer = 0;
                memcpy(&mapNamePointer, liveMapNameRaw, sizeof(mapNamePointer));
                const bool plausibleStringPointer =
                    liveMapNameBytesRead >= sizeof(mapNamePointer) &&
                    app::memory_address::IsCanonicalUserPointer(mapNamePointer);
                if (plausibleStringPointer) {
                    char pointedMapName[64] = {};
                    if (readValue(mapNamePointer, pointedMapName, sizeof(pointedMapName) - 1)) {
                        pointedMapName[sizeof(pointedMapName) - 1] = '\0';
                        normalizedMap = radar::NormalizeMapName(pointedMapName);
                    }
                }
            }
            if (!normalizedMap.empty()) {
                liveMapKey = normalizedMap;
                s_cachedLiveMapKey = normalizedMap;
                s_lastLiveMapNameRefreshUs = baseNowUs;
                s_lastLiveMapNameSeenUs.store(baseNowUs, std::memory_order_relaxed);
            }
        }

        
        entityList = s_basePointerSamples[0].Resolve(rawEntityList, rootBytesRead[0], baseNowUs);
        localPawn = s_basePointerSamples[1].Resolve(rawLocalPawn, rootBytesRead[1], baseNowUs);
        localController = s_basePointerSamples[2].Resolve(rawLocalController, rootBytesRead[2], baseNowUs);
        gameRules = engineMatchLikeBase ? s_basePointerSamples[3].Resolve(rawGameRules, rootBytesRead[3], baseNowUs) : 0;
        globalVars = s_basePointerSamples[4].Resolve(rawGlobalVars, rootBytesRead[4], baseNowUs);
        // C4 continuity belongs to the round-aware bomb state machine. A
        // generic pointer hold must not revive an entity from the last round.
        plantedC4Entity = engineMatchLikeBase ? s_basePointerSamples[5].Resolve(rawPlantedC4, rootBytesRead[5], baseNowUs, false) : 0;
        weaponC4Entity = engineMatchLikeBase ? s_basePointerSamples[6].Resolve(rawWeaponC4, rootBytesRead[6], baseNowUs, false) : 0;
        sensPtr = s_basePointerSamples[7].Resolve(rawSensPtr, rootBytesRead[7], baseNowUs);

        const bool pointersChanged =
            !hasCachedPointers ||
            entityList != s_cachedEntityList ||
            localPawn != s_cbpLocalPawn ||
            localController != s_cbpLocalController ||
            gameRules != s_cbpGameRules ||
            globalVars != s_cbpGlobalVars ||
            sensPtr != s_cbpSensPtr ||
            plantedC4Entity != s_cbpPlantedC4;

        bool dependentScatterOk = true;
        if (pointersChanged) {
            
            const bool highestEntityRefreshForced = highestEntityRefreshDue || entityList != s_cachedEntityList;
            const bool minimapRefreshForced = minimapRefreshDue || gameRules != s_cbpGameRules;
            const bool sensitivityRefreshForced = sensitivityRefreshDue || sensPtr != s_cbpSensPtr;
            const bool intervalRefreshForced = intervalRefreshDue || globalVars != s_cbpGlobalVars;
            rawListEntry = 0; liveLocalTeam = 0; bombPlantedFlag = 0; bombDroppedFlag = 0;
            gameRulesRoundStartCount = 0;
            gameRulesTotalRoundsPlayed = 0;
            gameRulesReadSource = 0;
            // A successful cached read belongs to the previous pointer chain.
            // Never carry its completion flags into a replacement-identity read.
            bombPlantedFlagBytesRead = 0;
            bombDroppedFlagBytesRead = 0;
            gameRulesRoundStartCountBytesRead = 0;
            gameRulesTotalRoundsPlayedBytesRead = 0;
            liveSensitivity = 0.0f;
            localControllerPawnHandle = 0;
            localControllerTeam = 0;
            localVitalSamplePawn = localPawn;
            localPawnHealthBytesRead = localPawnLifeStateBytesRead = 0;
            liveLocalTeamBytesRead = localControllerTeamBytesRead = 0;
            localControllerPawnHandleBytesRead = localFovSensitivityBytesRead = 0;
            rawListEntryBytesRead = highestEntityBytesRead = 0;
            minimapMinsBytesRead = minimapMaxsBytesRead = 0;
            liveSensitivityBytesRead = aimPunchServicesBytesRead = 0;
            localPawnHealth = -1;
            localPawnLifeState = 0xFF;
            localPos = {};
            localPosBytesRead = 0;
            localViewOffset = {};
            localViewOffsetBytesRead = 0;
            localFovSensitivityAdjust = 1.0f;
            rawLocalAimPunchServices = 0;
            localShotsFired = 0;
            localShotsFiredBytesRead = 0;
            localCrosshairEntityIndex = -1;
            localCrosshairEntityIndexBytesRead = 0;
            if (localPawn != s_cbpLocalPawn)
                localAimPunchBytesRead = 0;
            if (highestEntityRefreshForced)
                highestEntityIndex = 0;
            memset(intervalCandidateValues, 0, sizeof(intervalCandidateValues));
            memset(currentTimeCandidateValues, 0, sizeof(currentTimeCandidateValues));
            memset(intervalCandidateReadBytes, 0, sizeof(intervalCandidateReadBytes));
            memset(currentTimeCandidateReadBytes, 0, sizeof(currentTimeCandidateReadBytes));

            if (entityList) {
                mem.AddScatterReadRequest(handle, entityList + 0x10, &rawListEntry, sizeof(rawListEntry), &rawListEntryBytesRead);
                if (highestEntityRefreshForced && ofs.dwGameEntitySystem_highestEntityIndex > 0)
                    mem.AddScatterReadRequest(handle, entityList + static_cast<uintptr_t>(ofs.dwGameEntitySystem_highestEntityIndex), &highestEntityIndex, sizeof(highestEntityIndex), &highestEntityBytesRead);
            }
            if (localController && ofs.CCSPlayerController_m_hPlayerPawn > 0)
                mem.AddScatterReadRequest(handle, localController + ofs.CCSPlayerController_m_hPlayerPawn, &localControllerPawnHandle, sizeof(localControllerPawnHandle), &localControllerPawnHandleBytesRead);
            if (localController && ofs.C_BaseEntity_m_iTeamNum > 0)
                mem.AddScatterReadRequest(handle, localController + ofs.C_BaseEntity_m_iTeamNum, &localControllerTeam, sizeof(localControllerTeam), &localControllerTeamBytesRead);
            if (localPawn) {
                if (ofs.C_BaseEntity_m_iTeamNum > 0)
                    mem.AddScatterReadRequest(handle, localPawn + ofs.C_BaseEntity_m_iTeamNum, &liveLocalTeam, sizeof(liveLocalTeam), &liveLocalTeamBytesRead);
                if (ofs.C_BaseEntity_m_iHealth > 0)
                    mem.AddScatterReadRequest(handle, localPawn + ofs.C_BaseEntity_m_iHealth, &localPawnHealth, sizeof(localPawnHealth), &localPawnHealthBytesRead);
                if (ofs.C_BaseEntity_m_lifeState > 0)
                    mem.AddScatterReadRequest(handle, localPawn + ofs.C_BaseEntity_m_lifeState, &localPawnLifeState, sizeof(localPawnLifeState), &localPawnLifeStateBytesRead);
                if (ofs.C_BasePlayerPawn_m_vOldOrigin > 0)
                    mem.AddScatterReadRequest(handle, localPawn + ofs.C_BasePlayerPawn_m_vOldOrigin, &localPos, sizeof(localPos), &localPosBytesRead);
                if (ofs.C_BaseModelEntity_m_vecViewOffset > 0)
                    mem.AddScatterReadRequest(handle, localPawn + ofs.C_BaseModelEntity_m_vecViewOffset, &localViewOffset, sizeof(localViewOffset), &localViewOffsetBytesRead);
                if (ofs.C_BasePlayerPawn_m_flFOVSensitivityAdjust > 0)
                    mem.AddScatterReadRequest(handle, localPawn + ofs.C_BasePlayerPawn_m_flFOVSensitivityAdjust, &localFovSensitivityAdjust, sizeof(localFovSensitivityAdjust), &localFovSensitivityBytesRead);
                if (ofs.C_CSPlayerPawnBase_m_iIDEntIndex > 0)
                    mem.AddScatterReadRequest(handle, localPawn + ofs.C_CSPlayerPawnBase_m_iIDEntIndex, &localCrosshairEntityIndex, sizeof(localCrosshairEntityIndex), &localCrosshairEntityIndexBytesRead);
                if (wantsTargetRecoil && ofs.C_CSPlayerPawn_m_pAimPunchServices > 0)
                    mem.AddScatterReadRequest(handle, localPawn + ofs.C_CSPlayerPawn_m_pAimPunchServices, &rawLocalAimPunchServices, sizeof(rawLocalAimPunchServices), &aimPunchServicesBytesRead);
                if (wantsTargetRecoil && ofs.C_CSPlayerPawn_m_iShotsFired > 0)
                    mem.AddScatterReadRequest(handle, localPawn + ofs.C_CSPlayerPawn_m_iShotsFired, &localShotsFired, sizeof(localShotsFired), &localShotsFiredBytesRead);
            }
            if (gameRules) {
                gameRulesReadSource = gameRules;
                if (minimapRefreshForced && ofs.C_CSGameRules_m_vMinimapMins > 0)
                    mem.AddScatterReadRequest(handle, gameRules + ofs.C_CSGameRules_m_vMinimapMins, &minimapMins, sizeof(minimapMins), &minimapMinsBytesRead);
                if (minimapRefreshForced && ofs.C_CSGameRules_m_vMinimapMaxs > 0)
                    mem.AddScatterReadRequest(handle, gameRules + ofs.C_CSGameRules_m_vMinimapMaxs, &minimapMaxs, sizeof(minimapMaxs), &minimapMaxsBytesRead);
                if (ofs.C_CSGameRules_m_bBombPlanted > 0)
                    mem.AddScatterReadRequest(handle, gameRules + ofs.C_CSGameRules_m_bBombPlanted, &bombPlantedFlag, sizeof(bombPlantedFlag), &bombPlantedFlagBytesRead);
                if (ofs.C_CSGameRules_m_bBombDropped > 0)
                    mem.AddScatterReadRequest(handle, gameRules + ofs.C_CSGameRules_m_bBombDropped, &bombDroppedFlag, sizeof(bombDroppedFlag), &bombDroppedFlagBytesRead);
                if (ofs.C_CSGameRules_m_nRoundStartCount > 0)
                    mem.AddScatterReadRequest(handle, gameRules + ofs.C_CSGameRules_m_nRoundStartCount, &gameRulesRoundStartCount, sizeof(gameRulesRoundStartCount), &gameRulesRoundStartCountBytesRead);
                if (ofs.C_CSGameRules_m_totalRoundsPlayed > 0)
                    mem.AddScatterReadRequest(handle, gameRules + ofs.C_CSGameRules_m_totalRoundsPlayed, &gameRulesTotalRoundsPlayed, sizeof(gameRulesTotalRoundsPlayed), &gameRulesTotalRoundsPlayedBytesRead);
            }
            if (sensPtr && sensitivityRefreshForced && ofs.dwSensitivity_sensitivity > 0)
                mem.AddScatterReadRequest(handle, sensPtr + ofs.dwSensitivity_sensitivity, &liveSensitivity, sizeof(liveSensitivity), &liveSensitivityBytesRead);

            if (globalVars) {
                for (size_t i = 0; i < std::size(kIntervalCandidates); ++i) {
                    if (intervalRefreshForced && kIntervalCandidates[i] > 0)
                        mem.AddScatterReadRequest(handle, globalVars + static_cast<uintptr_t>(kIntervalCandidates[i]), &intervalCandidateValues[i], sizeof(float), &intervalCandidateReadBytes[i]);
                }
                queueCurrentTimeCandidates(
                    globalVars,
                    gameTimeCandidateSweepDue ||
                        globalVars != s_cbpGlobalVars);
            }
            dependentScatterOk = executeOptionalScatterRead();
            if (!dependentScatterOk) {
                logUpdateDataIssue("scatter_2", "dependent_data_refresh_failed");
            }
        }

        const uintptr_t resolvedAimPunchServices =
            aimPunchServicesBytesRead == sizeof(rawLocalAimPunchServices)
                ? sanitizePointer(rawLocalAimPunchServices) : 0;
        if (!wantsTargetRecoil || !localPawn) {
            s_cachedAimPunchServices = 0;
        } else if (resolvedAimPunchServices) {
            s_cachedAimPunchServices = resolvedAimPunchServices;
        }
        localShotsFiredValid =
            localShotsFiredBytesRead == sizeof(localShotsFired) &&
            localShotsFired >= 0 && localShotsFired <= 100;
        localViewOffsetReadValid =
            localPawn != 0 &&
            localViewOffsetBytesRead == sizeof(localViewOffset);
        localPosReadValid =
            localPawn != 0 &&
            localPosBytesRead == sizeof(localPos) &&
            isValidWorldPos(localPos);
        localCrosshairReadValid =
            localPawn != 0 &&
            localCrosshairEntityIndexBytesRead ==
                sizeof(localCrosshairEntityIndex) &&
            localCrosshairEntityIndex >= -1 &&
            localCrosshairEntityIndex < 4096;
        const bool localAimPunchReadComplete =
            queuedAimPunchServices != 0 &&
            queuedAimPunchServices == resolvedAimPunchServices &&
            localAimPunchBytesRead == sizeof(localAimPunch) &&
            IsFiniteVec(localAimPunch) &&
            std::fabs(localAimPunch.x) <= 45.0f &&
            std::fabs(localAimPunch.y) <= 45.0f &&
            std::fabs(localAimPunch.z) <= 10.0f;
        localAimPunchValid =
            wantsTargetRecoil && localShotsFiredValid &&
            localAimPunchReadComplete;
        if (!localShotsFiredValid)
            localShotsFired = 0;
        if (!localAimPunchValid)
            localAimPunch = {};

        listEntry = rawListEntryBytesRead == sizeof(rawListEntry)
            ? sanitizePointer(rawListEntry) : 0;
        if (highestEntityBytesRead != sizeof(highestEntityIndex))
            highestEntityIndex = entityList == s_cachedEntityList
                ? std::max(0, s_highestEntityIdxStat.load(std::memory_order_relaxed)) : 0;
        if (localControllerPawnHandleBytesRead != sizeof(localControllerPawnHandle))
            localControllerPawnHandle = 0;
        if (localFovSensitivityBytesRead != sizeof(localFovSensitivityAdjust))
            localFovSensitivityAdjust = localPawn == s_cbpLocalPawn ? s_fovSensitivityAdjust : 1.0f;
        if (localPawn && liveLocalTeamBytesRead == sizeof(liveLocalTeam) && ofs.C_BaseEntity_m_iTeamNum > 0 && (liveLocalTeam == 1 || liveLocalTeam == 2 || liveLocalTeam == 3)) {
            localTeam = liveLocalTeam;
            localTeamLiveResolved = true;
        }
        localPawnCoreLiveResolved = esp::data::IsLocalVitalSampleComplete(
            localVitalSamplePawn, localPawn, localPawnHealth, localPawnLifeState,
            localPawnHealthBytesRead, localPawnLifeStateBytesRead);
        if (localController && localControllerTeamBytesRead == sizeof(localControllerTeam) && ofs.C_BaseEntity_m_iTeamNum > 0 && (localControllerTeam == 1 || localControllerTeam == 2 || localControllerTeam == 3)) {
            localTeam = localControllerTeam;
            localTeamLiveResolved = true;
        }
        const bool bombRuleFieldsReadComplete =
            esp::data::IsBombFieldReadComplete(
                ofs.C_CSGameRules_m_bBombPlanted > 0,
                bombPlantedFlagBytesRead,
                sizeof(bombPlantedFlag)) &&
            esp::data::IsBombFieldReadComplete(
                ofs.C_CSGameRules_m_bBombDropped > 0,
                bombDroppedFlagBytesRead,
                sizeof(bombDroppedFlag));
        const bool gameRulesReadOk =
            gameRules != 0 &&
            baseScatterOk &&
            (!pointersChanged || dependentScatterOk) &&
            bombRuleFieldsReadComplete;
        if (gameRulesReadOk) {
            if (ofs.C_CSGameRules_m_bBombPlanted > 0)
                bombPlantedByRules = bombPlantedFlag != 0;
            if (ofs.C_CSGameRules_m_bBombDropped > 0)
                bombDroppedByRules = bombDroppedFlag != 0;
        } else if (engineMatchLikeBase && gameRules) {
            bombPlantedByRules = s_cachedBombPlantedByRules;
            bombDroppedByRules = s_cachedBombDroppedByRules;
        }
        const bool minimapReadComplete =
            minimapMinsBytesRead == sizeof(minimapMins) &&
            minimapMaxsBytesRead == sizeof(minimapMaxs) && gameRulesReadSource == gameRules;
        if (!minimapReadComplete || !IsBoundsValid(minimapMins, minimapMaxs)) {
            minimapMins = gameRules == s_cbpGameRules ? s_minimapMins : Vector3{};
            minimapMaxs = gameRules == s_cbpGameRules ? s_minimapMaxs : Vector3{};
        } else {
            s_lastMinimapRefreshUs = baseNowUs;
        }
        minimapBoundsValid = IsBoundsValid(minimapMins, minimapMaxs);
        if (sensPtr && liveSensitivityBytesRead == sizeof(liveSensitivity) &&
            ofs.dwSensitivity_sensitivity > 0 &&
            std::isfinite(liveSensitivity) && liveSensitivity > 0.0f && liveSensitivity < 100.0f) {
            sensValue = liveSensitivity;
            s_lastSensitivityRefreshUs = baseNowUs;
        }
        if (globalVars) {
            if (fullGameTimeCandidateSweepQueued &&
                baseScatterOk &&
                (!pointersChanged || dependentScatterOk)) {
                s_lastGameTimeCandidateSweepUs = baseNowUs;
            }
            for (size_t i = 0; i < std::size(kIntervalCandidates); ++i) {
                if (!esp::data::IsBombFieldReadComplete(true, intervalCandidateReadBytes[i], sizeof(float)))
                    continue;
                const float v = intervalCandidateValues[i];
                if (std::isfinite(v) && v >= 0.001f && v <= 0.1f) {
                    intervalPerTick = v;
                    s_lastIntervalRefreshUs = baseNowUs;
                    break;
                }
            }
            for (size_t i = 0; i < std::size(currentTimeCandidateValues); ++i) {
                if (!esp::data::IsBombFieldReadComplete(true, currentTimeCandidateReadBytes[i], sizeof(float)))
                    currentTimeCandidateValues[i] = 0.0f;
            }
            const auto gameTimeSelection =
                esp::data::SelectStableGameTimeCandidate(
                    currentTimeCandidateValues,
                    std::size(currentTimeCandidateValues),
                    s_lastStableGameTime,
                    s_lastStableGameTimeUs,
                    baseNowUs,
                    s_currentTimeCandidateIndex);
            currentGameTime = gameTimeSelection.value;
            currentGameTimeFresh = gameTimeSelection.acceptedRaw;
            if (gameTimeSelection.acceptedRaw) {
                s_currentTimeCandidateIndex = gameTimeSelection.candidateIndex;
                const bool firstStableGameTime =
                    !std::isfinite(s_lastStableGameTime) ||
                    s_lastStableGameTime < 1.0f ||
                    s_lastStableGameTimeUs == 0;
                const bool gameTimeAdvanced =
                    currentGameTime >
                    s_lastStableGameTime + 0.0001f;
                if (firstStableGameTime || gameTimeAdvanced) {
                    s_lastStableGameTime = currentGameTime;
                    s_lastStableGameTimeUs = baseNowUs;
                } else if (currentGameTime > s_lastStableGameTime) {
                    s_lastStableGameTime = currentGameTime;
                }
            }
        }
        if (!engineMatchLikeBase) {
            gameRules = 0;
            plantedC4Entity = 0;
            weaponC4Entity = 0;
            minimapMins = {};
            minimapMaxs = {};
            minimapBoundsValid = false;
            bombPlantedByRules = false;
            bombDroppedByRules = false;
            s_pendingGameRulesCandidate = 0;
            s_pendingGameRulesConfirmCount = 0;
            s_cachedBombPlantedByRules = false;
            s_cachedBombDroppedByRules = false;
            s_bombPlantedRulesFalseStreak = 0;
            s_bombDroppedRulesFalseStreak = 0;
        }

        const auto gameRulesWarmupState =
            static_cast<esp::SceneWarmupState>(s_sceneWarmupState.load(std::memory_order_relaxed));
        const bool stableInGameGameRulesWindow =
            s_engineInGame.load(std::memory_order_relaxed) &&
            gameRules != 0 &&
            gameRulesWarmupState == esp::SceneWarmupState::Stable &&
            (baseNowUs - s_lastSceneResetUs.load(std::memory_order_relaxed)) >= 2000000;
        const bool gameRulesCoreSane = esp::data::IsGameRulesCoreSane(
            gameRulesReadOk,
            bombPlantedFlag,
            bombDroppedFlag);
        const bool newGameRulesCandidate =
            stableInGameGameRulesWindow &&
            gameRules != 0 &&
            gameRulesCoreSane &&
            gameRules != s_cbpGameRules;

        if (newGameRulesCandidate) {
            if (s_pendingGameRulesCandidate != gameRules) {
                s_pendingGameRulesCandidate = gameRules;
                s_pendingGameRulesConfirmCount = 1;
            } else if (s_pendingGameRulesConfirmCount < 0xFFFFFFFFu) {
                ++s_pendingGameRulesConfirmCount;
            }

            if (s_pendingGameRulesConfirmCount < 2u) {
                gameRules = s_cbpGameRules;
                minimapMins = s_minimapMins;
                minimapMaxs = s_minimapMaxs;
                minimapBoundsValid = s_hasMinimapBounds;
                bombPlantedByRules = s_cachedBombPlantedByRules;
                bombDroppedByRules = s_cachedBombDroppedByRules;
            } else {
                s_pendingGameRulesCandidate = 0;
                s_pendingGameRulesConfirmCount = 0;
            }
        } else if (!gameRulesCoreSane || gameRules == 0 || gameRules == s_cbpGameRules) {
            s_pendingGameRulesCandidate = 0;
            s_pendingGameRulesConfirmCount = 0;
        }

        if (stableInGameGameRulesWindow && !gameRulesCoreSane) {
            ++s_gameRulesSanityFailureStreak;
            if (s_gameRulesSanityFailureSinceUs == 0)
                s_gameRulesSanityFailureSinceUs = baseNowUs;

            const bool shouldLog =
                esp::data::ShouldLogGameRulesSanityFailure(
                    s_gameRulesSanityFailureStreak,
                    s_lastGameRulesSanityLogUs,
                    baseNowUs);
            if (shouldLog) {
                s_lastGameRulesSanityLogUs = baseNowUs;
                DmaLogPrintf(
                    "[WARN] GameRules core validation failed: ptr=0x%llX dwGameRules=0x%llX read_ok=%u flags_read=%u planted=%u dropped=%u.",
                    static_cast<unsigned long long>(gameRules),
                    static_cast<unsigned long long>(ofs.dwGameRules),
                    static_cast<unsigned>(gameRulesReadOk),
                    static_cast<unsigned>(bombRuleFieldsReadComplete),
                    static_cast<unsigned>(bombPlantedFlag),
                    static_cast<unsigned>(bombDroppedFlag));
            }

            // GameRules only feeds minimap/C4 metadata. Never pause the shared
            // DMA/camera/player pipeline because these optional flags are
            // unavailable; Target and ESP player tracking must keep running.

            const bool canFallbackToCachedGameRules =
                s_cbpGameRules != 0 &&
                s_cbpGameRules != gameRules;
            gameRules = canFallbackToCachedGameRules ? s_cbpGameRules : 0;
            minimapMins = canFallbackToCachedGameRules ? s_minimapMins : Vector3{};
            minimapMaxs = canFallbackToCachedGameRules ? s_minimapMaxs : Vector3{};
            minimapBoundsValid = canFallbackToCachedGameRules ? s_hasMinimapBounds : false;
            bombPlantedByRules =
                canFallbackToCachedGameRules ? s_cachedBombPlantedByRules : false;
            bombDroppedByRules =
                canFallbackToCachedGameRules ? s_cachedBombDroppedByRules : false;
        } else if (!stableInGameGameRulesWindow || gameRulesCoreSane) {
            s_gameRulesSanityFailureStreak = 0;
            s_gameRulesSanityFailureSinceUs = 0;
        }

        if (!engineMatchLikeBase ||
            !gameRules ||
            gameRules != s_bombRoundCounterGameRules) {
            s_bombRoundCounterGameRules = gameRules;
            s_bombRoundCounterTracker.Reset();
        }
        const bool hasAuthoritativeRoundCounter =
            ofs.C_CSGameRules_m_nRoundStartCount > 0 ||
            ofs.C_CSGameRules_m_totalRoundsPlayed > 0;
        const bool authoritativeRoundCounterReadComplete =
            ofs.C_CSGameRules_m_nRoundStartCount > 0
                ? gameRulesRoundStartCountBytesRead ==
                    sizeof(gameRulesRoundStartCount)
                : (ofs.C_CSGameRules_m_totalRoundsPlayed > 0 &&
                   gameRulesTotalRoundsPlayedBytesRead ==
                    sizeof(gameRulesTotalRoundsPlayed));
        const bool authoritativeRoundCounterFresh =
            hasAuthoritativeRoundCounter &&
            gameRulesReadOk &&
            gameRulesCoreSane &&
            gameRules != 0 &&
            gameRulesReadSource == gameRules &&
            authoritativeRoundCounterReadComplete;
        if (authoritativeRoundCounterFresh) {
            const uint32_t observedRoundCounter =
                ofs.C_CSGameRules_m_nRoundStartCount > 0
                    ? gameRulesRoundStartCount
                    : gameRulesTotalRoundsPlayed;
            bombRoundChangedThisTick =
                s_bombRoundCounterTracker.Observe(observedRoundCounter);
        } else {
            s_bombRoundCounterTracker.DiscardPending();
        }

        if (gameRulesReadOk && gameRules && gameRulesCoreSane) {
            const auto plantedSignal =
                esp::data::SelectStableBombRuleSignal(
                    bombPlantedByRules,
                    s_cachedBombPlantedByRules,
                    s_bombPlantedRulesFalseStreak);
            const auto droppedSignal =
                esp::data::SelectStableBombRuleSignal(
                    bombDroppedByRules,
                    s_cachedBombDroppedByRules,
                    s_bombDroppedRulesFalseStreak);
            bombPlantedByRules = plantedSignal.value;
            bombDroppedByRules = droppedSignal.value;
            s_cachedBombPlantedByRules = plantedSignal.value;
            s_cachedBombDroppedByRules = droppedSignal.value;
            s_bombPlantedRulesFalseStreak = plantedSignal.falseStreak;
            s_bombDroppedRulesFalseStreak = droppedSignal.falseStreak;
        }

        if (!engineMatchLikeBase) {
            SetSubsystemUnknown(RuntimeSubsystem::GameRulesMap);
        } else if (!gameRulesCoreSane || !gameRules) {
            if (stableInGameGameRulesWindow)
                MarkSubsystemFailed(RuntimeSubsystem::GameRulesMap, baseNowUs);
            else
                MarkSubsystemDegraded(RuntimeSubsystem::GameRulesMap, baseNowUs);
        } else if (gameRulesReadOk) {
            MarkSubsystemHealthy(RuntimeSubsystem::GameRulesMap, baseNowUs);
        } else {
            MarkSubsystemDegraded(RuntimeSubsystem::GameRulesMap, baseNowUs);
        }

        
        s_cbpLocalPawn = localPawn;
        s_cbpLocalController = localController;
        if (!engineMatchLikeBase || (gameRules && gameRulesCoreSane))
            s_cbpGameRules = gameRules;
        if (globalVars || !engineMatchLikeBase)
            s_cbpGlobalVars = globalVars;
        if (sensPtr || !engineMatchLikeBase)
            s_cbpSensPtr = sensPtr;
        if (plantedC4Entity)
            s_cbpPlantedC4 = plantedC4Entity;
        else if (!bombPlantedByRules)
            s_cbpPlantedC4 = 0;

        if (bombPlantedByRules) {
            if (!plantedC4Entity)
                plantedC4Entity = s_cbpPlantedC4;
            weaponC4Entity = 0;
        }
        if (entityList && highestEntityBytesRead == sizeof(highestEntityIndex) && highestEntityIndex > 0)
            s_lastHighestEntityRefreshUs = baseNowUs;
    }

    
    
    
    
    
    if (!entityList && g::clientBase && ofs.dwEntityList > 0) {
        readPointer(g::clientBase + ofs.dwEntityList, &entityList);
    }
    if (entityList && !listEntry) {
        for (int attempt = 0; attempt < 3 && !listEntry; ++attempt) {
            readPointer(entityList + 0x10, &listEntry);
        }
    }



    
    
    
    
    
    bool acceptEntityListCacheUpdate = entityList != 0;
    if (entityList && s_cachedEntityList &&
        entityList != s_cachedEntityList) {
        const uint64_t entityListCandidateNowUs = TickNowUs();
        const auto entityListWarmupState =
            static_cast<esp::SceneWarmupState>(s_sceneWarmupState.load(std::memory_order_relaxed));
        const bool liveEntityHierarchyContext =
            s_engineInGame.load(std::memory_order_relaxed) &&
            !s_engineMenu.load(std::memory_order_relaxed) &&
            s_engineSignOnState.load(std::memory_order_relaxed) == 6;
        const bool stableInGameEntityList =
            liveEntityHierarchyContext &&
            entityListWarmupState == esp::SceneWarmupState::Stable;

        if (!liveEntityHierarchyContext) {
            s_pendingEntityList = 0;
            s_pendingEntityListConfirmCount = 0;
            s_pendingEntityListFirstSeenUs = 0;
            acceptEntityListCacheUpdate = true;
        } else {
            acceptEntityListCacheUpdate = false;
            if (s_pendingEntityList != entityList) {
                s_pendingEntityList = entityList;
                s_pendingEntityListConfirmCount = 1;
                s_pendingEntityListFirstSeenUs = entityListCandidateNowUs;
            } else if (s_pendingEntityListConfirmCount < 0xFFFFFFFFu) {
                ++s_pendingEntityListConfirmCount;
            }

            if (esp::data::ShouldAcceptEntityListCandidate(
                    stableInGameEntityList,
                    s_pendingEntityListConfirmCount,
                    s_pendingEntityListFirstSeenUs,
                    entityListCandidateNowUs)) {
                uintptr_t directlyConfirmedEntityList = 0;
                const bool directConfirmationOk =
                    g::clientBase &&
                    ofs.dwEntityList > 0 &&
                    readPointer(
                        g::clientBase + ofs.dwEntityList,
                        &directlyConfirmedEntityList) &&
                    directlyConfirmedEntityList == entityList;
                if (directConfirmationOk) {
                    acceptEntityListCacheUpdate = true;
                    s_pendingEntityList = 0;
                    s_pendingEntityListConfirmCount = 0;
                    s_pendingEntityListFirstSeenUs = 0;
                } else if (directlyConfirmedEntityList != 0 &&
                           directlyConfirmedEntityList != entityList) {
                    s_pendingEntityList = directlyConfirmedEntityList;
                    s_pendingEntityListConfirmCount = 1;
                    s_pendingEntityListFirstSeenUs = entityListCandidateNowUs;
                }
            }
        }
    } else {
        s_pendingEntityList = 0;
        s_pendingEntityListConfirmCount = 0;
        s_pendingEntityListFirstSeenUs = 0;
    }

    if (!acceptEntityListCacheUpdate) {
        entityList = s_cachedEntityList;
        listEntry = s_cachedListEntry;
        highestEntityIndex = std::max(0, s_highestEntityIdxStat.load(std::memory_order_relaxed));
    }

    if (acceptEntityListCacheUpdate && entityList)
        s_cachedEntityList = entityList;
    if (listEntry)
        s_cachedListEntry = listEntry;

    if (!entityList)
        highestEntityIndex = 0;
    if (!listEntry)
        highestEntityIndex = 0;

    
    
    const uint64_t baseReadsSceneAge = TickNowUs() - s_lastSceneResetUs.load(std::memory_order_relaxed);
    const auto baseWarmupState =
        static_cast<esp::SceneWarmupState>(s_sceneWarmupState.load(std::memory_order_relaxed));
    const bool sceneSettling =
        esp::data::IsBaseSceneSettling(
            baseReadsSceneAge,
            baseWarmupState == esp::SceneWarmupState::Stable ||
                baseWarmupState == esp::SceneWarmupState::Recovery);
    {
        const bool entityHierarchyReady = entityList != 0 && listEntry != 0;
        const bool liveEntityHierarchyContext =
            s_engineInGame.load(std::memory_order_relaxed) &&
            !s_engineMenu.load(std::memory_order_relaxed) &&
            s_engineSignOnState.load(std::memory_order_relaxed) == 6;
        if (!entityHierarchyReady) {
            if (!liveEntityHierarchyContext) {
                resetEntityHierarchyRecovery();
            } else if (baseWarmupState == esp::SceneWarmupState::Recovery ||
                       s_dmaRecovering.load(std::memory_order_relaxed)) {
                if (!s_entityHierarchyMissing) {
                    s_entityHierarchyMissing = true;
                    s_entityHierarchyMissingSinceUs = TickNowUs();
                }
            } else {
                const uint64_t nowUs = TickNowUs();
                if (!s_entityHierarchyMissing) {
                    s_entityHierarchyMissing = true;
                    s_entityHierarchyMissingSinceUs = nowUs;
                    s_entityHierarchyMissingStreak = 1;
                    s_entityHierarchyRecoveryStage =
                        esp::data::EntityHierarchyRecoveryAction::None;
                    s_lastEntityHierarchyRecoveryUs = 0;
                } else if (s_entityHierarchyMissingStreak < 0xFFFFFFFFu) {
                    ++s_entityHierarchyMissingStreak;
                }

                const auto recoveryAction =
                    esp::data::SelectMissingEntityHierarchyRecovery(
                        s_entityHierarchyMissingStreak,
                        s_entityHierarchyMissingSinceUs,
                        s_entityHierarchyRecoveryStage,
                        s_lastEntityHierarchyRecoveryUs,
                        nowUs);
                if (recoveryAction != esp::data::EntityHierarchyRecoveryAction::None) {
                    s_lastEntityHierarchyRecoveryUs = nowUs;
                    s_entityHierarchyRecoveryStage = recoveryAction;
                    const bool full =
                        recoveryAction == esp::data::EntityHierarchyRecoveryAction::Full ||
                        recoveryAction == esp::data::EntityHierarchyRecoveryAction::ForcedFull;
                    const bool repair =
                        recoveryAction == esp::data::EntityHierarchyRecoveryAction::Repair;
                    const char* reason =
                        recoveryAction == esp::data::EntityHierarchyRecoveryAction::ForcedFull
                            ? "entity_hierarchy_missing_forced_full"
                            : recoveryAction == esp::data::EntityHierarchyRecoveryAction::Full
                                ? "entity_hierarchy_missing_full"
                                : recoveryAction == esp::data::EntityHierarchyRecoveryAction::Repair
                                    ? "entity_hierarchy_missing_repair"
                                    : "entity_hierarchy_missing_probe";
                    refreshDmaCaches(
                        reason,
                        full ? DmaRefreshTier::Full :
                        repair ? DmaRefreshTier::Repair :
                        DmaRefreshTier::Probe,
                        false);
                }
            }
        } else {
            resetMissingHierarchyRecovery();
            const bool flatLiveEntityHierarchy =
                esp::data::IsFlatLiveEntityHierarchy(
                    liveEntityHierarchyContext,
                    baseReadsSceneAge,
                    s_playerSlotScanLimitStat.load(std::memory_order_relaxed),
                    s_activePlayerCount.load(std::memory_order_relaxed),
                    highestEntityIndex);
            if (flatLiveEntityHierarchy &&
                baseWarmupState != esp::SceneWarmupState::Recovery &&
                !s_dmaRecovering.load(std::memory_order_relaxed)) {
                ++s_entityHierarchyFlatStreak;
                const uint64_t nowUs = TickNowUs();
                if (s_entityHierarchyFlatSinceUs == 0)
                    s_entityHierarchyFlatSinceUs = nowUs;
                const auto flatRecoveryAction =
                    esp::data::SelectFlatEntityHierarchyRecovery(
                        s_entityHierarchyFlatStreak,
                        s_entityHierarchyFlatSinceUs,
                        s_entityHierarchyFlatRecoveryStage,
                        s_lastEntityHierarchyFlatRecoveryUs,
                        nowUs);
                if (flatRecoveryAction != esp::data::EntityHierarchyRecoveryAction::None) {
                    s_lastEntityHierarchyFlatRecoveryUs = nowUs;
                    s_entityHierarchyFlatRecoveryStage = flatRecoveryAction;
                    const bool repairFlat =
                        flatRecoveryAction == esp::data::EntityHierarchyRecoveryAction::Repair ||
                        flatRecoveryAction == esp::data::EntityHierarchyRecoveryAction::Full;
                    const bool fullFlat = flatRecoveryAction == esp::data::EntityHierarchyRecoveryAction::Full;
                    if (repairFlat)
                        SetSceneWarmupState(esp::SceneWarmupState::HierarchyWarming, nowUs);
                    refreshDmaCaches(
                        fullFlat ? "entity_hierarchy_flat_full" :
                        repairFlat ? "entity_hierarchy_flat_repair" :
                        "entity_hierarchy_flat_probe",
                        fullFlat ? DmaRefreshTier::Full :
                        repairFlat ? DmaRefreshTier::Repair :
                        DmaRefreshTier::Probe,
                        false);
                }
            } else {
                resetFlatHierarchyRecovery();
            }
        }
    }

    

    const bool canReadEntityHierarchy = entityList != 0 && listEntry != 0;
