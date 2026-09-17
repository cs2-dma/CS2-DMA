        static uint64_t s_commitPlayerResetSerial = 0;
        static uint64_t s_zeroPawnSinceUs[64] = {};
        static uint64_t s_coreInvalidSinceUs[64] = {};
        static uint64_t s_deadReadSinceUs[64] = {};
        static uintptr_t s_deadReadPawn[64] = {};
        static uint64_t s_lastAliveCoreReadUs[64] = {};
        static uintptr_t s_committedPlayerControllers[64] = {};
        const uint64_t commitPlayerResetSerial = s_sceneResetSerial.load(std::memory_order_relaxed);
        if (s_commitPlayerResetSerial != commitPlayerResetSerial) {
            s_commitPlayerResetSerial = commitPlayerResetSerial;
            memset(s_zeroPawnSinceUs, 0, sizeof(s_zeroPawnSinceUs));
            memset(s_coreInvalidSinceUs, 0, sizeof(s_coreInvalidSinceUs));
            memset(s_deadReadSinceUs, 0, sizeof(s_deadReadSinceUs));
            memset(s_deadReadPawn, 0, sizeof(s_deadReadPawn));
            memset(
                s_committedPlayerControllers,
                0,
                sizeof(s_committedPlayerControllers));
            for (int idx = 0; idx < 64; ++idx) {
                s_lastAliveCoreReadUs[idx] =
                    s_players[idx].valid &&
                    s_players[idx].pawn != 0 &&
                    s_players[idx].health > 0
                        ? nowUs
                        : 0;
            }
        }

        auto clearPendingPlayerState = [&](int idx) {
            s_zeroPawnSinceUs[idx] = 0;
            s_coreInvalidSinceUs[idx] = 0;
            s_deadReadSinceUs[idx] = 0;
            s_deadReadPawn[idx] = 0;
        };

        auto invalidatePlayerSlot = [&](int idx) {
            s_playerInvalidReadStreak[idx] = 0;
            refreshed[idx] = false;
            s_players[idx] = {};
            s_committedPlayerControllers[idx] = 0;
            clearPendingPlayerState(idx);
            s_lastAliveCoreReadUs[idx] = 0;
        };

        const uint64_t recentResetUs = s_lastSceneResetUs.load(std::memory_order_relaxed);
        const bool recentStructuralReset =
            esp::data::IsRecentStructuralReset(recentResetUs, nowUs);

        const uint64_t lastBulkEvictUs =
            s_lastBulkEvictionUs.load(std::memory_order_relaxed);
        const bool inBulkRecovery =
            esp::data::IsWithinBulkRecoveryStaleWindow(lastBulkEvictUs, nowUs);
        auto& s_deathConfirmCount = s_playerDeathConfirmCount;

        int visibilityFreshSlots = 0;
        int visibilityVisibleSlots = 0;
        int visibilityMaskSlots = 0;
        int zeroPawnHeldSlots = 0;
        int coreHeldSlots = 0;
        for (int i = 0; i < 64; i++) {
            if (s_players[i].valid) {
                const bool sameImmunityIdentity =
                    s_players[i].pawn != 0 &&
                    s_players[i].pawn == pawns[i];
                s_players[i].gunGameImmunityValid =
                    sameImmunityIdentity && gunGameImmunityReadFresh[i];
                s_players[i].gunGameImmunity =
                    s_players[i].gunGameImmunityValid &&
                    gunGameImmunityFlags[i] == 1u;
                s_players[i].gunGameImmunityUpdatedAtUs =
                    s_players[i].gunGameImmunityValid ? nowUs : 0u;
            }
            if (hierarchyIdentityRejected[i]) {
                invalidatePlayerSlot(i);
                s_deathConfirmCount[i] = 0;
                continue;
            }
            const bool isLocalSlot =
                localPlayerIndexValid &&
                localPlayerIndexHasLiveEvidence &&
                (i == localPlayerIndex);
            const bool isLocalControllerSlot =
                localControllerMaskBit > 0 &&
                localControllerMaskBit <= 64 &&
                i == (localControllerMaskBit - 1);
            const bool isLocalPawn =
                (localPawnResolved != 0) &&
                (pawns[i] == localPawnResolved);
            const bool isLocalHandle =
                localControllerPawnHandleValid &&
                pawnHandles[i] != 0u &&
                pawnHandles[i] != 0xFFFFFFFFu &&
                pawnHandles[i] == localControllerPawnHandle;
            const bool teamLiveResolved = liveTeamReads[i];
            const bool teamValid =
                (teams[i] == 1 || teams[i] == 2 || teams[i] == 3);
            const bool positionValid =
                isValidWorldPos(positions[i]);
            const bool coreFreshThisFrame =
                coreRosterCommitAllowed &&
                coreReadFresh[i] &&
                coreReadPlausible[i] &&
                pawns[i] != 0;
            const bool coreAliveThisFrame =
                coreFreshThisFrame &&
                coreReadAlive[i] &&
                healths[i] > 0 &&
                lifeStates[i] == 0;
            const bool coreDeadThisFrame =
                coreFreshThisFrame &&
                !coreAliveThisFrame &&
                (healths[i] <= 0 || lifeStates[i] != 0);
            const bool perSlotVitalSampleFresh =
                coreHealthBytesRead[i] == sizeof(healths[i]) &&
                coreLifeStateBytesRead[i] == sizeof(lifeStates[i]) &&
                healths[i] >= 0 &&
                healths[i] <= 500 &&
                lifeStates[i] <= 2;
            const bool authoritativeDeadThisFrame =
                esp::data::IsAuthoritativeDeadCoreSample(
                    perSlotVitalSampleFresh,
                    pawns[i],
                    healths[i],
                    lifeStates[i]);
            if (coreFreshThisFrame) {
                if (coreAliveThisFrame)
                    s_lastAliveCoreReadUs[i] = nowUs;
            }
            const bool hadTrackedPlayer =
                s_players[i].valid &&
                s_players[i].pawn != 0;
            const bool pawnChangedThisFrame =
                hadTrackedPlayer &&
                s_players[i].pawn != pawns[i];
            const bool coreLooksInvalid =
                !coreAliveThisFrame ||
                !teamValid ||
                !positionValid;
            const bool structuralLiveEvidence =
                pawns[i] != 0 &&
                coreFreshThisFrame &&
                teamValid &&
                positionValid;

            if (isLocalSlot || isLocalControllerSlot || isLocalPawn || isLocalHandle) {
                if (s_players[i].valid)
                    RecordEspEvent({
                        .type = EspEventType::SlotEvictedLocal,
                        .slot = static_cast<uint8_t>(i)
                    });
                invalidatePlayerSlot(i);
                s_deathConfirmCount[i] = 0;
                continue;
            }
            if (!pawns[i]) {
                if (s_players[i].valid && s_players[i].pawn != 0) {
                    if (s_zeroPawnSinceUs[i] == 0)
                        s_zeroPawnSinceUs[i] = nowUs;
                    const uint64_t zeroPawnGraceUs =
                        esp::data::SelectZeroPawnGraceUs(inBulkRecovery);
                    if (esp::data::IsWithinGraceWindow(s_zeroPawnSinceUs[i], nowUs, zeroPawnGraceUs)) {
                        ++zeroPawnHeldSlots;
                        if (!sceneSettling || recentStructuralReset || inBulkRecovery)
                            ++s_playerInvalidReadStreak[i];
                        refreshed[i] = true;
                        continue;
                    }
                }
                if (s_players[i].valid)
                    RecordEspEvent({
                        .type = EspEventType::SlotEvictedMissing,
                        .slot = static_cast<uint8_t>(i)
                    });
                clearPendingPlayerState(i);
                invalidatePlayerSlot(i);
                s_deathConfirmCount[i] = 0;
                continue;
            }
            s_zeroPawnSinceUs[i] = 0;

            // Death is an exposure boundary, not a transient-read boundary.
            // The first complete health/life-state sample must make the slot
            // non-renderable and non-targetable immediately.  Keep a minimal
            // same-pawn tombstone until hierarchy changes so the renderer can
            // never revive the previous alive snapshot through interpolation
            // or its one-tick fallback path.
            if (authoritativeDeadThisFrame) {
                const bool wasRenderableAlive =
                    s_players[i].valid &&
                    s_players[i].pawn == pawns[i] &&
                    s_players[i].health > 0;

                esp::PlayerData deadTombstone = {};
                deadTombstone.valid = true;
                deadTombstone.pawn = pawns[i];
                deadTombstone.pawnHandle = pawnHandles[i];
                deadTombstone.health = 0;
                deadTombstone.team = teamValid ? teams[i] : 0;
                deadTombstone.coreUpdatedAtUs = nowUs;
                s_players[i] = deadTombstone;
                s_committedPlayerControllers[i] = controllers[i];

                refreshed[i] = true;
                clearPendingPlayerState(i);
                s_lastAliveCoreReadUs[i] = 0;
                s_playerInvalidReadStreak[i] = 0;
                s_deathConfirmCount[i] = 0;

                if (wasRenderableAlive) {
                    RecordEspEvent({
                        .type = EspEventType::SlotEvictedDead,
                        .slot = static_cast<uint8_t>(i)
                    });
                }
                continue;
            }
            if (localTeamLikelySwitched && !teamLiveResolved) {
                if (s_players[i].valid && s_players[i].pawn == pawns[i]) {
                    ++coreHeldSlots;
                    if (s_coreInvalidSinceUs[i] == 0)
                        s_coreInvalidSinceUs[i] = nowUs;
                    ++s_playerInvalidReadStreak[i];
                    refreshed[i] = true;
                    continue;
                }
            }
            if (coreLooksInvalid) {
                const bool missingFreshCore = !coreFreshThisFrame;
                const uint64_t coreStaleHoldUs =
                    esp::data::SelectCoreStaleHoldUs(
                        sceneSettling,
                        recentStructuralReset,
                        inBulkRecovery);
                const bool canTemporarilyHoldAliveCore =
                    esp::data::CanTemporarilyHoldAliveCore({
                        missingFreshCore,
                        pawnChangedThisFrame,
                        s_players[i].valid,
                        s_players[i].pawn,
                        pawns[i],
                        s_lastAliveCoreReadUs[i],
                        nowUs,
                        coreStaleHoldUs,
                    });
                if (canTemporarilyHoldAliveCore) {
                    ++coreHeldSlots;
                    if (s_coreInvalidSinceUs[i] == 0)
                        s_coreInvalidSinceUs[i] = nowUs;
                    ++s_playerInvalidReadStreak[i];
                    refreshed[i] = true;
                    continue;
                }

                const bool looksDeadThisFrame =
                    structuralLiveEvidence &&
                    coreDeadThisFrame &&
                    (healths[i] <= 0 || lifeStates[i] != 0);
                if (looksDeadThisFrame) {
                    if (s_deadReadPawn[i] != pawns[i]) {
                        s_deadReadPawn[i] = pawns[i];
                        s_deadReadSinceUs[i] = nowUs;
                    } else if (s_deadReadSinceUs[i] == 0) {
                        s_deadReadSinceUs[i] = nowUs;
                    }
                    ++s_deathConfirmCount[i];
                } else {
                    s_deathConfirmCount[i] = 0;
                    s_deadReadSinceUs[i] = 0;
                    s_deadReadPawn[i] = 0;
                }
                const bool confirmedDead =
                    esp::data::IsDeathConfirmed(
                        looksDeadThisFrame,
                        s_deathConfirmCount[i],
                        s_deadReadSinceUs[i],
                        nowUs);
                if (confirmedDead && s_players[i].valid && s_players[i].pawn == pawns[i]) {
                    s_players[i].health = 0;
                    s_players[i].hasBones = false;
                    s_players[i].bonesUpdatedAtUs = 0;
                    s_players[i].hasHitboxes = false;
                    s_players[i].hitboxCount = 0;
                    s_players[i].hitboxesUpdatedAtUs = 0;
                    memset(s_players[i].bones, 0, sizeof(s_players[i].bones));
                    memset(s_players[i].hitboxes, 0, sizeof(s_players[i].hitboxes));
                }
                if (!confirmedDead &&
                    s_players[i].valid &&
                    s_players[i].pawn != 0) {
                    if (s_coreInvalidSinceUs[i] == 0)
                        s_coreInvalidSinceUs[i] = nowUs;
                    const uint64_t invalidGraceUs =
                        esp::data::SelectCoreInvalidGraceUs(
                            looksDeadThisFrame,
                            pawnChangedThisFrame,
                            coreStaleHoldUs);
                    if (esp::data::IsWithinGraceWindow(s_coreInvalidSinceUs[i], nowUs, invalidGraceUs)) {
                        ++coreHeldSlots;
                        if (!sceneSettling || recentStructuralReset)
                            ++s_playerInvalidReadStreak[i];
                        refreshed[i] = true;
                        continue;
                    }
                }
                if (s_players[i].valid)
                    RecordEspEvent(
                        {
                            .type = confirmedDead
                                ? EspEventType::SlotEvictedDead
                                : EspEventType::SlotEvictedFallback,
                            .slot = static_cast<uint8_t>(i)
                        });
                clearPendingPlayerState(i);
                invalidatePlayerSlot(i);
                s_deathConfirmCount[i] = 0;
                continue;
            }
            clearPendingPlayerState(i);
            s_deathConfirmCount[i] = 0;
            s_playerInvalidReadStreak[i] = 0;
            refreshed[i] = true;
            esp::PlayerData& p = s_players[i];
            const bool hadTrackedPlayerNow = p.valid || p.pawn != 0;
            const bool pawnChanged = !p.valid || p.pawn != pawns[i];
            const int previousHealth = p.health;
            const Vector3 previousPosition = p.position;
            if (pawnChanged) {
                p.money = 0;
                p.ping = 0;
                p.visible = false;
                p.visibilityUpdatedAtUs = 0;
                p.scoped = false;
                p.defusing = false;
                p.hasDefuser = false;
                p.flashed = false;
                p.flashDuration = 0.0f;
                p.eyeYaw = 0.0f;
                memset(p.name, 0, sizeof(p.name));
                p.weaponId = 0;
                p.ammoClip = -1;
                p.hasBomb = false;
                p.grenadeCount = 0;
                memset(p.grenadeIds, 0, sizeof(p.grenadeIds));
                p.hasBones = false;
                p.bonesUpdatedAtUs = 0;
                p.hasHitboxes = false;
                p.hitboxCount = 0;
                p.hitboxesUpdatedAtUs = 0;
                p.coreUpdatedAtUs = 0;
                memset(p.bones, 0, sizeof(p.bones));
                memset(p.hitboxes, 0, sizeof(p.hitboxes));
            }
            const bool respawnedThisFrame =
                hadTrackedPlayerNow &&
                !pawnChanged &&
                previousHealth <= 0 &&
                healths[i] > 0 &&
                lifeStates[i] == 0;
            const float positionJump2D =
                (!pawnChanged && isValidWorldPos(previousPosition) && positionValid)
                ? static_cast<float>(std::hypot(
                    positions[i].x - previousPosition.x,
                    positions[i].y - previousPosition.y))
                : 0.0f;
            const bool likelyRoundRespawnTeleport =
                hadTrackedPlayerNow &&
                !pawnChanged &&
                previousHealth > 0 &&
                healths[i] > 0 &&
                lifeStates[i] == 0 &&
                positionJump2D >= esp::data::kPlayerCommitRespawnTeleportDistance2D;
            if (respawnedThisFrame || likelyRoundRespawnTeleport) {
                p.hasBones = false;
                p.bonesUpdatedAtUs = 0;
                p.hasHitboxes = false;
                p.hitboxCount = 0;
                p.hitboxesUpdatedAtUs = 0;
                memset(p.bones, 0, sizeof(p.bones));
                memset(p.hitboxes, 0, sizeof(p.hitboxes));
            }
            p.valid = true;
            p.pawn = pawns[i];
            p.pawnHandle = pawnHandles[i];
            s_committedPlayerControllers[i] = controllers[i];
            p.staleFrames = 0;
            p.health = healths[i];
            p.armor = std::clamp(armors[i], 0, 100);
            p.team = teams[i];
            p.position = positions[i];
            p.coreUpdatedAtUs = nowUs;
            p.gunGameImmunityValid = gunGameImmunityReadFresh[i];
            p.gunGameImmunity =
                p.gunGameImmunityValid &&
                gunGameImmunityFlags[i] == 1u;
            p.gunGameImmunityUpdatedAtUs =
                p.gunGameImmunityValid ? nowUs : 0u;
            const uint64_t currentSpottedMask =
                esp::data::BuildSpottedMask(spottedMasks[i][0], spottedMasks[i][1]);
            const auto visibilityState =
                esp::data::ResolveVisibilityFromSpotted({
                    spottedReadFresh[i],
                    currentSpottedMask,
                    i == localCrosshairTargetSlot,
                    localMaskResolved,
                    localMaskBit,
                    localMaskSlotBit,
                    localHandleSlotBit,
                    localControllerMaskBit,
                });
            if (visibilityState.hasFreshState) {
                p.visible = visibilityState.visible;
                p.visibilityUpdatedAtUs = nowUs;
            }
            if (visibilityState.hasFreshState)
                ++visibilityFreshSlots;
            if (visibilityState.hasFreshState && currentSpottedMask != 0ULL)
                ++visibilityMaskSlots;
            if (p.visible)
                ++visibilityVisibleSlots;
            p.velocityValid = velocityReadFresh[i];
            p.velocity = p.velocityValid ? velocities[i] : Vector3{};
            velocities[i] = p.velocity;
        }

        for (int firstSlot = 0; firstSlot < 64; ++firstSlot) {
            if (!s_players[firstSlot].valid ||
                s_players[firstSlot].pawn == 0) {
                continue;
            }

            for (int secondSlot = firstSlot + 1; secondSlot < 64; ++secondSlot) {
                if (!s_players[secondSlot].valid ||
                    s_players[secondSlot].pawn == 0) {
                    continue;
                }

                auto makeCommittedIdentity = [&](int slot) {
                    const bool currentHierarchy =
                        controllers[slot] != 0 &&
                        controllers[slot] == s_committedPlayerControllers[slot] &&
                        pawns[slot] != 0 &&
                        pawns[slot] == s_players[slot].pawn;
                    const bool freshCore =
                        currentHierarchy &&
                        coreReadFresh[slot] &&
                        coreReadPlausible[slot] &&
                        coreReadAlive[slot];
                    return esp::data::CommittedPlayerIdentityCandidate{
                        .slot = slot,
                        .controller = s_committedPlayerControllers[slot],
                        .pawn = s_players[slot].pawn,
                        .freshCore = freshCore,
                        .currentHierarchy = currentHierarchy,
                    };
                };

                const auto firstIdentity = makeCommittedIdentity(firstSlot);
                const auto secondIdentity = makeCommittedIdentity(secondSlot);
                if (!esp::data::IsSameCommittedPlayerIdentity(
                        firstIdentity,
                        secondIdentity)) {
                    continue;
                }

                const bool preferSecond =
                    esp::data::PreferSecondCommittedPlayerIdentity(
                        firstIdentity,
                        secondIdentity);
                const int rejectedSlot = preferSecond ? firstSlot : secondSlot;
                const int retainedSlot = preferSecond ? secondSlot : firstSlot;
                RecordEspEvent({
                    .type = EspEventType::SlotEvictedHierarchy,
                    .slot = static_cast<uint8_t>(rejectedSlot),
                    .param = static_cast<uint16_t>(retainedSlot + 1),
                });
                invalidatePlayerSlot(rejectedSlot);
                s_deathConfirmCount[rejectedSlot] = 0;
                if (rejectedSlot == firstSlot)
                    break;
            }
        }

        s_visibilityFreshSlots.store(visibilityFreshSlots, std::memory_order_relaxed);
        s_visibilityVisibleSlots.store(visibilityVisibleSlots, std::memory_order_relaxed);
        s_visibilityMaskSlots.store(visibilityMaskSlots, std::memory_order_relaxed);
        s_visibilityCrosshairSlot.store(localCrosshairTargetSlot, std::memory_order_relaxed);
        s_visibilityCrosshairValid.store(
            localCrosshairReadValid,
            std::memory_order_relaxed);
        s_visibilityLocalMaskResolved.store(localMaskResolved, std::memory_order_relaxed);
        s_visibilityLastCommitUs.store(nowUs, std::memory_order_relaxed);
        s_playerZeroPawnHeldSlotCount.store(zeroPawnHeldSlots, std::memory_order_relaxed);
        s_playerCoreHeldSlotCount.store(coreHeldSlots, std::memory_order_relaxed);
