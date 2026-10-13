    {
        static uint64_t s_coreRepairResetSerial = 0;
        static uint8_t s_coreRepairStreaks[64] = {};
        static uint64_t s_lastCoreRepairAttemptUs[64] = {};
        static esp::data::CoreRecoveryState s_coreRecovery[64] = {};
        static uint32_t s_partialCoreStreak = 0;
        static uint64_t s_partialCoreSinceUs = 0;
        static uint64_t s_lastPartialCoreBadMask = 0;
        static uint32_t s_partialCoreConfirmedStreak = 0;
        static bool s_partialCoreIncidentActive = false;
        const uint64_t coreRepairResetSerial = s_sceneResetSerial.load(std::memory_order_relaxed);
        if (s_coreRepairResetSerial != coreRepairResetSerial) {
            s_coreRepairResetSerial = coreRepairResetSerial;
            memset(s_coreRepairStreaks, 0, sizeof(s_coreRepairStreaks));
            memset(s_lastCoreRepairAttemptUs, 0, sizeof(s_lastCoreRepairAttemptUs));
            for (auto& recovery : s_coreRecovery)
                recovery = {};
            s_partialCoreStreak = 0;
            s_partialCoreSinceUs = 0;
            s_lastPartialCoreBadMask = 0;
            s_partialCoreConfirmedStreak = 0;
            s_partialCoreIncidentActive = false;
        }
        const int repairSlotLimit = std::max(
            playerSlotScanLimit,
            std::clamp(s_playerHierarchyHighWaterSlot.load(std::memory_order_relaxed), 0, 64));
        for (int i = 0; i < repairSlotLimit; ++i) {
            if (pawns[i])
                continue;
            s_coreRepairStreaks[i] = 0;
            s_lastCoreRepairAttemptUs[i] = 0;
            s_coreRecovery[i] = {};
        }

        int pawnCount = 0;
        int saneCoreCount = 0;
        uint64_t partialCoreBadMask = 0;
        const uint64_t coreRepairNowUs = TickNowUs();
        const uint64_t recentResetUs = s_lastSceneResetUs.load(std::memory_order_relaxed);
        const bool recentStructuralReset =
            esp::data::IsRecentStructuralReset(recentResetUs, coreRepairNowUs);

        const auto coreRepairPolicy =
            esp::data::SelectCoreRepairPolicy(sceneSettling, recentStructuralReset);
        
        bool anyCoreRepairNeeded = false;
        bool repairAttempted[64] = {};
        for (int resolvedIdx = 0; resolvedIdx < playerResolvedSlotCount; ++resolvedIdx) {
            const int i = playerResolvedSlots[resolvedIdx];
            if (!pawns[i]) {
                s_coreRepairStreaks[i] = 0;
                s_lastCoreRepairAttemptUs[i] = 0;
                continue;
            }
            ++pawnCount;
            if (s_coreRecovery[i].pawn != pawns[i] ||
                s_coreRecovery[i].handle != pawnHandles[i]) {
                s_coreRepairStreaks[i] = 0;
                s_lastCoreRepairAttemptUs[i] = 0;
            }
            if (coreReadPlausible[i]) {
                s_coreRepairStreaks[i] = 0;
                continue;
            }
            partialCoreBadMask |= (1ull << static_cast<unsigned>(i));
            if (s_coreRepairStreaks[i] < 255u)
                ++s_coreRepairStreaks[i];
            if (s_coreRepairStreaks[i] < coreRepairPolicy.repairThreshold)
                continue;
            if (!esp::data::IsCoreRepairAttemptDue(
                    s_lastCoreRepairAttemptUs[i],
                    coreRepairNowUs,
                    esp::data::SelectCoreRepairRetryIntervalUs(
                        coreRepairPolicy.retryIntervalUs,
                        s_coreRepairStreaks[i])))
                continue;
            s_lastCoreRepairAttemptUs[i] = coreRepairNowUs;
            queueMandatoryCoreReads(i, coreRepairNowUs, false);
            repairAttempted[i] = true;
            anyCoreRepairNeeded = true;
        }
        if (anyCoreRepairNeeded) {
            executeOptionalScatterRead();
            // Repairs use the same coalesced vital blocks as the first batch.
            // Unpack them and refresh per-field caches before evaluating success.
            applyCoreBatchResults();
        }
        if (anyCoreRepairNeeded && ofs.C_BaseEntity_m_pGameSceneNode > 0 &&
            ofs.CGameSceneNode_m_vecAbsOrigin > 0 && ofs.C_BasePlayerPawn_m_hController > 0) {
            struct PositionRepair {
                uintptr_t node = 0;
                uintptr_t confirmedNode = 0;
                uint32_t pawnHandle = 0;
                uint32_t controllerHandle = 0;
                Vector3 position = {};
                int health = 0;
                uint8_t lifeState = 0xFFu;
                DWORD nodeBytes = 0;
                DWORD confirmedNodeBytes = 0;
                DWORD pawnHandleBytes = 0;
                DWORD controllerHandleBytes = 0;
                DWORD positionBytes = 0;
                DWORD healthBytes = 0;
                DWORD lifeStateBytes = 0;
            };
            PositionRepair repairs[64] = {};
            bool queuedNodes = false;
            for (int resolvedIdx = 0; resolvedIdx < playerResolvedSlotCount; ++resolvedIdx) {
                const int i = playerResolvedSlots[resolvedIdx];
                if (!repairAttempted[i] || !coreReadCompleted(i) ||
                    esp::data::ResolveCoreRejectionReason(teamLooksValid(teams[i]),
                        healths[i], armors[i], lifeStates[i], isValidWorldPos(positions[i])) !=
                        esp::data::CoreRejectionReason::Position)
                    continue;
                auto& repair = repairs[i];
                mem.AddScatterReadRequest(handle, pawns[i] + ofs.C_BaseEntity_m_pGameSceneNode,
                    &repair.node, sizeof(repair.node), &repair.nodeBytes);
                queuedNodes = true;
            }
            if (queuedNodes) {
                executeOptionalScatterRead();
                bool queuedPositions = false;
                for (int resolvedIdx = 0; resolvedIdx < playerResolvedSlotCount; ++resolvedIdx) {
                    const int i = playerResolvedSlots[resolvedIdx];
                    auto& repair = repairs[i];
                    if (repair.nodeBytes != sizeof(repair.node) || !isLikelyGamePointer(repair.node))
                        continue;
                    mem.AddScatterReadRequest(handle, repair.node + ofs.CGameSceneNode_m_vecAbsOrigin,
                        &repair.position, sizeof(repair.position), &repair.positionBytes);
                    mem.AddScatterReadRequest(handle, pawns[i] + ofs.C_BaseEntity_m_pGameSceneNode,
                        &repair.confirmedNode, sizeof(repair.confirmedNode), &repair.confirmedNodeBytes);
                    mem.AddScatterReadRequest(handle, controllers[i] + ofs.CCSPlayerController_m_hPlayerPawn,
                        &repair.pawnHandle, sizeof(repair.pawnHandle), &repair.pawnHandleBytes);
                    mem.AddScatterReadRequest(handle, pawns[i] + ofs.C_BasePlayerPawn_m_hController,
                        &repair.controllerHandle, sizeof(repair.controllerHandle), &repair.controllerHandleBytes);
                    mem.AddScatterReadRequest(handle, pawns[i] + ofs.C_BaseEntity_m_iHealth,
                        &repair.health, sizeof(repair.health), &repair.healthBytes);
                    mem.AddScatterReadRequest(handle, pawns[i] + ofs.C_BaseEntity_m_lifeState,
                        &repair.lifeState, sizeof(repair.lifeState), &repair.lifeStateBytes);
                    queuedPositions = true;
                }
                if (queuedPositions) {
                    executeOptionalScatterRead();
                    for (int resolvedIdx = 0; resolvedIdx < playerResolvedSlotCount; ++resolvedIdx) {
                        const int i = playerResolvedSlots[resolvedIdx];
                        const auto& repair = repairs[i];
                        const bool complete = repair.positionBytes == sizeof(repair.position) &&
                            repair.confirmedNodeBytes == sizeof(repair.confirmedNode) &&
                            repair.pawnHandleBytes == sizeof(repair.pawnHandle) &&
                            repair.controllerHandleBytes == sizeof(repair.controllerHandle) &&
                            repair.healthBytes == sizeof(repair.health) &&
                            repair.lifeStateBytes == sizeof(repair.lifeState);
                        if (!esp::data::IsScenePositionRepairCoherent(complete,
                                repair.node, repair.confirmedNode, pawnHandles[i], repair.pawnHandle,
                                esp::data::EvaluateControllerBacklink(i, repair.controllerHandle,
                                    kEntityHandleMask) == esp::data::ControllerBacklinkState::Match,
                                isValidWorldPos(repair.position), repair.health, repair.lifeState))
                            continue;
                        positions[i] = repair.position;
                        healths[i] = repair.health;
                        lifeStates[i] = repair.lifeState;
                        corePositionBytesRead[i] = sizeof(positions[i]);
                        markCoreReadResult(i);
                        markPlayerFlagFreshness(i);
                        cacheCoreAuxState(i);
                    }
                }
            }
        }
        for (int resolvedIdx = 0; resolvedIdx < playerResolvedSlotCount; ++resolvedIdx) {
            const int i = playerResolvedSlots[resolvedIdx];
            if (pawns[i] &&
                coreReadCompleted(i) &&
                coreStateLooksSane(i)) {
                s_coreRepairStreaks[i] = 0;
                coreReadFresh[i] = true;
                coreReadPlausible[i] = true;
                coreReadAlive[i] = healths[i] > 0 && lifeStates[i] == 0;
                gunGameImmunityReadFresh[i] =
                    esp::data::IsBinaryPlayerFlagReadComplete(
                        wantsGunGameImmunity,
                        gunGameImmunityBytesRead[i],
                        gunGameImmunityFlags[i]);
                markCoreAvailability(i, true);
                ++saneCoreCount;
            }
        }

        esp::data::CoreRecoveryAction recoveryAction = esp::data::CoreRecoveryAction::None;
        for (int resolvedIdx = 0; resolvedIdx < playerResolvedSlotCount; ++resolvedIdx) {
            const int i = playerResolvedSlots[resolvedIdx];
            const auto action = esp::data::ObserveCoreRecovery(
                s_coreRecovery[i], pawns[i], pawnHandles[i], coreReadPlausible[i],
                coreAnomalyEligible, coreRepairNowUs);
            if (action > recoveryAction)
                recoveryAction = action;
        }
        if (recoveryAction == esp::data::CoreRecoveryAction::Repair)
            refreshDmaCaches("persistent_player_core_repair", DmaRefreshTier::Repair);
        else if (recoveryAction == esp::data::CoreRecoveryAction::Probe)
            refreshDmaCaches("persistent_player_core_probe", DmaRefreshTier::Probe);

        const bool localCoreEvidence =
            localPawn != 0 ||
            (localControllerPawnHandle != 0u && localControllerPawnHandle != 0xFFFFFFFFu);
        const bool coreLooksPartial =
            esp::data::IsCorePartial(
                pawnCount,
                saneCoreCount,
                coreRepairPolicy.missingTolerance);
        if (coreLooksPartial && localCoreEvidence) {
            if (s_partialCoreStreak < 0xFFFFFFFFu)
                ++s_partialCoreStreak;
            if (s_partialCoreSinceUs == 0)
                s_partialCoreSinceUs = coreRepairNowUs;
            const bool repeatedBadSlots =
                esp::data::ArePartialCoreBadSlotsRepeated(
                    s_lastPartialCoreBadMask,
                    partialCoreBadMask);
            if (repeatedBadSlots) {
                if (s_partialCoreConfirmedStreak < 0xFFFFFFFFu)
                    ++s_partialCoreConfirmedStreak;
            } else {
                s_partialCoreConfirmedStreak = 0;
            }
            s_lastPartialCoreBadMask = partialCoreBadMask;
            const uint64_t partialAgeUs =
                esp::data::ElapsedSinceOrZero(coreRepairNowUs, s_partialCoreSinceUs);
            const bool confirmedPartial =
                esp::data::IsPartialCoreConfirmed(
                    sceneSettling,
                    repeatedBadSlots,
                    partialCoreBadMask,
                    s_partialCoreConfirmedStreak,
                    partialAgeUs,
                    recentStructuralReset);
            if (esp::data::ShouldReportPartialCoreIncident(
                    sceneSettling,
                    s_partialCoreStreak,
                    coreRepairPolicy.partialCoreThreshold,
                    confirmedPartial) &&
                !s_partialCoreIncidentActive) {
                s_partialCoreIncidentActive = true;
                s_playerCoreGlobalRefreshAvoidedCount.fetch_add(1, std::memory_order_relaxed);
            }
        } else {
            s_partialCoreStreak = 0;
            s_partialCoreSinceUs = 0;
            s_lastPartialCoreBadMask = 0;
            s_partialCoreConfirmedStreak = 0;
            s_partialCoreIncidentActive = false;
        }
    }
