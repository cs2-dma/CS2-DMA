    const uint64_t coreNowUs = TickNowUs();
    constexpr uint64_t kCoreTeamRefreshIntervalUs = 100000;
    constexpr uint64_t kCoreArmorRefreshIntervalUs = 16667;
    constexpr uint64_t kPlayerFlagRefreshIntervalUs = 8333;
    constexpr uint64_t kPlayerEyeAnglesRefreshIntervalUs = 8333;
    static uint64_t s_lastPlayerFlagReadUs = 0;
    static uint64_t s_lastPlayerEyeAnglesReadUs = 0;
    static uint64_t s_dynamicCadenceResetSerial = 0;
    const uint64_t dynamicCadenceResetSerial =
        s_sceneResetSerial.load(std::memory_order_relaxed);
    if (s_dynamicCadenceResetSerial != dynamicCadenceResetSerial) {
        s_dynamicCadenceResetSerial = dynamicCadenceResetSerial;
        s_lastPlayerFlagReadUs = 0;
        s_lastPlayerEyeAnglesReadUs = 0;
    }
    auto dynamicCadenceDue = [&](bool requested, uint64_t lastReadUs, uint64_t intervalUs) {
        return requested &&
               (lastReadUs == 0u ||
                coreNowUs < lastReadUs ||
                (coreNowUs - lastReadUs) >= intervalUs);
    };
    const bool readPlayerFlagsThisTick = dynamicCadenceDue(
        wantsFastScopedFlag || wantsFastDefusingFlag || wantsFastBlindFlag,
        s_lastPlayerFlagReadUs,
        kPlayerFlagRefreshIntervalUs);
    const bool readEyeAnglesThisTick = dynamicCadenceDue(
        wantsFastEyeAngles,
        s_lastPlayerEyeAnglesReadUs,
        kPlayerEyeAnglesRefreshIntervalUs);
    const bool previousLocalTeamValid = (s_localTeam == 2 || s_localTeam == 3);
    int currentResolvedLocalTeam = 0;
    if (localTeamLiveResolved && (localTeam == 2 || localTeam == 3)) {
        currentResolvedLocalTeam = localTeam;
    } else if (localControllerTeam == 2 || localControllerTeam == 3) {
        currentResolvedLocalTeam = localControllerTeam;
    }
    const bool currentLocalTeamValid = (currentResolvedLocalTeam == 2 || currentResolvedLocalTeam == 3);
    const bool localTeamSwitchLiveEvidence =
        localTeamLiveResolved ||
        (localControllerTeam == 2 || localControllerTeam == 3);
    static int s_pendingLocalTeamFrom = 0;
    static int s_pendingLocalTeamTo = 0;
    static uint32_t s_pendingLocalTeamSwitchCount = 0;
    static uint64_t s_pendingLocalTeamSwitchSinceUs = 0;
    static uint64_t s_lastHandledLocalTeamSwitchUs = 0;
    static int s_lastHandledLocalTeamFrom = 0;
    static int s_lastHandledLocalTeamTo = 0;
    static uint64_t s_localTeamSwitchResetSerial = 0;
    const uint64_t localTeamSwitchResetSerial =
        s_sceneResetSerial.load(std::memory_order_relaxed);
    if (s_localTeamSwitchResetSerial != localTeamSwitchResetSerial) {
        s_localTeamSwitchResetSerial = localTeamSwitchResetSerial;
        s_pendingLocalTeamFrom = 0;
        s_pendingLocalTeamTo = 0;
        s_pendingLocalTeamSwitchCount = 0;
        s_pendingLocalTeamSwitchSinceUs = 0;
        s_lastHandledLocalTeamSwitchUs = 0;
        s_lastHandledLocalTeamFrom = 0;
        s_lastHandledLocalTeamTo = 0;
    }
    const bool localTeamSwitchSignal =
        previousLocalTeamValid &&
        localTeamSwitchLiveEvidence &&
        currentLocalTeamValid &&
        currentResolvedLocalTeam != s_localTeam;
    bool localTeamLikelySwitched = false;
    if (localTeamSwitchSignal) {
        const bool samePendingSwitch =
            esp::data::IsSamePendingLocalTeamSwitch(
                s_pendingLocalTeamFrom,
                s_localTeam,
                s_pendingLocalTeamTo,
                currentResolvedLocalTeam,
                s_pendingLocalTeamSwitchSinceUs,
                coreNowUs);
        if (!samePendingSwitch) {
            s_pendingLocalTeamFrom = s_localTeam;
            s_pendingLocalTeamTo = currentResolvedLocalTeam;
            s_pendingLocalTeamSwitchCount = 1;
            s_pendingLocalTeamSwitchSinceUs = coreNowUs;
        } else if (s_pendingLocalTeamSwitchCount < 0xFFFFFFFFu) {
            ++s_pendingLocalTeamSwitchCount;
        }
        localTeamLikelySwitched = s_pendingLocalTeamSwitchCount >= 3u;
    } else {
        s_pendingLocalTeamFrom = 0;
        s_pendingLocalTeamTo = 0;
        s_pendingLocalTeamSwitchCount = 0;
        s_pendingLocalTeamSwitchSinceUs = 0;
    }
    if (localTeamLikelySwitched) {
        const bool newTeamSwitchEdge =
            esp::data::IsNewLocalTeamSwitchEdge(
                s_lastHandledLocalTeamSwitchUs,
                s_lastHandledLocalTeamFrom,
                s_localTeam,
                s_lastHandledLocalTeamTo,
                currentResolvedLocalTeam,
                coreNowUs);
        if (newTeamSwitchEdge) {
            s_lastHandledLocalTeamSwitchUs = coreNowUs;
            s_lastHandledLocalTeamFrom = s_localTeam;
            s_lastHandledLocalTeamTo = currentResolvedLocalTeam;
            BumpSceneReset(coreNowUs);
            setSceneWarmupState(esp::SceneWarmupState::SceneTransition);
            memset(s_playerInvalidReadStreak, 0, sizeof(s_playerInvalidReadStreak));
            memset(s_playerDeathConfirmCount, 0, sizeof(s_playerDeathConfirmCount));
            DmaLogPrintf(
                "[INFO] Local team switch detected (%d -> %d), rewarming player caches",
                s_localTeam,
                currentResolvedLocalTeam);
        }
        s_pendingLocalTeamFrom = 0;
        s_pendingLocalTeamTo = 0;
        s_pendingLocalTeamSwitchCount = 0;
        s_pendingLocalTeamSwitchSinceUs = 0;
        memset(s_cachedCoreTeams, 0, sizeof(s_cachedCoreTeams));
        memset(s_cachedCoreTeamPawns, 0, sizeof(s_cachedCoreTeamPawns));
        memset(s_lastCoreTeamReadUs, 0, sizeof(s_lastCoreTeamReadUs));
        memset(teams, 0, sizeof(teams));
    }
    auto coreTeamRefreshDueAt = [&](uint64_t nowAtUs, int idx) -> bool {
        if (idx < 0 || idx >= 64 || !pawns[idx])
            return false;
        if (s_cachedCoreTeamPawns[idx] != pawns[idx])
            return true;
        if (!teamLooksValid(teams[idx]))
            return true;
        return s_lastCoreTeamReadUs[idx] == 0 ||
               (nowAtUs - s_lastCoreTeamReadUs[idx]) >= kCoreTeamRefreshIntervalUs;
    };
    auto lifeStateLooksValid = [](uint8_t lifeStateValue) -> bool {
        return lifeStateValue <= 2;
    };
    auto coreValuesLookPlausible = [&](int teamValue,
                                       int healthValue,
                                       int armorValue,
                                       uint8_t lifeStateValue,
                                       const Vector3& positionValue) -> bool {
        return lifeStateLooksValid(lifeStateValue) &&
               esp::data::IsPlayerCoreStatePlausible(
                   teamLooksValid(teamValue),
                   healthValue,
                   armorValue,
                   lifeStateValue,
                   isValidWorldPos(positionValue));
    };
    auto coreArmorRefreshDueAt = [&](uint64_t nowAtUs, int idx) -> bool {
        if (idx < 0 || idx >= 64 || !pawns[idx])
            return false;
        if (s_cachedCoreArmorPawns[idx] != pawns[idx])
            return true;
        if (armors[idx] < 0 || armors[idx] > 500)
            return true;
        return s_lastCoreArmorReadUs[idx] == 0u ||
               nowAtUs < s_lastCoreArmorReadUs[idx] ||
               (nowAtUs - s_lastCoreArmorReadUs[idx]) >=
                   kCoreArmorRefreshIntervalUs;
    };
    auto coreStateLooksPlausible = [&](int idx) -> bool {
        if (idx < 0 || idx >= 64 || !pawns[idx])
            return false;
        return coreValuesLookPlausible(teams[idx], healths[idx], armors[idx], lifeStates[idx], positions[idx]);
    };
    auto coreStateLooksSane = [&](int idx) -> bool {
        return coreStateLooksPlausible(idx);
    };
    auto& coreTeamReadsQueued = s_playerReadScratch.coreTeamReadsQueued;
    auto coreReadCompleted = [&](int idx) -> bool {
        if (idx < 0 || idx >= 64)
            return false;
        return esp::data::IsPlayerCoreReadComplete({
            coreHealthBytesRead[idx],
            coreArmorBytesRead[idx],
            coreLifeStateBytesRead[idx],
            corePositionBytesRead[idx],
            coreTeamReadsQueued[idx],
            coreTeamBytesRead[idx],
        });
    };
    auto markCoreReadResult = [&](int idx) {
        if (idx < 0 || idx >= 64)
            return;
        const bool plausible =
            coreReadCompleted(idx) &&
            coreStateLooksPlausible(idx);
        coreReadPlausible[idx] = plausible;
        coreReadFresh[idx] = plausible;
        coreReadAlive[idx] = plausible && healths[idx] > 0 && lifeStates[idx] == 0;
    };
    bool playersCoreHadFailure = false;
    bool playersCoreHardFailure = false;
    constexpr size_t kCoreVitalReadBlockCapacity = 64u;
    const esp::data::CoreVitalReadLayout coreVitalReadLayout =
        esp::data::ResolveCoreVitalReadLayout(
            ofs.C_BaseEntity_m_iHealth,
            ofs.C_BaseEntity_m_lifeState,
            kCoreVitalReadBlockCapacity);
    static thread_local uint8_t
        s_coreVitalReadBlocks[64][kCoreVitalReadBlockCapacity] = {};
    DWORD coreVitalBlockBytesRead[64] = {};
    bool coreVitalReadsQueued[64] = {};

    auto unpackCoreVitalRead = [&](int idx) {
        if (idx < 0 || idx >= 64 || !coreVitalReadsQueued[idx] ||
            !esp::data::DecodeCoreVitalRead(coreVitalReadLayout,
                s_coreVitalReadBlocks[idx], kCoreVitalReadBlockCapacity,
                coreVitalBlockBytesRead[idx], healths[idx], lifeStates[idx])) {
            return;
        }
        coreHealthBytesRead[idx] = sizeof(healths[idx]);
        coreLifeStateBytesRead[idx] = sizeof(lifeStates[idx]);
    };

    bool queuedCorePlayerReads = false;
    esp::data::PlayerTeamSample coreTeamSamples[64] = {};
    auto markLiveTeamRead = [&](int idx) {
        if (idx < 0 || idx >= 64 || !pawns[idx])
            return;
        if (coreTeamBytesRead[idx] == sizeof(coreTeamSamples[idx])) {
            teams[idx] = coreTeamSamples[idx];
            liveTeamReads[idx] = teamLooksValid(teams[idx]);
            if (liveTeamReads[idx]) {
                s_cachedCoreTeams[idx] = teams[idx];
                s_cachedCoreTeamPawns[idx] = pawns[idx];
                s_lastCoreTeamReadUs[idx] = coreNowUs;
            }
        }
    };
    auto cacheCoreAuxState = [&](int idx) {
        s_cachedEyeAnglesPerPlayer[idx] = eyeAnglesPerPlayer[idx];
        if (spottedReadFresh[idx]) {
            s_cachedSpottedMasks[idx][0] = spottedMasks[idx][0];
            s_cachedSpottedMasks[idx][1] = spottedMasks[idx][1];
        }
        s_cachedDynamicPawns[idx] = pawns[idx];
    };
    auto spottedReadCompleted = [&](int idx) {
        return esp::data::IsSpottedStateReadComplete(
            wantsFastSpottedState && hasSpottedStateOffsets,
            spottedMaskBytesRead[idx],
            sizeof(spottedMasks[idx]));
    };
    auto markPlayerFlagFreshness = [&](int idx) {
        gunGameImmunityReadFresh[idx] =
            esp::data::IsBinaryPlayerFlagReadComplete(
                wantsGunGameImmunity,
                gunGameImmunityBytesRead[idx],
                gunGameImmunityFlags[idx]);
        scopedReadFresh[idx] =
            esp::data::IsBinaryPlayerFlagReadComplete(
                readPlayerFlagsThisTick && wantsFastScopedFlag &&
                    ofs.C_CSPlayerPawn_m_bIsScoped > 0,
                scopedFlagBytesRead[idx],
                scopedFlags[idx]);
        defusingReadFresh[idx] =
            esp::data::IsBinaryPlayerFlagReadComplete(
                readPlayerFlagsThisTick && wantsFastDefusingFlag &&
                    ofs.C_CSPlayerPawn_m_bIsDefusing > 0,
                defusingFlagBytesRead[idx],
                defusingFlags[idx]);
        flashReadFresh[idx] =
            esp::data::IsBlindFlashReadComplete(
                readPlayerFlagsThisTick && wantsFastBlindFlag &&
                    ofs.C_CSPlayerPawnBase_m_flFlashBangTime > 0 &&
                    ofs.C_CSPlayerPawnBase_m_flFlashDuration > 0,
                currentGameTimeFresh,
                flashBangTimeBytesRead[idx],
                flashDurationBytesRead[idx],
                flashBangTimes[idx],
                flashDurations[idx],
                currentGameTime);
    };
    auto queueMandatoryCoreReads = [&](int i, uint64_t readNowUs, bool includeVelocity) {
        coreTeamReadsQueued[i] = false;
        coreArmorReadsQueued[i] = false;
        coreTeamSamples[i] = 0;
        coreHealthBytesRead[i] = 0;
        coreArmorBytesRead[i] = 0;
        coreTeamBytesRead[i] = 0;
        coreLifeStateBytesRead[i] = 0;
        corePositionBytesRead[i] = 0;
        coreVitalBlockBytesRead[i] = 0;
        coreVitalReadsQueued[i] = false;
        gunGameImmunityBytesRead[i] = 0;
        gunGameImmunityReadFresh[i] = false;
        gunGameImmunityFlags[i] = esp::data::kInvalidPlayerFlagSample;
        if (includeVelocity)
            coreVelocityBytesRead[i] = 0;

        if (coreVitalReadLayout.coalesced) {
            std::memset(
                s_coreVitalReadBlocks[i],
                0,
                coreVitalReadLayout.spanBytes);
            mem.AddScatterReadRequest(
                handle,
                pawns[i] + coreVitalReadLayout.baseOffset,
                s_coreVitalReadBlocks[i],
                coreVitalReadLayout.spanBytes,
                &coreVitalBlockBytesRead[i]);
            coreVitalReadsQueued[i] = true;
        } else {
            mem.AddScatterReadRequest(
                handle,
                pawns[i] + ofs.C_BaseEntity_m_iHealth,
                &healths[i],
                sizeof(healths[i]),
                &coreHealthBytesRead[i]);
            mem.AddScatterReadRequest(
                handle,
                pawns[i] + ofs.C_BaseEntity_m_lifeState,
                &lifeStates[i],
                sizeof(lifeStates[i]),
                &coreLifeStateBytesRead[i]);
        }
        if (coreArmorRefreshDueAt(readNowUs, i)) {
            mem.AddScatterReadRequest(
                handle,
                pawns[i] + ofs.C_CSPlayerPawn_m_ArmorValue,
                &armors[i],
                sizeof(armors[i]),
                &coreArmorBytesRead[i]);
            coreArmorReadsQueued[i] = true;
        } else {
            coreArmorBytesRead[i] = sizeof(armors[i]);
        }
        if (coreTeamRefreshDueAt(readNowUs, i)) {
            mem.AddScatterReadRequest(
                handle,
                pawns[i] + ofs.C_BaseEntity_m_iTeamNum,
                &coreTeamSamples[i],
                sizeof(coreTeamSamples[i]),
                &coreTeamBytesRead[i]);
            coreTeamReadsQueued[i] = true;
        }
        mem.AddScatterReadRequest(
            handle,
            pawns[i] + ofs.C_BasePlayerPawn_m_vOldOrigin,
            &positions[i],
            sizeof(positions[i]),
            &corePositionBytesRead[i]);
        if (wantsGunGameImmunity) {
            mem.AddScatterReadRequest(
                handle,
                pawns[i] + ofs.C_CSPlayerPawn_m_bGunGameImmunity,
                &gunGameImmunityFlags[i],
                sizeof(gunGameImmunityFlags[i]),
                &gunGameImmunityBytesRead[i]);
        }
        if (includeVelocity && ofs.C_BaseEntity_m_vecVelocity > 0) {
            mem.AddScatterReadRequest(
                handle,
                pawns[i] + ofs.C_BaseEntity_m_vecVelocity,
                &velocities[i],
                sizeof(velocities[i]),
                &coreVelocityBytesRead[i]);
        }
    };
    auto queueCorePlayerReads = [&]() {
        for (int resolvedIdx = 0; resolvedIdx < playerResolvedSlotCount; ++resolvedIdx) {
            const int i = playerResolvedSlots[resolvedIdx];
            if (!pawns[i])
                continue;
            queuedCorePlayerReads = true;
            queueMandatoryCoreReads(i, coreNowUs, wantsTargetVelocity);

            scopedFlagBytesRead[i] = 0;
            defusingFlagBytesRead[i] = 0;
            flashBangTimeBytesRead[i] = 0;
            flashDurationBytesRead[i] = 0;
            spottedMaskBytesRead[i] = 0;

            if (readPlayerFlagsThisTick && wantsFastScopedFlag && ofs.C_CSPlayerPawn_m_bIsScoped > 0) {
                scopedFlags[i] = esp::data::kInvalidPlayerFlagSample;
                mem.AddScatterReadRequest(
                    handle,
                    pawns[i] + ofs.C_CSPlayerPawn_m_bIsScoped,
                    &scopedFlags[i],
                    sizeof(uint8_t),
                    &scopedFlagBytesRead[i]);
            }
            if (readPlayerFlagsThisTick && wantsFastDefusingFlag && ofs.C_CSPlayerPawn_m_bIsDefusing > 0) {
                defusingFlags[i] = esp::data::kInvalidPlayerFlagSample;
                mem.AddScatterReadRequest(
                    handle,
                    pawns[i] + ofs.C_CSPlayerPawn_m_bIsDefusing,
                    &defusingFlags[i],
                    sizeof(uint8_t),
                    &defusingFlagBytesRead[i]);
            }
            if (readPlayerFlagsThisTick && wantsFastBlindFlag &&
                ofs.C_CSPlayerPawnBase_m_flFlashBangTime > 0 &&
                ofs.C_CSPlayerPawnBase_m_flFlashDuration > 0) {
                flashBangTimes[i] = std::numeric_limits<float>::quiet_NaN();
                flashDurations[i] = std::numeric_limits<float>::quiet_NaN();
                mem.AddScatterReadRequest(
                    handle,
                    pawns[i] + ofs.C_CSPlayerPawnBase_m_flFlashBangTime,
                    &flashBangTimes[i],
                    sizeof(float),
                    &flashBangTimeBytesRead[i]);
                mem.AddScatterReadRequest(
                    handle,
                    pawns[i] + ofs.C_CSPlayerPawnBase_m_flFlashDuration,
                    &flashDurations[i],
                    sizeof(float),
                    &flashDurationBytesRead[i]);
            }
            if (readEyeAnglesThisTick && wantsFastEyeAngles && ofs.C_CSPlayerPawn_m_angEyeAngles > 0) {
                mem.AddScatterReadRequest(handle, pawns[i] + ofs.C_CSPlayerPawn_m_angEyeAngles, &eyeAnglesPerPlayer[i], sizeof(Vector3));
            }
            if (wantsFastSpottedState && hasSpottedStateOffsets) {
                spottedMaskBytesRead[i] = 0;
                mem.AddScatterReadRequest(
                    handle,
                    pawns[i] + ofs.C_CSPlayerPawn_m_entitySpottedState +
                        ofs.EntitySpottedState_t_m_bSpottedByMask,
                    reinterpret_cast<void*>(spottedMasks[i]),
                    sizeof(spottedMasks[i]),
                    &spottedMaskBytesRead[i]);
            }
        }
    };
    queueCorePlayerReads();
    if (queuedCorePlayerReads && readPlayerFlagsThisTick)
        s_lastPlayerFlagReadUs = coreNowUs;
    if (queuedCorePlayerReads && readEyeAnglesThisTick)
        s_lastPlayerEyeAnglesReadUs = coreNowUs;

    static bool s_coreReadUnstable[64] = {};
    static uintptr_t s_coreReadStabilityPawn[64] = {};
    static uint32_t s_coreReadStabilityHandle[64] = {};
    static uint64_t s_coreReadStabilityResetSerial = 0;
    {
        const uint64_t resetSerial = s_sceneResetSerial.load(std::memory_order_relaxed);
        if (s_coreReadStabilityResetSerial != resetSerial) {
            s_coreReadStabilityResetSerial = resetSerial;
            memset(s_coreReadUnstable, 0, sizeof(s_coreReadUnstable));
            memset(s_coreReadStabilityPawn, 0, sizeof(s_coreReadStabilityPawn));
            memset(s_coreReadStabilityHandle, 0, sizeof(s_coreReadStabilityHandle));
        }
    }
    const auto coreStabilityWarmupState =
        static_cast<esp::SceneWarmupState>(s_sceneWarmupState.load(std::memory_order_relaxed));
    const bool coreAnomalyEligible =
        !sceneSettling &&
        coreStabilityWarmupState == esp::SceneWarmupState::Stable &&
        s_engineInGame.load(std::memory_order_relaxed);
    auto markCoreAvailability = [&](int idx, bool available) {
        if (idx < 0 || idx >= 64)
            return;
        if (s_coreReadStabilityPawn[idx] != pawns[idx] ||
            s_coreReadStabilityHandle[idx] != pawnHandles[idx]) {
            s_coreReadStabilityPawn[idx] = pawns[idx];
            s_coreReadStabilityHandle[idx] = pawnHandles[idx];
            s_coreReadUnstable[idx] = false;
        }
        if (!pawns[idx]) {
            s_coreReadUnstable[idx] = false;
            return;
        }
        if (available) {
            if (s_coreReadUnstable[idx])
                s_playerCoreRecoveredCount.fetch_add(1, std::memory_order_relaxed);
            s_coreReadUnstable[idx] = false;
            return;
        }
        if (coreAnomalyEligible && !s_coreReadUnstable[idx]) {
            s_coreReadUnstable[idx] = true;
            s_playerCoreAnomalyCount.fetch_add(1, std::memory_order_relaxed);
        }
    };
    auto applyCoreBatchResults = [&]() {
        for (int resolvedIdx = 0; resolvedIdx < playerResolvedSlotCount; ++resolvedIdx) {
            const int i = playerResolvedSlots[resolvedIdx];
            if (!pawns[i])
                continue;
            unpackCoreVitalRead(i);
            if (coreTeamReadsQueued[i])
                markLiveTeamRead(i);
            if (coreArmorReadsQueued[i] &&
                coreArmorBytesRead[i] == sizeof(armors[i]) &&
                armors[i] >= 0 && armors[i] <= 500) {
                s_cachedCoreArmors[i] = armors[i];
                s_cachedCoreArmorPawns[i] = pawns[i];
                s_lastCoreArmorReadUs[i] = coreNowUs;
            }
            markCoreReadResult(i);
            velocityReadFresh[i] =
                ofs.C_BaseEntity_m_vecVelocity > 0 &&
                coreVelocityBytesRead[i] == sizeof(velocities[i]) &&
                esp::validation::IsValidVelocity(velocities[i]);
            spottedReadFresh[i] = spottedReadCompleted(i);
            if (coreReadPlausible[i]) {
                markCoreAvailability(i, true);
                markPlayerFlagFreshness(i);
                cacheCoreAuxState(i);
            } else {
                markCoreAvailability(i, false);
            }
        }
    };
    const bool coreScatterOk =
        !queuedCorePlayerReads || mem.ExecuteReadScatter(handle);


    if (coreScatterOk && queuedCorePlayerReads)
        applyCoreBatchResults();

    if (!coreScatterOk) {
        playersCoreHadFailure = true;
        logUpdateDataIssue("scatter_10", "advanced_player_batch_failed_retry_core");
        memset(velocities, 0, sizeof(velocities));

        queueCorePlayerReads();

         const bool retryCoreScatterOk = mem.ExecuteReadScatter(handle);
         if (!retryCoreScatterOk) {
            memset(healths, 0, sizeof(healths));
            memset(armors, 0, sizeof(armors));
            memset(lifeStates, 0, sizeof(lifeStates));
            memset(positions, 0, sizeof(positions));
            memcpy(teams, s_cachedCoreTeams, sizeof(teams));

            bool recoveredAnyCorePlayer = false;
            for (int resolvedIdx = 0; resolvedIdx < playerResolvedSlotCount; ++resolvedIdx) {
                const int i = playerResolvedSlots[resolvedIdx];
                if (!pawns[i])
                    continue;
                if (!isLikelyGamePointer(pawns[i])) {
                    markCoreAvailability(i, false);
                    continue;
                }

                int healthValue = 0;
                int armorValue = armors[i];
                esp::data::PlayerTeamSample teamValue = static_cast<esp::data::PlayerTeamSample>(teams[i]);
                uint8_t lifeStateValue = 0;
                Vector3 positionValue = {};
                Vector3 velocityValue = {};
                const bool needsTeamRead = coreTeamRefreshDueAt(coreNowUs, i);
                const bool needsArmorRead = coreArmorRefreshDueAt(coreNowUs, i);
                DWORD healthBytesRead = 0;
                DWORD armorBytesRead = 0;
                DWORD teamBytesRead = 0;
                DWORD lifeStateBytesRead = 0;
                DWORD positionBytesRead = 0;
                DWORD velocityBytesRead = 0;
                uint8_t gunGameImmunityValue =
                    esp::data::kInvalidPlayerFlagSample;
                DWORD gunGameImmunityBytesReadValue = 0;

                uint8_t scopedVal = esp::data::kInvalidPlayerFlagSample;
                uint8_t defusingVal = esp::data::kInvalidPlayerFlagSample;
                float flashBangTimeVal = std::numeric_limits<float>::quiet_NaN();
                float flashVal = std::numeric_limits<float>::quiet_NaN();
                DWORD scopedBytesReadVal = 0;
                DWORD defusingBytesReadVal = 0;
                DWORD flashBangTimeBytesReadVal = 0;
                DWORD flashDurationBytesReadVal = 0;
                Vector3 eyeVal = eyeAnglesPerPlayer[i];
                uint32_t spottedMaskVal[2] = {};
                DWORD spottedMaskBytesReadVal = 0;

                mem.AddScatterReadRequest(handle, pawns[i] + ofs.C_BaseEntity_m_iHealth, &healthValue, sizeof(healthValue), &healthBytesRead);
                if (needsArmorRead) {
                    mem.AddScatterReadRequest(handle, pawns[i] + ofs.C_CSPlayerPawn_m_ArmorValue, &armorValue, sizeof(armorValue), &armorBytesRead);
                } else {
                    armorBytesRead = sizeof(armorValue);
                }
                mem.AddScatterReadRequest(handle, pawns[i] + ofs.C_BaseEntity_m_lifeState, &lifeStateValue, sizeof(lifeStateValue), &lifeStateBytesRead);
                mem.AddScatterReadRequest(handle, pawns[i] + ofs.C_BasePlayerPawn_m_vOldOrigin, &positionValue, sizeof(positionValue), &positionBytesRead);
                if (wantsGunGameImmunity) {
                    mem.AddScatterReadRequest(
                        handle,
                        pawns[i] + ofs.C_CSPlayerPawn_m_bGunGameImmunity,
                        &gunGameImmunityValue,
                        sizeof(gunGameImmunityValue),
                        &gunGameImmunityBytesReadValue);
                }
                if (wantsTargetVelocity && ofs.C_BaseEntity_m_vecVelocity > 0) {
                    mem.AddScatterReadRequest(
                        handle,
                        pawns[i] + ofs.C_BaseEntity_m_vecVelocity,
                        &velocityValue,
                        sizeof(velocityValue),
                        &velocityBytesRead);
                }
                if (needsTeamRead)
                    mem.AddScatterReadRequest(handle, pawns[i] + ofs.C_BaseEntity_m_iTeamNum, &teamValue, sizeof(teamValue), &teamBytesRead);

                if (readPlayerFlagsThisTick && wantsFastScopedFlag && ofs.C_CSPlayerPawn_m_bIsScoped > 0)
                    mem.AddScatterReadRequest(
                        handle,
                        pawns[i] + ofs.C_CSPlayerPawn_m_bIsScoped,
                        &scopedVal,
                        sizeof(uint8_t),
                        &scopedBytesReadVal);
                if (readPlayerFlagsThisTick && wantsFastDefusingFlag && ofs.C_CSPlayerPawn_m_bIsDefusing > 0)
                    mem.AddScatterReadRequest(
                        handle,
                        pawns[i] + ofs.C_CSPlayerPawn_m_bIsDefusing,
                        &defusingVal,
                        sizeof(uint8_t),
                        &defusingBytesReadVal);
                if (readPlayerFlagsThisTick && wantsFastBlindFlag &&
                    ofs.C_CSPlayerPawnBase_m_flFlashBangTime > 0 &&
                    ofs.C_CSPlayerPawnBase_m_flFlashDuration > 0) {
                    mem.AddScatterReadRequest(
                        handle,
                        pawns[i] + ofs.C_CSPlayerPawnBase_m_flFlashBangTime,
                        &flashBangTimeVal,
                        sizeof(float),
                        &flashBangTimeBytesReadVal);
                    mem.AddScatterReadRequest(
                        handle,
                        pawns[i] + ofs.C_CSPlayerPawnBase_m_flFlashDuration,
                        &flashVal,
                        sizeof(float),
                        &flashDurationBytesReadVal);
                }
                if (readEyeAnglesThisTick && wantsFastEyeAngles && ofs.C_CSPlayerPawn_m_angEyeAngles > 0)
                    mem.AddScatterReadRequest(handle, pawns[i] + ofs.C_CSPlayerPawn_m_angEyeAngles, &eyeVal, sizeof(Vector3));
                if (wantsFastSpottedState && hasSpottedStateOffsets) {
                    mem.AddScatterReadRequest(
                        handle,
                        pawns[i] + ofs.C_CSPlayerPawn_m_entitySpottedState +
                            ofs.EntitySpottedState_t_m_bSpottedByMask,
                        reinterpret_cast<void*>(spottedMaskVal),
                        sizeof(spottedMaskVal),
                        &spottedMaskBytesReadVal);
                }

                if (!mem.ExecuteReadScatter(handle)) {
                    markCoreAvailability(i, false);
                    continue;
                }

                const bool requiredReadsComplete =
                    esp::data::IsPlayerCoreReadComplete({
                        healthBytesRead,
                        armorBytesRead,
                        lifeStateBytesRead,
                        positionBytesRead,
                        needsTeamRead,
                        teamBytesRead,
                    });
                const bool teamValueValid =
                    !needsTeamRead ||
                    (teamBytesRead == sizeof(teamValue) &&
                     teamLooksValid(teamValue));
                const bool slotCoreLooksPlausible =
                    requiredReadsComplete &&
                    teamValueValid &&
                    coreValuesLookPlausible(
                        teamValue,
                        healthValue,
                        armorValue,
                        lifeStateValue,
                        positionValue);
                if (!slotCoreLooksPlausible) {
                    markCoreAvailability(i, false);
                    continue;
                }

                healths[i] = healthValue;
                armors[i] = armorValue;
                coreHealthBytesRead[i] = healthBytesRead;
                coreArmorBytesRead[i] = armorBytesRead;
                coreLifeStateBytesRead[i] = lifeStateBytesRead;
                corePositionBytesRead[i] = positionBytesRead;
                coreTeamBytesRead[i] = teamBytesRead;
                coreTeamReadsQueued[i] = needsTeamRead;
                coreArmorReadsQueued[i] = needsArmorRead;
                if (needsArmorRead) {
                    s_cachedCoreArmors[i] = armorValue;
                    s_cachedCoreArmorPawns[i] = pawns[i];
                    s_lastCoreArmorReadUs[i] = coreNowUs;
                }
                lifeStates[i] = lifeStateValue;
                positions[i] = positionValue;
                gunGameImmunityFlags[i] = gunGameImmunityValue;
                gunGameImmunityBytesRead[i] =
                    gunGameImmunityBytesReadValue;
                if (wantsTargetVelocity && ofs.C_BaseEntity_m_vecVelocity > 0 &&
                    velocityBytesRead == sizeof(velocityValue) &&
                    esp::validation::IsValidVelocity(velocityValue)) {
                    velocities[i] = velocityValue;
                    velocityReadFresh[i] = true;
                }
                if (needsTeamRead && teamLooksValid(teamValue)) {
                    teams[i] = teamValue;
                    coreTeamSamples[i] = teamValue;
                    coreTeamReadsQueued[i] = true;
                    liveTeamReads[i] = true;
                }

                scopedFlags[i] = scopedVal;
                defusingFlags[i] = defusingVal;
                flashBangTimes[i] = flashBangTimeVal;
                flashDurations[i] = flashVal;
                scopedFlagBytesRead[i] = scopedBytesReadVal;
                defusingFlagBytesRead[i] = defusingBytesReadVal;
                flashBangTimeBytesRead[i] = flashBangTimeBytesReadVal;
                flashDurationBytesRead[i] = flashDurationBytesReadVal;
                eyeAnglesPerPlayer[i] = eyeVal;
                spottedReadFresh[i] =
                    esp::data::IsSpottedStateReadComplete(
                        wantsFastSpottedState && hasSpottedStateOffsets,
                        spottedMaskBytesReadVal,
                        sizeof(spottedMaskVal));
                if (spottedReadFresh[i]) {
                    spottedMasks[i][0] = spottedMaskVal[0];
                    spottedMasks[i][1] = spottedMaskVal[1];
                }

                markPlayerFlagFreshness(i);
                cacheCoreAuxState(i);

                markCoreReadResult(i);
                markCoreAvailability(i, true);
                recoveredAnyCorePlayer = true;
            }

            const bool localCoreEvidence =
                (localTeam == 2 || localTeam == 3) ||
                (localControllerPawnHandle != 0u && localControllerPawnHandle != 0xFFFFFFFFu);
            playersCoreHardFailure = !recoveredAnyCorePlayer && !localCoreEvidence;
            if (!recoveredAnyCorePlayer && !localCoreEvidence)
                logUpdateDataIssue("scatter_10_core", "player_core_state_unavailable_using_cached_snapshot");
        } else {
            applyCoreBatchResults();
        }
    }
    static esp::data::PlayerFlagFilterState s_scopedFlagFilters[64] = {};
    static esp::data::PlayerFlagFilterState s_defusingFlagFilters[64] = {};
    static esp::data::PlayerFlagFilterState s_blindFlagFilters[64] = {};
    static uintptr_t s_playerFlagPawns[64] = {};
    static uint64_t s_playerFlagResetSerial = 0;
    const uint64_t playerFlagResetSerial =
        s_sceneResetSerial.load(std::memory_order_relaxed);
    if (s_playerFlagResetSerial != playerFlagResetSerial) {
        s_playerFlagResetSerial = playerFlagResetSerial;
        memset(s_scopedFlagFilters, 0, sizeof(s_scopedFlagFilters));
        memset(s_defusingFlagFilters, 0, sizeof(s_defusingFlagFilters));
        memset(s_blindFlagFilters, 0, sizeof(s_blindFlagFilters));
        memset(s_playerFlagPawns, 0, sizeof(s_playerFlagPawns));
    }

    for (int i = 0; i < 64; ++i) {
        if (s_playerFlagPawns[i] != pawns[i]) {
            s_playerFlagPawns[i] = pawns[i];
            esp::data::ResetPlayerFlagFilter(s_scopedFlagFilters[i]);
            esp::data::ResetPlayerFlagFilter(s_defusingFlagFilters[i]);
            esp::data::ResetPlayerFlagFilter(s_blindFlagFilters[i]);
        }

        const bool confirmedDead =
            coreReadFresh[i] &&
            (!coreReadAlive[i] || healths[i] <= 0 || lifeStates[i] != 0);
        if (!pawns[i] || confirmedDead) {
            esp::data::ResetPlayerFlagFilter(s_scopedFlagFilters[i]);
            esp::data::ResetPlayerFlagFilter(s_defusingFlagFilters[i]);
            esp::data::ResetPlayerFlagFilter(s_blindFlagFilters[i]);
            scopedFlags[i] = 0;
            defusingFlags[i] = 0;
            flashBangTimes[i] = 0.0f;
            flashDurations[i] = 0.0f;
            s_cachedScopedFlags[i] = 0;
            s_cachedDefusingFlags[i] = 0;
            s_cachedFlashDurations[i] = 0.0f;
            continue;
        }

        const bool scopedActive =
            esp::data::UpdatePlayerFlagFilter(
                s_scopedFlagFilters[i],
                wantsFastScopedFlag,
                scopedReadFresh[i],
                scopedReadFresh[i] && scopedFlags[i] == 1u,
                coreNowUs);
        const bool defusingActive =
            esp::data::UpdatePlayerFlagFilter(
                s_defusingFlagFilters[i],
                wantsFastDefusingFlag,
                defusingReadFresh[i],
                defusingReadFresh[i] && defusingFlags[i] == 1u,
                coreNowUs);
        const auto blindSample = flashReadFresh[i]
            ? esp::data::EvaluateBlindFlashSample(
                  flashBangTimes[i],
                  flashDurations[i],
                  currentGameTime,
                  s_blindFlagFilters[i].active)
            : esp::data::BlindFlashSample{};
        const bool blindActive =
            esp::data::UpdatePlayerFlagFilter(
                s_blindFlagFilters[i],
                wantsFastBlindFlag,
                blindSample.fresh,
                blindSample.active,
                coreNowUs);

        scopedFlags[i] = scopedActive ? 1u : 0u;
        defusingFlags[i] = defusingActive ? 1u : 0u;
        s_cachedScopedFlags[i] = scopedFlags[i];
        s_cachedDefusingFlags[i] = defusingFlags[i];
        if (blindActive) {
            if (blindSample.fresh)
                s_cachedFlashDurations[i] = blindSample.remainingSeconds;
            flashDurations[i] = s_cachedFlashDurations[i];
        } else {
            flashDurations[i] = 0.0f;
            s_cachedFlashDurations[i] = 0.0f;
        }
    }

    for (int resolvedIdx = 0; resolvedIdx < playerResolvedSlotCount; ++resolvedIdx) {
        const int i = playerResolvedSlots[resolvedIdx];
        if (!pawns[i]) {
            s_cachedCoreTeams[i] = 0;
            s_cachedCoreTeamPawns[i] = 0;
            s_lastCoreTeamReadUs[i] = 0;
            continue;
        }
        s_cachedCoreTeamPawns[i] = pawns[i];
        s_cachedCoreArmorPawns[i] = pawns[i];
        if (coreArmorBytesRead[i] == sizeof(armors[i]) && armors[i] >= 0 && armors[i] <= 500)
            s_cachedCoreArmors[i] = armors[i];
        if (coreTeamBytesRead[i] == sizeof(esp::data::PlayerTeamSample) &&
            liveTeamReads[i] && teamLooksValid(teams[i])) {
            s_cachedCoreTeams[i] = teams[i];
            if (coreTeamReadsQueued[i] || s_lastCoreTeamReadUs[i] == 0)
                s_lastCoreTeamReadUs[i] = coreNowUs;
        } else if (!teamLooksValid(s_cachedCoreTeams[i])) {
            s_lastCoreTeamReadUs[i] = 0;
        }
    }
