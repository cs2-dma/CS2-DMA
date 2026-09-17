        if (worldScanOk && worldPositionsOk) {
        worldScanCommitted = true;
        int unknownUtilityProbeCount = 0;
        static thread_local int16_t s_worldPawnOwnerBySlot[kMaxTrackedWorldEntities + 1];
        static thread_local int16_t s_worldControllerOwnerBySlot[65];
        const bool needsOwnerPlayerMaps =
            worldDomainDroppedItemsDue ||
            worldDomainBombRescueDue;
        if (needsOwnerPlayerMaps) {
            std::memset(s_worldPawnOwnerBySlot, 0xFF, sizeof(s_worldPawnOwnerBySlot));
            std::memset(s_worldControllerOwnerBySlot, 0xFF, sizeof(s_worldControllerOwnerBySlot));
            for (int resolvedIdx = 0; resolvedIdx < playerResolvedSlotCount; ++resolvedIdx) {
                const int playerIdx = playerResolvedSlots[resolvedIdx];
                const uint32_t pawnSlot = pawnHandles[playerIdx] & kEntityHandleMask;
                if (pawnSlot != 0u && pawnSlot <= kMaxTrackedWorldEntities)
                    s_worldPawnOwnerBySlot[pawnSlot] = static_cast<int16_t>(playerIdx);
                if (controllers[playerIdx])
                    s_worldControllerOwnerBySlot[playerIdx + 1] = static_cast<int16_t>(playerIdx);
            }
        }
        for (int processIdx = 0; processIdx < worldProcessCount; ++processIdx) {
            const int idx = s_worldProcessIndices[processIdx];
            const uintptr_t ent = worldEntities[idx];
            emittingWorldEntity = ent;
            emittingWorldSlot = idx;
            emittingWorldSampleUs = s_cachedWorldPositionSampleUs[idx];
            if (!ent) {
                if (isTrackedWorldEntitySlot(idx)) {
                    s_worldEntityRefs[idx] = 0;
                    s_worldEntitySubclassIds[idx] = 0;
                    s_worldEntityItemIds[idx] = 0;
                    s_worldEntityClassKinds[idx] = 0;
                    ResetWorldUtilityTrackingSlot(idx);
                    refreshTrackedWorldIndex(idx);
                }
                continue;
            }

            const uint16_t rawItemId = worldItemDefs[idx];
            Vector3 pos = worldPositions[idx];
            if (rawItemId == kWeaponC4Id &&
                esp::data::IsWorldFieldReadComplete(
                    worldTransformPositionReadBytes[idx],
                    sizeof(Vector3)) &&
                isValidWorldPos(worldTransformPositions[idx])) {
                const float dx = pos.x - worldTransformPositions[idx].x;
                const float dy = pos.y - worldTransformPositions[idx].y;
                const float dz = pos.z - worldTransformPositions[idx].z;
                if (!isValidWorldPos(pos) ||
                    (dx * dx + dy * dy + dz * dz) > (16.0f * 16.0f)) {
                    pos = worldTransformPositions[idx];
                    emittingWorldSampleUs = nowUs;
                }
            }
            if (!std::isfinite(pos.x) || !std::isfinite(pos.y) || !std::isfinite(pos.z))
                continue;

            const uint16_t itemId = normalizeWorldItemId(rawItemId);
            const uint32_t subclassId = worldSubclassIds[idx];
            const auto exactClass = static_cast<esp::data::WorldEntityClass>(worldClassKinds[idx]);
            if (s_worldEntityRefs[idx] != ent || s_worldEntityItemIds[idx] != itemId || s_worldEntitySubclassIds[idx] != subclassId) {
                s_worldEntityRefs[idx] = ent;
                s_worldEntityItemIds[idx] = itemId;
                s_worldEntitySubclassIds[idx] = subclassId;
                ResetWorldUtilityTrackingSlot(idx);
                refreshTrackedWorldIndex(idx);
            }

            const uint32_t owner = worldOwnerHandles[idx];
            esp::data::WorldOwnerEvidence ownerEvidence =
                static_cast<esp::data::WorldOwnerEvidence>(
                    s_cachedWorldOwnerEvidence[idx]);
            if (!esp::data::IsWorldOwnerEvidenceFresh(
                    ownerEvidence,
                    s_cachedWorldOwnerSampleUs[idx],
                    nowUs)) {
                ownerEvidence = esp::data::WorldOwnerEvidence::Unknown;
            }
            const bool noOwner =
                ownerEvidence == esp::data::WorldOwnerEvidence::Dropped;
            const bool hasOwner =
                ownerEvidence == esp::data::WorldOwnerEvidence::Held;
            const bool isHeGrenade = (itemId == 44);
            const bool isFlashGrenade = (itemId == 43);
            const bool isSmokeGrenade = (itemId == 45);
            const bool isMolotovGrenade = (itemId == 46);
            const bool isDecoyGrenade = (itemId == 47);
            const bool isIncendiaryGrenade = (itemId == 48);
            const bool isInfernoGrenade = isMolotovGrenade || isIncendiaryGrenade;
            const bool knownSmokeSubclass =
                exactClass == esp::data::WorldEntityClass::SmokeProjectile ||
                hasTrackedSubclass(s_worldSmokeSubclassIds, subclassId);
            const bool knownDecoySubclass =
                exactClass == esp::data::WorldEntityClass::DecoyProjectile ||
                hasTrackedSubclass(s_worldDecoySubclassIds, subclassId);
            const bool knownHeSubclass =
                exactClass == esp::data::WorldEntityClass::HeProjectile ||
                hasTrackedSubclass(s_worldHeSubclassIds, subclassId);
            const bool knownInfernoSubclass =
                exactClass == esp::data::WorldEntityClass::Inferno ||
                hasTrackedSubclass(s_worldInfernoSubclassIds, subclassId);
            const bool knownMolotovProjectileSubclass =
                exactClass == esp::data::WorldEntityClass::MolotovProjectile ||
                hasTrackedSubclass(s_worldMolotovSubclassIds, subclassId);
            const bool isUtilityGrenade = isFlashGrenade || isHeGrenade || isSmokeGrenade || isMolotovGrenade || isDecoyGrenade || isIncendiaryGrenade;
            const char* knownItemName = WeaponNameFromItemId(itemId);
            const bool knownDroppedItem =
                exactClass == esp::data::WorldEntityClass::DroppedWeapon &&
                knownItemName != nullptr &&
                !isUtilityGrenade &&
                itemId != 49 &&
                !IsKnifeItemId(itemId);
            const bool posNonOrigin = (std::fabs(pos.x) > 1.0f || std::fabs(pos.y) > 1.0f);
            const bool discoveryShardActive =
                (worldDiscoveryShardCount <= 1u) ||
                (static_cast<uint32_t>(idx - esp::data::kFirstWorldEntitySlot) % worldDiscoveryShardCount) == activeWorldDiscoveryShard;
            // Sharding throttles discovery, not already tracked objects. Applying
            // it to every refresh made existing item markers blink on dense maps.
            const bool droppedItemDiscoveryAllowed = esp::data::ShouldProcessDroppedItem(
                heavyWorldCadence, discoveryShardActive,
                s_worldEntityChangedFlags[idx] == 0u && s_worldTrackedIndexPos[idx] != 0u);
            const bool droppedItemCandidate =
                wantsDroppedItemMarkers &&
                !warmupWorldScan &&
                knownDroppedItem &&
                itemId < 1200 &&
                espItemEnabledMaskSnapshot.test(itemId) &&
                worldSceneNodes[idx] != 0 &&
                posNonOrigin &&
                droppedItemDiscoveryAllowed;
            const bool needsOwnerResolution = needsOwnerPlayerMaps && ((rawItemId == kWeaponC4Id) || droppedItemCandidate);
            int ownerPlayerIndex = -1;
            if (needsOwnerResolution && !noOwner) {
                if (rawItemId == kWeaponC4Id) {
                    ownerPlayerIndex = findPlayerIndexByEntityHandle(owner);
                } else {
                    const uint32_t ownerSlot = owner & kEntityHandleMask;
                    if (ownerSlot != 0u) {
                        if (ownerSlot <= kMaxTrackedWorldEntities && s_worldPawnOwnerBySlot[ownerSlot] >= 0)
                            ownerPlayerIndex = static_cast<int>(s_worldPawnOwnerBySlot[ownerSlot]);
                        else if (ownerSlot <= 64u && s_worldControllerOwnerBySlot[ownerSlot] >= 0)
                            ownerPlayerIndex = static_cast<int>(s_worldControllerOwnerBySlot[ownerSlot]);
                    }
                }
            }
            bool ownerHoldingNearby = false;
            if (ownerPlayerIndex >= 0 && ownerPlayerIndex < 64 &&
                healths[ownerPlayerIndex] > 0 &&
                lifeStates[ownerPlayerIndex] == 0 &&
                IsFiniteVec(positions[ownerPlayerIndex])) {
                const float ownerDx = pos.x - positions[ownerPlayerIndex].x;
                const float ownerDy = pos.y - positions[ownerPlayerIndex].y;
                const float ownerDz = std::fabs(pos.z - positions[ownerPlayerIndex].z);
                ownerHoldingNearby = ((ownerDx * ownerDx + ownerDy * ownerDy) <= (120.0f * 120.0f)) && ownerDz <= 96.0f;
            }
            
            
            
            const bool ownerActiveWeaponMatches =
                (ownerPlayerIndex >= 0 && ownerPlayerIndex < 64 && itemId != 0 &&
                 weaponIds[ownerPlayerIndex] == itemId);
            // Owner handle is authoritative. Player-map, distance and active
            // weapon checks are diagnostics only: they can lag behind a pickup
            // and must never turn an owned weapon into a dropped marker.
            const bool droppedOwnerReleased =
                ownerEvidence == esp::data::WorldOwnerEvidence::Dropped;

            #include "world_process_dropped_c4.inl"
            #include "world_process_utility.inl"
            // Re-evaluate after identity and utility evidence are committed.
            // A raw entity pointer or an arbitrary subclass ID is not enough
            // to keep a slot in the active world registry.
            refreshTrackedWorldIndex(idx);
        }
        }
        if (worldScanCommitted) {
            if (worldDomainBombRescueDue) {
                s_lastWorldBombRescueScanUs = nowUs;
                if (!worldScanFoundC4 && !worldScanC4ReadIncomplete)
                    clearWorldBombCandidateSlots();
            }
            if (worldDomainDroppedItemsDue)
                s_lastWorldDroppedItemsScanUs = nowUs;
            if (worldDomainActiveUtilityDue)
                s_lastWorldActiveUtilityScanUs = nowUs;
            if (worldDomainSlowDiscoveryDue)
                s_lastWorldSlowDiscoveryScanUs = nowUs;
            if (scannedMarkerCount == 0 && s_worldTrackedIndexCount == 0) {
                if (s_worldIdleScanStreak < 255u)
                    ++s_worldIdleScanStreak;
            } else {
                s_worldIdleScanStreak = 0;
            }
            // A rescue/refresh-only pass did not inspect the discovery shard.
            // Advancing here on every pass can permanently starve half of the
            // entity slots when the two cadences become phase-locked.
            s_worldDiscoveryShard = esp::data::NextWorldDiscoveryShard(
                activeWorldDiscoveryShard, worldDiscoveryShardCount, discoveryCandidatesDue);
            s_lastWorldScanUs = nowUs;
            s_lastWorldScanCommittedUs.store(nowUs, std::memory_order_relaxed);
        }

        if (kDebugWorldUtility) {
            static uint32_t s_dbgScan = 0;
            ++s_dbgScan;
            const bool anomaly = (dbgEvidenceEntities > 0 && (dbgSignalSmoke + dbgSignalInferno + dbgSignalDecoy + dbgSignalExplosive) == 0);
            if ((s_dbgScan % 12u) == 0u || anomaly) {
                DmaLogPrintf(
                    "[DEBUG] WorldESP: scan=%u time=%.2f interval=%.5f limit=%d markers=%d evidence=%d raw(S/I/D/E)=%d/%d/%d/%d signal=%d/%d/%d/%d timers(tick/effect/stationary/terminal)=%d/%d/%d/%d anomaly=%d",
                    s_dbgScan,
                    currentGameTime,
                    safeIntervalPerTick,
                    worldLimit,
                    scannedMarkerCount,
                    dbgEvidenceEntities,
                    dbgRawSmoke,
                    dbgRawInferno,
                    dbgRawDecoy,
                    dbgRawExplosive,
                    dbgSignalSmoke,
                    dbgSignalInferno,
                    dbgSignalDecoy,
                    dbgSignalExplosive,
                    dbgTimerTick,
                    dbgTimerEffect,
                    dbgTimerStationary,
                    dbgTimerTerminal,
                    anomaly ? 1 : 0);
                for (int i = 0; i < dbgNoSignalSamples; ++i) {
                    const DebugSample& s = dbgSamples[i];
                    DmaLogPrintf(
                        "[DEBUG] WorldESP sample: idx=%d item=%u owner=%u ticks(s/i/d/d2/e)=%d/%d/%d/%d/%d fire=%d active=%d rem(s/i/d/e)=%.2f/%.2f/%.2f/%.2f",
                        s.idx,
                        static_cast<unsigned>(s.itemId),
                        static_cast<unsigned>(s.owner),
                        s.smokeTick,
                        s.infernoTick,
                        s.decoyTick,
                        s.decoyClientTick,
                        s.explodeTick,
                        s.infernoFireCount,
                        s.smokeActive,
                        s.smokeRemaining,
                        s.infernoRemaining,
                        s.decoyRemaining,
                        s.explodeRemaining);
                }
            }
        }
