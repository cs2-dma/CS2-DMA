    
    bool worldScanFoundC4 = false;
    uintptr_t worldScanC4Entity = 0;
    Vector3 worldScanC4Pos = {};
    int worldScanC4OwnerIdx = -1;
    bool worldScanC4OwnerAlive = false;
    bool worldScanC4OwnerNearby = false;
    bool worldScanC4NoOwner = false;
    bool worldScanC4Dormant = false;
    bool worldScanC4ReadIncomplete = false;
    uint32_t worldScanC4DropTick = 0;
    int worldScanC4Score = (std::numeric_limits<int>::min)();
    const int worldSignOnState = s_engineSignOnState.load(std::memory_order_relaxed);
    const bool liveWorldContext =
        s_engineInGame.load(std::memory_order_relaxed) &&
        !s_engineMenu.load(std::memory_order_relaxed) &&
        worldSignOnState == 6;

    const bool kDebugWorldUtility =
        narrowDebug.Enabled(esp::diagnostics::kNarrowDebugWorld);
    const bool wantsDroppedItemMarkers = wantsEspItem;
    
    
    
    
    
    const bool wantsWorldProjectiles = wantsEspWorld && wantsEspWorldProjectiles;
    const bool wantsWorldUtilityMarkers = wantsEspWorld || webRadarDemandActive;
    const bool wantsWorldUtilityData = wantsWorldUtilityMarkers || wantsWorldProjectiles;
    const bool wantsWorldIdentityData =
        wantsWorldUtilityData || wantsDroppedItemMarkers;
    const bool wantsBombConsumers = wantsEspBombInfo || wantsRadarShowBomb || webRadarDemandActive;
    const bool wantsWorldOwnerHandles =
        wantsWorldProjectiles ||
        wantsDroppedItemMarkers ||
        wantsBombConsumers;
    bool worldScanAttempted = false;
    bool worldScanOk = false;
    uint64_t worldMarkerReadGapHolds = 0;
    uint64_t worldPositionReadMisses = 0;
    static int s_worldBombCandidateSlots[8] = {};
    static uint8_t s_worldBombCandidateSlotCount = 0;

    #include "world_parts/world_domain_scheduler.inl"
    if (shouldScanWorld && entityList && ofs.CGameSceneNode_m_vecAbsOrigin > 0 && highestEntityIndex >= esp::data::kFirstWorldEntitySlot) {
        static bool s_warnedSmokeTickOffset = false;
        static bool s_warnedSmokeActiveOffset = false;
        static bool s_warnedInfernoTickOffset = false;
        static bool s_warnedDecoyTickOffset = false;
        static bool s_warnedExplodeTickOffset = false;
        if (ofs.C_SmokeGrenadeProjectile_m_nSmokeEffectTickBegin <= 0 && !s_warnedSmokeTickOffset) {
            s_warnedSmokeTickOffset = true;
            logUpdateDataIssue("scatter_20", "missing_offset_smoke_tick_begin");
        }
        if (ofs.C_SmokeGrenadeProjectile_m_bDidSmokeEffect <= 0 && !s_warnedSmokeActiveOffset) {
            s_warnedSmokeActiveOffset = true;
            logUpdateDataIssue("scatter_20", "missing_offset_smoke_active_flag");
        }
        if (ofs.C_Inferno_m_nFireEffectTickBegin <= 0 && !s_warnedInfernoTickOffset) {
            s_warnedInfernoTickOffset = true;
            logUpdateDataIssue("scatter_20", "missing_offset_inferno_tick_begin");
        }
        if (ofs.C_DecoyProjectile_m_nDecoyShotTick <= 0 && !s_warnedDecoyTickOffset) {
            s_warnedDecoyTickOffset = true;
            logUpdateDataIssue("scatter_20", "missing_offset_decoy_tick_begin");
        }
        if (ofs.C_BaseCSGrenadeProjectile_m_nExplodeEffectTickBegin <= 0 && !s_warnedExplodeTickOffset) {
            s_warnedExplodeTickOffset = true;
            logUpdateDataIssue("scatter_20", "missing_offset_explode_tick_begin");
        }

        #include "world_parts/world_read_context.inl"

        
        const bool shouldReadWorldUtilityDetails =
            worldDomainActiveUtilityDue &&
            (!heavyWorldCadence || (nowUs - s_lastWorldUtilityDetailScanUs) >= effectiveWorldUtilityDetailIntervalUs);
        const bool shouldReadWorldIdentityProbeDetails =
            (worldDomainActiveUtilityDue || worldDomainDroppedItemsDue) &&
            (!heavyWorldCadence ||
             (nowUs - s_lastWorldUtilityProbeScanUs) >=
                 effectiveWorldUtilityProbeIntervalUs);

        
        
        
        
        
        static uintptr_t s_cachedWorldSceneNodes[kMaxTrackedWorldEntities + 1] = {};
        static uint32_t s_cachedWorldOwnerHandles[kMaxTrackedWorldEntities + 1] = {};
        static uint8_t s_cachedWorldOwnerEvidence[kMaxTrackedWorldEntities + 1] = {};
        static uint64_t s_cachedWorldOwnerSampleUs[kMaxTrackedWorldEntities + 1] = {};
        static Vector3 s_cachedWorldPositions[kMaxTrackedWorldEntities + 1] = {};
        static uint64_t s_cachedWorldPositionSampleUs[kMaxTrackedWorldEntities + 1] = {};
        static uint64_t s_worldIdentityRetryUs[kMaxTrackedWorldEntities + 1] = {};
        static uint32_t s_worldIdentityValidationCursor = 0;
        static uint64_t s_worldDetailCacheResetSerial = 0;
        {
            const uint64_t detailResetSerial = s_worldDataCacheEpoch;
            if (s_worldDetailCacheResetSerial != detailResetSerial) {
                s_worldDetailCacheResetSerial = detailResetSerial;
                memset(s_cachedWorldSceneNodes, 0, sizeof(s_cachedWorldSceneNodes));
                memset(s_cachedWorldOwnerHandles, 0, sizeof(s_cachedWorldOwnerHandles));
                memset(s_cachedWorldOwnerEvidence, 0, sizeof(s_cachedWorldOwnerEvidence));
                memset(s_cachedWorldOwnerSampleUs, 0, sizeof(s_cachedWorldOwnerSampleUs));
                memset(s_cachedWorldPositions, 0, sizeof(s_cachedWorldPositions));
                memset(s_cachedWorldPositionSampleUs, 0, sizeof(s_cachedWorldPositionSampleUs));
                memset(s_worldIdentityRetryUs, 0, sizeof(s_worldIdentityRetryUs));
                s_worldIdentityValidationCursor = 0;
            }
        }

        auto rememberEntityClass = [&](uint32_t subclassId, esp::data::WorldEntityClass entityClass) {
            if (subclassId == 0u || entityClass == esp::data::WorldEntityClass::Unknown)
                return;
            switch (entityClass) {
            case esp::data::WorldEntityClass::SmokeProjectile:
                rememberTrackedSubclass(s_worldSmokeSubclassIds, subclassId);
                break;
            case esp::data::WorldEntityClass::Inferno:
                rememberTrackedSubclass(s_worldInfernoSubclassIds, subclassId);
                break;
            case esp::data::WorldEntityClass::DecoyProjectile:
                rememberTrackedSubclass(s_worldDecoySubclassIds, subclassId);
                break;
            case esp::data::WorldEntityClass::HeProjectile:
                rememberTrackedSubclass(s_worldHeSubclassIds, subclassId);
                break;
            case esp::data::WorldEntityClass::MolotovProjectile:
                rememberTrackedSubclass(s_worldMolotovSubclassIds, subclassId);
                break;
            default:
                break;
            }
        };

        auto queueSmokeStateReads = [&](int idx, uintptr_t ent) {
            if (ofs.C_SmokeGrenadeProjectile_m_nSmokeEffectTickBegin > 0)
                mem.AddScatterReadRequest(handle, ent + ofs.C_SmokeGrenadeProjectile_m_nSmokeEffectTickBegin, &worldSmokeTick[idx], sizeof(int), &worldSmokeTickReadBytes[idx]);
            if (ofs.C_SmokeGrenadeProjectile_m_bDidSmokeEffect > 0)
                mem.AddScatterReadRequest(handle, ent + ofs.C_SmokeGrenadeProjectile_m_bDidSmokeEffect, &worldSmokeActive[idx], sizeof(uint8_t), &worldSmokeActiveReadBytes[idx]);
            if (ofs.C_SmokeGrenadeProjectile_m_bSmokeVolumeDataReceived > 0)
                mem.AddScatterReadRequest(handle, ent + ofs.C_SmokeGrenadeProjectile_m_bSmokeVolumeDataReceived, &worldSmokeVolumeDataReceived[idx], sizeof(uint8_t), &worldSmokeVolumeReadBytes[idx]);
            if (ofs.C_SmokeGrenadeProjectile_m_bSmokeEffectSpawned > 0)
                mem.AddScatterReadRequest(handle, ent + ofs.C_SmokeGrenadeProjectile_m_bSmokeEffectSpawned, &worldSmokeEffectSpawned[idx], sizeof(uint8_t), &worldSmokeSpawnedReadBytes[idx]);
        };
        auto queueInfernoStateReads = [&](int idx, uintptr_t ent) {
            if (ofs.C_Inferno_m_nFireEffectTickBegin > 0)
                mem.AddScatterReadRequest(handle, ent + ofs.C_Inferno_m_nFireEffectTickBegin, &worldInfernoTick[idx], sizeof(int), &worldInfernoTickReadBytes[idx]);
            if (ofs.C_Inferno_m_nFireLifetime > 0)
                mem.AddScatterReadRequest(handle, ent + ofs.C_Inferno_m_nFireLifetime, &worldInfernoLife[idx], sizeof(float), &worldInfernoLifeReadBytes[idx]);
            if (ofs.C_Inferno_m_fireCount > 0)
                mem.AddScatterReadRequest(handle, ent + ofs.C_Inferno_m_fireCount, &worldInfernoFireCount[idx], sizeof(int), &worldInfernoFireCountReadBytes[idx]);
            if (ofs.C_Inferno_m_bInPostEffectTime > 0)
                mem.AddScatterReadRequest(handle, ent + ofs.C_Inferno_m_bInPostEffectTime, &worldInfernoInPostEffect[idx], sizeof(uint8_t), &worldInfernoPostEffectReadBytes[idx]);
        };
        auto queueDecoyStateReads = [&](int idx, uintptr_t ent) {
            if (ofs.C_DecoyProjectile_m_nDecoyShotTick > 0)
                mem.AddScatterReadRequest(handle, ent + ofs.C_DecoyProjectile_m_nDecoyShotTick, &worldDecoyTick[idx], sizeof(int), &worldDecoyTickReadBytes[idx]);
            if (ofs.C_DecoyProjectile_m_nClientLastKnownDecoyShotTick > 0)
                mem.AddScatterReadRequest(handle, ent + ofs.C_DecoyProjectile_m_nClientLastKnownDecoyShotTick, &worldDecoyClientTick[idx], sizeof(int), &worldDecoyClientTickReadBytes[idx]);
        };
        auto queueExplosionStateRead = [&](int idx, uintptr_t ent) {
            if (ofs.C_BaseCSGrenadeProjectile_m_nExplodeEffectTickBegin > 0)
                mem.AddScatterReadRequest(handle, ent + ofs.C_BaseCSGrenadeProjectile_m_nExplodeEffectTickBegin, &worldExplodeTick[idx], sizeof(int), &worldExplodeTickReadBytes[idx]);
        };

        auto queueUtilityReads = [&](int idx, uintptr_t ent, uint16_t itemId, uint32_t subclassId, uint8_t classKind) {
            const auto exactClass = static_cast<esp::data::WorldEntityClass>(classKind);
            if (exactClass == esp::data::WorldEntityClass::DroppedWeapon)
                return;
            const bool isSmokeGrenade = (itemId == 45);
            const bool isMolotovGrenade = (itemId == 46);
            const bool isIncendiaryGrenade = (itemId == 48);
            const bool isInfernoGrenade = isMolotovGrenade || isIncendiaryGrenade;
            const bool isDecoyGrenade = (itemId == 47);
            const bool isHeGrenade = (itemId == 44);
            const bool knownSmokeSubclass =
                exactClass == esp::data::WorldEntityClass::SmokeProjectile ||
                hasTrackedSubclass(s_worldSmokeSubclassIds, subclassId);
            const bool knownDecoySubclass =
                exactClass == esp::data::WorldEntityClass::DecoyProjectile ||
                hasTrackedSubclass(s_worldDecoySubclassIds, subclassId);
            const bool knownHeSubclass =
                exactClass == esp::data::WorldEntityClass::HeProjectile ||
                hasTrackedSubclass(s_worldHeSubclassIds, subclassId);
            const bool knownMolotovProjectileSubclass =
                exactClass == esp::data::WorldEntityClass::MolotovProjectile ||
                hasTrackedSubclass(s_worldMolotovSubclassIds, subclassId);

            if (isSmokeGrenade || knownSmokeSubclass) {
                queueSmokeStateReads(idx, ent);
                if (ofs.C_BaseEntity_m_vecVelocity > 0 && wantsWorldProjectiles)
                    mem.AddScatterReadRequest(handle, ent + ofs.C_BaseEntity_m_vecVelocity, &worldVelocities[idx], sizeof(Vector3), &worldVelocityReadBytes[idx]);
            }
            if (ofs.C_BaseEntity_m_vecVelocity > 0 &&
                wantsWorldProjectiles &&
                (isInfernoGrenade || knownMolotovProjectileSubclass))
                mem.AddScatterReadRequest(handle, ent + ofs.C_BaseEntity_m_vecVelocity, &worldVelocities[idx], sizeof(Vector3), &worldVelocityReadBytes[idx]);
            if (isDecoyGrenade || knownDecoySubclass) {
                queueDecoyStateReads(idx, ent);
                if (ofs.C_BaseEntity_m_vecVelocity > 0 && wantsWorldProjectiles)
                    mem.AddScatterReadRequest(handle, ent + ofs.C_BaseEntity_m_vecVelocity, &worldVelocities[idx], sizeof(Vector3), &worldVelocityReadBytes[idx]);
            }
            if (isHeGrenade || knownHeSubclass)
                queueExplosionStateRead(idx, ent);
        };

        
        int cachedIdentityRefreshCount = 0;
        int cachedIdentityRetryCount = 0;
        int cachedUnknownIdentityRefreshCount = 0;
        constexpr int kMaxWorldIdentityRefreshesPerScan = 12;
        constexpr uint64_t kUnknownIdentityRetryIntervalUs = 1000000u;
        constexpr uint64_t kKnownIdentityValidationIntervalUs = 250000u;

        
        
        static thread_local uint8_t s_positionReadAttemptedMask[kMaxTrackedWorldEntities + 1];
        static thread_local uint8_t s_identityRefreshMask[kMaxTrackedWorldEntities + 1];
        std::memset(s_positionReadAttemptedMask, 0, sizeof(s_positionReadAttemptedMask));
        std::memset(s_identityRefreshMask, 0, sizeof(s_identityRefreshMask));

        
        
        
        
        
        {
            
            for (int b = 0; b < blockCount; ++b) {
                mem.AddScatterReadRequest(
                    handle,
                    entityList + 0x10 + static_cast<uintptr_t>(b) * 8u,
                    &worldBlocks[b],
                    sizeof(uintptr_t),
                    &worldBlockReadBytes[b]);
            }

            
            for (int candidateIdx = 0; candidateIdx < worldCandidateCount; ++candidateIdx) {
                const int idx = s_worldCandidateIndices[candidateIdx];
                const int blockIdx = idx >> 9;
                if (blockIdx < 0 || blockIdx >= s_cachedWorldBlockCount)
                    continue;
                const uintptr_t cachedBlock = s_cachedWorldBlocks[blockIdx];
                if (!cachedBlock)
                    continue;
                mem.AddScatterReadRequest(
                    handle,
                    cachedBlock + s_activeEntitySlotSize * (static_cast<uint32_t>(idx) & kEntitySlotMask),
                    &worldEntities[idx],
                    sizeof(uintptr_t),
                    &worldEntityReadBytes[idx]);
            }

            
            
            
            const int cachedSlotCount = worldLimit - esp::data::kFirstWorldEntitySlot + 1;
            const int cachedReadStart = static_cast<int>(
                s_worldIdentityValidationCursor % static_cast<uint32_t>(cachedSlotCount));
            for (int candidateOrdinal = 0; candidateOrdinal < cachedSlotCount; ++candidateOrdinal) {
                const int idx = esp::data::kFirstWorldEntitySlot +
                    (cachedReadStart + candidateOrdinal) % cachedSlotCount;
                if (s_worldCandidateMask[idx] == 0u)
                    continue;
                const uintptr_t cachedEnt = s_worldEntityRefs[idx];
                if (!cachedEnt)
                    continue;
                const uintptr_t cachedSceneNode = s_cachedWorldSceneNodes[idx];
                const auto cachedNodeClass = static_cast<esp::data::WorldEntityClass>(s_worldEntityClassKinds[idx]);
                if (!cachedSceneNode && cachedNodeClass != esp::data::WorldEntityClass::Inferno &&
                    cachedNodeClass != esp::data::WorldEntityClass::SmokeProjectile)
                    continue;
                const uint16_t cachedItemId = s_worldEntityItemIds[idx];
                const uint32_t cachedSubclassId = s_worldEntitySubclassIds[idx];
                const uint8_t cachedClassKind = s_worldEntityClassKinds[idx];
                
                const auto cachedExactClass = static_cast<esp::data::WorldEntityClass>(cachedClassKind);
                const bool cachedKnownSmokeSubclass =
                    cachedExactClass == esp::data::WorldEntityClass::SmokeProjectile ||
                    hasTrackedSubclass(s_worldSmokeSubclassIds, cachedSubclassId);
                const bool cachedKnownDecoySubclass =
                    cachedExactClass == esp::data::WorldEntityClass::DecoyProjectile ||
                    hasTrackedSubclass(s_worldDecoySubclassIds, cachedSubclassId);
                const bool cachedKnownHeSubclass =
                    cachedExactClass == esp::data::WorldEntityClass::HeProjectile ||
                    hasTrackedSubclass(s_worldHeSubclassIds, cachedSubclassId);
                const bool cachedKnownInfernoSubclass =
                    cachedExactClass == esp::data::WorldEntityClass::Inferno ||
                    hasTrackedSubclass(s_worldInfernoSubclassIds, cachedSubclassId);
                const bool cachedKnownMolotovProjectileSubclass =
                    cachedExactClass == esp::data::WorldEntityClass::MolotovProjectile ||
                    hasTrackedSubclass(s_worldMolotovSubclassIds, cachedSubclassId);
                const bool cachedBombCandidate =
                    cachedItemId == kWeaponC4Id &&
                    worldDomainBombRescueDue;
                const bool cachedDroppedItemCandidate =
                    worldDomainDroppedItemsDue &&
                    esp::data::IsDroppedWorldItem(cachedItemId, cachedExactClass);
                const bool cachedUtilityCandidate =
                    worldDomainActiveUtilityDue &&
                    (isUtilityWorldItemId(cachedItemId) ||
                     cachedKnownSmokeSubclass ||
                     cachedKnownDecoySubclass ||
                     cachedKnownHeSubclass ||
                     cachedKnownInfernoSubclass ||
                     cachedKnownMolotovProjectileSubclass ||
                     s_worldUtilityHasHistory[idx]);
                const bool cachedIdentityRetryDue =
                    esp::data::ShouldRetryWorldIdentity(
                        shouldReadWorldIdentityProbeDetails,
                        cachedItemId,
                        static_cast<esp::data::WorldEntityClass>(cachedClassKind),
                        wantsWorldIdentityData,
                        s_worldIdentityRetryUs[idx],
                        nowUs,
                        kUnknownIdentityRetryIntervalUs) ||
                    ((worldDomainDroppedItemsDue || worldDomainActiveUtilityDue || worldDomainSlowDiscoveryDue) &&
                     cachedExactClass != esp::data::WorldEntityClass::Unknown &&
                     (s_worldIdentityRetryUs[idx] == 0u || nowUs < s_worldIdentityRetryUs[idx] ||
                      nowUs - s_worldIdentityRetryUs[idx] >= kKnownIdentityValidationIntervalUs));
                const bool cachedIdentityRefreshCandidate =
                    cachedIdentityRetryDue &&
                    cachedIdentityRetryCount <
                        kMaxWorldIdentityRefreshesPerScan;
                if (!(cachedBombCandidate ||
                      cachedDroppedItemCandidate ||
                      cachedUtilityCandidate ||
                      cachedIdentityRefreshCandidate))
                    continue;

                
                if (ofs.CGameSceneNode_m_vecAbsOrigin > 0 && isLikelyGamePointer(cachedSceneNode)) {
                    mem.AddScatterReadRequest(
                        handle,
                        cachedSceneNode + ofs.CGameSceneNode_m_vecAbsOrigin,
                        &worldPositions[idx],
                        sizeof(Vector3),
                        &worldPositionReadBytes[idx]);
                    s_positionReadAttemptedMask[idx] = 1u;
                }
                // Revalidate cached coordinate owners in the existing batch. A
                // stable entity address does not guarantee a stable scene node.
                if ((cachedBombCandidate || cachedDroppedItemCandidate ||
                     (cachedUtilityCandidate && shouldReadWorldUtilityDetails)) &&
                    ofs.C_BaseEntity_m_pGameSceneNode > 0) {
                    worldSceneNodeRefreshRequested[idx] = 1u;
                    mem.AddScatterReadRequest(
                        handle,
                        cachedEnt + ofs.C_BaseEntity_m_pGameSceneNode,
                        &worldSceneNodes[idx],
                        sizeof(uintptr_t),
                        &worldSceneNodeReadBytes[idx]);
                }
                if (cachedBombCandidate && ofs.CGameSceneNode_m_bDormant > 0) {
                    mem.AddScatterReadRequest(
                        handle,
                        cachedSceneNode + ofs.CGameSceneNode_m_bDormant,
                        &worldDormantFlags[idx],
                        sizeof(uint8_t),
                        &worldDormantReadBytes[idx]);
                }
                if ((cachedDroppedItemCandidate || needsWorldOwnerRefreshForItemId(cachedItemId) ||
                     (wantsWorldProjectiles && cachedUtilityCandidate)) &&
                    ofs.C_BaseEntity_m_hOwnerEntity > 0) {
                    mem.AddScatterReadRequest(
                        handle,
                        cachedEnt + ofs.C_BaseEntity_m_hOwnerEntity,
                        &worldOwnerHandles[idx],
                        sizeof(uint32_t),
                        &worldOwnerReadBytes[idx]);
                }
                if (cachedBombCandidate && ofs.C_CSWeaponBase_m_nDropTick > 0) {
                    mem.AddScatterReadRequest(
                        handle,
                        cachedEnt + ofs.C_CSWeaponBase_m_nDropTick,
                        &worldDropTicks[idx],
                        sizeof(uint32_t),
                        &worldDropTickReadBytes[idx]);
                }
                if (cachedBombCandidate || cachedDroppedItemCandidate) {
                    mem.AddScatterReadRequest(
                        handle,
                        cachedSceneNode + 0x10u,
                        &worldTransformPositions[idx],
                        sizeof(Vector3),
                        &worldTransformPositionReadBytes[idx]);
                }

                
                if (shouldReadWorldUtilityDetails) {
                    const bool isUtility =
                        isUtilityWorldItemId(cachedItemId) ||
                        cachedKnownSmokeSubclass ||
                        cachedKnownDecoySubclass ||
                        cachedKnownHeSubclass ||
                        cachedKnownInfernoSubclass ||
                        cachedKnownMolotovProjectileSubclass;
                    if (isUtility)
                        queueUtilityReads(idx, cachedEnt, cachedItemId, cachedSubclassId, cachedClassKind);
                }

                
                if (cachedIdentityRefreshCandidate) {
                    s_identityRefreshMask[idx] = 1u;
                    s_worldIdentityRetryUs[idx] = nowUs;
                    ++cachedIdentityRetryCount;
                    if (cachedExactClass == esp::data::WorldEntityClass::Unknown)
                        ++cachedUnknownIdentityRefreshCount;
                    if (worldDomainDroppedItemsDue || worldDomainSlowDiscoveryDue)
                        s_worldIdentityValidationCursor = static_cast<uint32_t>(
                            (idx + 1 - esp::data::kFirstWorldEntitySlot) % cachedSlotCount);
                    if (ofs.C_BaseEntity_m_nSubclassID > 0) {
                        mem.AddScatterReadRequest(
                            handle,
                            cachedEnt + ofs.C_BaseEntity_m_nSubclassID,
                            &worldSubclassIds[idx],
                            sizeof(uint32_t),
                            &worldSubclassReadBytes[idx]);
                    }
                    if ((cachedExactClass == esp::data::WorldEntityClass::Unknown ||
                         cachedExactClass == esp::data::WorldEntityClass::DroppedWeapon) &&
                        ofs.C_EconEntity_m_AttributeManager > 0 &&
                        ofs.C_AttributeContainer_m_Item > 0 &&
                        ofs.C_EconItemView_m_iItemDefinitionIndex > 0) {
                        const uintptr_t itemDefAddress =
                            cachedEnt + ofs.C_EconEntity_m_AttributeManager +
                            ofs.C_AttributeContainer_m_Item +
                            ofs.C_EconItemView_m_iItemDefinitionIndex;
                        mem.AddScatterReadRequest(
                            handle,
                            itemDefAddress,
                            &worldItemDefs[idx],
                            sizeof(uint16_t),
                            &worldItemDefReadBytes[idx]);
                    }
                    if (ofs.CEntityInstance_m_pEntity > 0) {
                        mem.AddScatterReadRequest(
                            handle,
                            cachedEnt + ofs.CEntityInstance_m_pEntity,
                            &worldIdentities[idx],
                            sizeof(uintptr_t),
                            &worldIdentityReadBytes[idx]);
                        ++cachedIdentityRefreshCount;
                    }
                }
            }

            
            worldScanAttempted = true;
            const bool worldBatchOk = executeOptionalScatterRead();
            uint32_t completedWorldBlockReads = 0;
            for (int b = 0; b < blockCount; ++b) {
                if (esp::data::IsWorldFieldReadComplete(
                        worldBlockReadBytes[b],
                        sizeof(uintptr_t))) {
                    ++completedWorldBlockReads;
                }
            }
            worldScanOk = esp::data::HasUsableWorldStructuralReads(
                worldBatchOk,
                completedWorldBlockReads);

            if (worldScanOk) {
                
                bool blocksChanged = false;
                for (int b = 0; b < blockCount; ++b) {
                    const bool blockReadComplete =
                        app::memory_address::IsCompleteGamePointerSample(
                            worldBlocks[b], worldBlockReadBytes[b]);
                    if (!blockReadComplete) {
                        worldBlockReadBytes[b] = 0;
                        worldBlocks[b] = s_cachedWorldBlocks[b];
                    } else if (!isLikelyGamePointer(worldBlocks[b])) {
                        worldBlocks[b] = 0;
                        if (s_cachedWorldBlocks[b] != 0)
                            blocksChanged = true;
                    } else if (worldBlocks[b] != s_cachedWorldBlocks[b]) {
                        blocksChanged = true;
                    }
                }

                if (blocksChanged) {
                    
                    
                    for (int b = 0; b < blockCount; ++b)
                        s_cachedWorldBlocks[b] = worldBlocks[b];
                    s_cachedWorldBlockCount = blockCount;

                    
                    std::memset(s_positionReadAttemptedMask, 0, sizeof(s_positionReadAttemptedMask));
                    std::memset(worldDormantFlags, 0, sizeof(worldDormantFlags));
                    for (int candidateIdx = 0; candidateIdx < worldCandidateCount; ++candidateIdx) {
                        const int idx = s_worldCandidateIndices[candidateIdx];
                        resetWorldScratchSlot(idx);
                    }

                    
                    for (int candidateIdx = 0; candidateIdx < worldCandidateCount; ++candidateIdx) {
                        const int idx = s_worldCandidateIndices[candidateIdx];
                        const int blockIdx = idx >> 9;
                        if (blockIdx < 0 || blockIdx >= blockCount)
                            continue;
                        if (!worldBlocks[blockIdx])
                            continue;
                        mem.AddScatterReadRequest(
                            handle,
                            worldBlocks[blockIdx] + s_activeEntitySlotSize * (static_cast<uint32_t>(idx) & kEntitySlotMask),
                            &worldEntities[idx],
                            sizeof(uintptr_t),
                            &worldEntityReadBytes[idx]);
                    }
                    const bool entityRefreshBatchOk = executeOptionalScatterRead();
                    uint32_t completedEntityReads = 0;
                    for (int candidateIdx = 0; candidateIdx < worldCandidateCount; ++candidateIdx) {
                        const int idx = s_worldCandidateIndices[candidateIdx];
                        if (esp::data::IsWorldFieldReadComplete(
                                worldEntityReadBytes[idx],
                                sizeof(uintptr_t))) {
                            ++completedEntityReads;
                        }
                    }
                    worldScanOk = esp::data::HasUsableWorldStructuralReads(
                        entityRefreshBatchOk,
                        completedEntityReads);
                } else {
                    
                    for (int b = 0; b < blockCount; ++b)
                        s_cachedWorldBlocks[b] = worldBlocks[b];
                    s_cachedWorldBlockCount = blockCount;
                }

                
                if (shouldReadWorldUtilityDetails)
                    s_lastWorldUtilityDetailScanUs = nowUs;

            }
        }






        #include "world_parts/world_read_identity.inl"


        for (int candidateIdx = 0; candidateIdx < worldCandidateCount; ++candidateIdx) {
            const int idx = s_worldCandidateIndices[candidateIdx];
            worldItemDefs[idx] = esp::data::CanonicalWorldItemId(worldItemDefs[idx],
                static_cast<esp::data::WorldEntityClass>(worldClassKinds[idx]));
        }

        struct InfernoPoseRead {
            Vector3 points[64] = {};
            std::array<esp::data::WorldVectorReadChunk, 3> chunks{};
            DWORD bytes[3]{};
            int slot = 0;
        };
        static thread_local InfernoPoseRead infernoPoses[esp::data::kMaxUtilityEffectMarkers];
        int infernoPoseCount = 0;
        if (worldScanOk && shouldReadWorldUtilityDetails) {
            int smokePoseCount = 0;
            for (int candidateIdx = 0; candidateIdx < worldCandidateCount; ++candidateIdx) {
                const int idx = s_worldCandidateIndices[candidateIdx];
                if (!worldEntityReadComplete[idx] || !worldEntities[idx] ||
                    worldClassKinds[idx] != static_cast<uint8_t>(esp::data::WorldEntityClass::SmokeProjectile))
                    continue;
                if (worldSmokeActiveReadBytes[idx] == 0u || worldSmokeTickReadBytes[idx] == 0u)
                    queueSmokeStateReads(idx, worldEntities[idx]);
                if (ofs.C_SmokeGrenadeProjectile_m_vSmokeDetonationPos > 0) {
                    mem.AddScatterReadRequest(handle, worldEntities[idx] + ofs.C_SmokeGrenadeProjectile_m_vSmokeDetonationPos,
                        &worldSmokeOrigin[idx], sizeof(Vector3), &worldSmokeOriginReadBytes[idx]);
                    s_positionReadAttemptedMask[idx] = 1u;
                }
                ++smokePoseCount;
            }
            for (int candidateIdx = 0; candidateIdx < worldCandidateCount; ++candidateIdx) {
                const int idx = s_worldCandidateIndices[candidateIdx];
                if (!worldEntityReadComplete[idx] || !worldEntities[idx] ||
                    worldClassKinds[idx] != static_cast<uint8_t>(esp::data::WorldEntityClass::Inferno))
                    continue;
                if (infernoPoseCount >= static_cast<int>(std::size(infernoPoses)))
                    break;
                auto& pose = infernoPoses[infernoPoseCount++];
                pose = {};
                pose.slot = idx;
                queueInfernoStateReads(idx, worldEntities[idx]);
                if (isLikelyGamePointer(worldSceneNodes[idx]) && ofs.CGameSceneNode_m_vecAbsOrigin > 0) {
                    mem.AddScatterReadRequest(handle, worldSceneNodes[idx] + ofs.CGameSceneNode_m_vecAbsOrigin,
                        &worldPositions[idx], sizeof(Vector3), &worldPositionReadBytes[idx]);
                    mem.AddScatterReadRequest(handle, worldSceneNodes[idx] + 0x10u,
                        &worldTransformPositions[idx], sizeof(Vector3), &worldTransformPositionReadBytes[idx]);
                    s_positionReadAttemptedMask[idx] = 1u;
                }
                if (ofs.C_Inferno_m_firePositions > 0) {
                    const auto address = worldEntities[idx] + ofs.C_Inferno_m_firePositions;
                    pose.chunks = esp::data::PlanWorldVectorReads<Vector3, 64>(address);
                    if (pose.chunks[0].count != 0) s_positionReadAttemptedMask[idx] = 1u;
                    for (size_t c = 0; c < pose.chunks.size(); ++c) {
                        const auto chunk = pose.chunks[c];
                        if (chunk.count != 0)
                            mem.AddScatterReadRequest(handle, address + chunk.first * sizeof(Vector3),
                                pose.points + chunk.first, chunk.count * sizeof(Vector3), &pose.bytes[c]);
                    }
                }
            }
            if (infernoPoseCount > 0 || smokePoseCount > 0) {
                executeOptionalScatterRead();
                for (int candidateIdx = 0; candidateIdx < worldCandidateCount; ++candidateIdx) {
                    const int idx = s_worldCandidateIndices[candidateIdx];
                    Vector3 origin = {};
                    if (worldEntityReadComplete[idx] && worldEntities[idx] &&
                        worldClassKinds[idx] == static_cast<uint8_t>(esp::data::WorldEntityClass::SmokeProjectile) &&
                        esp::data::ResolveSmokeOrigin(worldSmokeOrigin[idx], worldSmokeOriginReadBytes[idx],
                            worldSmokeActive[idx], worldSmokeActiveReadBytes[idx],
                            worldSmokeEffectSpawned[idx], worldSmokeSpawnedReadBytes[idx], origin, isValidWorldPos))
                        worldBasicDetailsComplete[idx] = 1u;
                }
                for (int p = 0; p < infernoPoseCount; ++p) {
                    const auto& pose = infernoPoses[p];
                    const int idx = pose.slot;
                    const bool countComplete = worldInfernoFireCountReadBytes[idx] == sizeof(int);
                    if (countComplete && worldInfernoFireCount[idx] >= 0 && worldInfernoFireCount[idx] <= 64)
                        worldBasicDetailsComplete[idx] = 1u;
                    uint64_t validSamples = 0;
                    for (size_t c = 0; c < pose.chunks.size(); ++c) {
                        const auto chunk = pose.chunks[c];
                        if (pose.bytes[c] != chunk.count * sizeof(Vector3)) continue;
                        for (size_t i = 0; i < chunk.count; ++i)
                            validSamples |= uint64_t{1} << (chunk.first + i);
                    }
                    if (esp::data::ResolveInfernoSampledOrigin(pose.points, validSamples, worldInfernoFireCount[idx],
                            countComplete, worldInfernoOrigin[idx], isValidWorldPos)) {
                        worldInfernoOriginReadBytes[idx] = sizeof(Vector3);
                    }
                }
                int anchorRetryCount = 0;
                for (int p = 0; p < infernoPoseCount; ++p) {
                    const auto& pose = infernoPoses[p];
                    const int idx = pose.slot;
                    if (worldInfernoOriginReadBytes[idx] != 0 || ofs.C_Inferno_m_firePositions <= 0 ||
                        worldInfernoFireCountReadBytes[idx] != sizeof(int) || worldInfernoFireCount[idx] <= 0 ||
                        worldInfernoFireCount[idx] > 64) continue;
                    if ((worldPositionReadBytes[idx] == sizeof(Vector3) && isValidWorldPos(worldPositions[idx])) ||
                        (worldTransformPositionReadBytes[idx] == sizeof(Vector3) && isValidWorldPos(worldTransformPositions[idx]))) continue;
                    bool incomplete = false;
                    for (size_t c = 0; c < pose.chunks.size(); ++c)
                        incomplete = incomplete || pose.bytes[c] != pose.chunks[c].count * sizeof(Vector3);
                    if (!incomplete) continue;
                    mem.AddScatterReadRequest(handle, worldEntities[idx] + ofs.C_Inferno_m_firePositions,
                        &worldInfernoOrigin[idx], sizeof(Vector3), &worldInfernoOriginReadBytes[idx]);
                    ++anchorRetryCount;
                }
                if (anchorRetryCount != 0) executeOptionalScatterRead();
            }
        }

        if (worldScanOk && dynamicRefreshCount > 0) {
            for (int candidateIdx = 0; candidateIdx < worldCandidateCount; ++candidateIdx) {
                const int idx = s_worldCandidateIndices[candidateIdx];
                const uintptr_t ent = worldEntities[idx];
                if (!ent) continue;
                if (worldClassKinds[idx] == static_cast<uint8_t>(esp::data::WorldEntityClass::Inferno))
                    continue;
                const bool entityUnchanged = (ent == s_worldEntityRefs[idx]) && s_cachedWorldSceneNodes[idx];
                if (entityUnchanged && worldSceneNodeChanged[idx] == 0u && worldDetailsReadQueued[idx] == 0u) continue;
                if (!worldBasicDetailsComplete[idx]) continue;
                if (!worldSceneNodes[idx]) continue;
                worldPositions[idx] = {};
                worldPositionReadBytes[idx] = 0;
                s_positionReadAttemptedMask[idx] = 0u;
                if (ofs.CGameSceneNode_m_vecAbsOrigin > 0 &&
                    (!bombOnlyWorldMode || worldItemDefs[idx] == kWeaponC4Id || s_worldEntityItemIds[idx] == kWeaponC4Id)) {
                    mem.AddScatterReadRequest(
                        handle,
                        worldSceneNodes[idx] + ofs.CGameSceneNode_m_vecAbsOrigin,
                        &worldPositions[idx],
                        sizeof(Vector3),
                        &worldPositionReadBytes[idx]);
                    s_positionReadAttemptedMask[idx] = 1u;
                }
                if ((worldItemDefs[idx] == kWeaponC4Id || s_worldEntityItemIds[idx] == kWeaponC4Id) &&
                    ofs.CGameSceneNode_m_bDormant > 0) {
                    worldDormantReadBytes[idx] = 0;
                    mem.AddScatterReadRequest(
                        handle,
                        worldSceneNodes[idx] + ofs.CGameSceneNode_m_bDormant,
                        &worldDormantFlags[idx],
                        sizeof(uint8_t),
                        &worldDormantReadBytes[idx]);
                }
                if (worldItemDefs[idx] == kWeaponC4Id ||
                    s_worldEntityItemIds[idx] == kWeaponC4Id ||
                    esp::data::IsDroppedWorldItem(worldItemDefs[idx],
                        static_cast<esp::data::WorldEntityClass>(worldClassKinds[idx]))) {
                    if ((worldItemDefs[idx] == kWeaponC4Id || s_worldEntityItemIds[idx] == kWeaponC4Id) &&
                        ofs.C_CSWeaponBase_m_nDropTick > 0) {
                        worldDropTickReadBytes[idx] = 0;
                        mem.AddScatterReadRequest(
                            handle,
                            ent + ofs.C_CSWeaponBase_m_nDropTick,
                            &worldDropTicks[idx],
                            sizeof(uint32_t),
                            &worldDropTickReadBytes[idx]);
                    }
                    worldTransformPositionReadBytes[idx] = 0;
                    mem.AddScatterReadRequest(
                        handle,
                        worldSceneNodes[idx] + 0x10u,
                        &worldTransformPositions[idx],
                        sizeof(Vector3),
                        &worldTransformPositionReadBytes[idx]);
                }
                if (shouldReadWorldUtilityDetails)
                    queueUtilityReads(idx, ent, worldItemDefs[idx], worldSubclassIds[idx], worldClassKinds[idx]);
            }
            executeOptionalScatterRead();
        }

        if (worldScanOk && (newEntityCount > 0 || cachedIdentityRefreshCount > 0 || dynamicRefreshCount > 0)) {
            struct WorldProof {
                int slot = 0;
                uintptr_t entity = 0, node = 0, identity = 0;
                uint32_t owner = 0;
                DWORD entityBytes = 0, nodeBytes = 0, identityBytes = 0, ownerBytes = 0;
            };
            static thread_local WorldProof proofs[kMaxTrackedWorldEntities + 1];
            std::array<uintptr_t, kMaxTrackedWorldBlocks> proofBlocks{};
            std::array<DWORD, kMaxTrackedWorldBlocks> proofBlockBytes{};
            std::array<bool, kMaxTrackedWorldBlocks> proofBlockQueued{};
            int proofCount = 0;
            for (int c = 0; c < worldCandidateCount; ++c) {
                const int idx = s_worldCandidateIndices[c];
                const auto kind = static_cast<esp::data::WorldEntityClass>(worldClassKinds[idx]);
                if (!worldEntityReadComplete[idx] || !worldEntities[idx] || !worldBasicDetailsComplete[idx] ||
                    !(worldItemDefs[idx] == kWeaponC4Id || esp::data::IsDroppedWorldItem(worldItemDefs[idx], kind) ||
                      esp::data::IsWorldUtilityClass(kind))) continue;
                const int b = idx >> 9;
                if (b >= blockCount || !isLikelyGamePointer(worldBlocks[b])) continue;
                auto& proof = proofs[proofCount++];
                proof = {};
                proof.slot = idx;
                if (!proofBlockQueued[b]) {
                    proofBlockQueued[b] = true;
                    mem.AddScatterReadRequest(handle, entityList + 0x10u + static_cast<uintptr_t>(b) * 8u,
                        &proofBlocks[b], sizeof(uintptr_t), &proofBlockBytes[b]);
                }
                mem.AddScatterReadRequest(handle, worldBlocks[b] + s_activeEntitySlotSize * (idx & kEntitySlotMask),
                    &proof.entity, sizeof(uintptr_t), &proof.entityBytes);
                mem.AddScatterReadRequest(handle, worldEntities[idx] + ofs.C_BaseEntity_m_pGameSceneNode,
                    &proof.node, sizeof(uintptr_t), &proof.nodeBytes);
                if (worldIdentityReadBytes[idx] == sizeof(uintptr_t) && isLikelyGamePointer(worldIdentities[idx]))
                    mem.AddScatterReadRequest(handle, worldEntities[idx] + ofs.CEntityInstance_m_pEntity,
                        &proof.identity, sizeof(uintptr_t), &proof.identityBytes);
                if ((kind == esp::data::WorldEntityClass::DroppedWeapon || worldItemDefs[idx] == kWeaponC4Id) &&
                    ofs.C_BaseEntity_m_hOwnerEntity > 0)
                    mem.AddScatterReadRequest(handle, worldEntities[idx] + ofs.C_BaseEntity_m_hOwnerEntity,
                        &proof.owner, sizeof(uint32_t), &proof.ownerBytes);
            }
            if (proofCount != 0) executeOptionalScatterRead();
            for (int p = 0; p < proofCount; ++p) {
                const auto& proof = proofs[p];
                const int idx = proof.slot, b = idx >> 9;
                const bool blockComplete = app::memory_address::IsCompleteGamePointerSample(proofBlocks[b], proofBlockBytes[b]);
                const bool entityComplete = app::memory_address::IsCompleteGamePointerSample(proof.entity, proof.entityBytes);
                if (blockComplete && proofBlocks[b] == 0) {
                    worldEntities[idx] = 0;
                    worldBasicDetailsComplete[idx] = 0;
                    continue;
                }
                if (!blockComplete || proofBlocks[b] != worldBlocks[b] || !entityComplete) {
                    worldEntityReadComplete[idx] = 0;
                    worldBasicDetailsComplete[idx] = 0;
                    continue;
                }
                if (proof.entity != worldEntities[idx]) {
                    worldEntities[idx] = proof.entity;
                    worldBasicDetailsComplete[idx] = 0;
                    continue;
                }
                if (worldIdentityReadBytes[idx] == sizeof(uintptr_t) && isLikelyGamePointer(worldIdentities[idx]) &&
                    (proof.identityBytes != sizeof(uintptr_t) || proof.identity != worldIdentities[idx])) {
                    worldBasicDetailsComplete[idx] = 0;
                    if (app::memory_address::IsCompleteGamePointerSample(proof.identity, proof.identityBytes))
                        worldEntities[idx] = 0;
                    continue;
                }
                if (!app::memory_address::IsCompleteGamePointerSample(proof.node, proof.nodeBytes) ||
                    proof.node != worldSceneNodes[idx]) {
                    worldBasicDetailsComplete[idx] = 0;
                    if (app::memory_address::IsCompleteGamePointerSample(proof.node, proof.nodeBytes))
                        s_cachedWorldSceneNodes[idx] = proof.node;
                    continue;
                }
                if (proof.ownerBytes == sizeof(uint32_t)) {
                    worldOwnerHandles[idx] = proof.owner;
                    worldOwnerReadBytes[idx] = proof.ownerBytes;
                } else if ((static_cast<esp::data::WorldEntityClass>(worldClassKinds[idx]) ==
                        esp::data::WorldEntityClass::DroppedWeapon || worldItemDefs[idx] == kWeaponC4Id) &&
                    ofs.C_BaseEntity_m_hOwnerEntity > 0) {
                    worldBasicDetailsComplete[idx] = 0;
                }
            }
        }

        for (int candidateIdx = 0; candidateIdx < worldCandidateCount; ++candidateIdx) {
            const int idx = s_worldCandidateIndices[candidateIdx];
            if (!worldEntityReadComplete[idx])
                continue;
            s_worldEntityChangedFlags[idx] = static_cast<uint8_t>(worldEntities[idx] != s_worldEntityRefs[idx]);
            if (s_worldEntityChangedFlags[idx] != 0u) {
                s_cachedWorldSceneNodes[idx] = 0;
                s_cachedWorldPositions[idx] = {};
                s_cachedWorldPositionSampleUs[idx] = 0;
                s_cachedWorldOwnerHandles[idx] = 0;
                s_cachedWorldOwnerEvidence[idx] =
                    static_cast<uint8_t>(esp::data::WorldOwnerEvidence::Unknown);
                s_cachedWorldOwnerSampleUs[idx] = 0;
            }
            if (worldEntities[idx] &&
                worldBasicDetailsComplete[idx] &&
                (worldSceneNodes[idx] ||
                    worldClassKinds[idx] == static_cast<uint8_t>(esp::data::WorldEntityClass::Inferno) ||
                    worldClassKinds[idx] == static_cast<uint8_t>(esp::data::WorldEntityClass::SmokeProjectile))) {
                s_cachedWorldSceneNodes[idx] = worldSceneNodes[idx];
                if (worldClassKinds[idx] != s_worldEntityClassKinds[idx]) {
                    s_cachedWorldPositions[idx] = {};
                    s_cachedWorldPositionSampleUs[idx] = 0;
                    ResetWorldUtilityTrackingSlot(idx);
                }
                const auto refreshedClass = static_cast<esp::data::WorldEntityClass>(worldClassKinds[idx]);
                const bool effectStartChanged =
                    (refreshedClass == esp::data::WorldEntityClass::SmokeProjectile &&
                     worldSmokeTickReadBytes[idx] == sizeof(int) && s_worldPrevSmokeTick[idx] >= 4 &&
                     worldSmokeTick[idx] >= 4 && worldSmokeTick[idx] < 20000000 &&
                     worldSmokeTick[idx] != s_worldPrevSmokeTick[idx]) ||
                    (refreshedClass == esp::data::WorldEntityClass::Inferno &&
                     worldInfernoTickReadBytes[idx] == sizeof(int) && s_worldPrevInfernoTick[idx] >= 4 &&
                     worldInfernoTick[idx] >= 4 && worldInfernoTick[idx] < 20000000 &&
                     worldInfernoTick[idx] != s_worldPrevInfernoTick[idx]);
                if (effectStartChanged) {
                    s_cachedWorldPositions[idx] = {};
                    s_cachedWorldPositionSampleUs[idx] = 0;
                    ResetWorldUtilityTrackingSlot(idx);
                }
                s_worldEntityClassKinds[idx] = worldClassKinds[idx];
                const bool ownerWasRead =
                    esp::data::IsWorldFieldReadComplete(
                        worldOwnerReadBytes[idx],
                        sizeof(uint32_t));
                if (ownerWasRead) {
                    s_cachedWorldOwnerHandles[idx] = worldOwnerHandles[idx];
                    s_cachedWorldOwnerEvidence[idx] = static_cast<uint8_t>(
                        esp::data::ResolveWorldOwnerEvidence(
                            ofs.C_BaseEntity_m_hOwnerEntity > 0,
                            true,
                            worldOwnerHandles[idx]));
                    s_cachedWorldOwnerSampleUs[idx] = nowUs;
                }
                if (worldClassKinds[idx] == static_cast<uint8_t>(esp::data::WorldEntityClass::Inferno) &&
                    worldInfernoOriginReadBytes[idx] == sizeof(Vector3) &&
                    isValidWorldPos(worldInfernoOrigin[idx])) {
                    worldPositions[idx] = worldInfernoOrigin[idx];
                    worldPositionReadBytes[idx] = sizeof(Vector3);
                }
                if (worldClassKinds[idx] == static_cast<uint8_t>(esp::data::WorldEntityClass::SmokeProjectile) &&
                    esp::data::ResolveSmokeOrigin(worldSmokeOrigin[idx], worldSmokeOriginReadBytes[idx],
                        worldSmokeActive[idx], worldSmokeActiveReadBytes[idx],
                        worldSmokeEffectSpawned[idx], worldSmokeSpawnedReadBytes[idx], worldPositions[idx], isValidWorldPos))
                    worldPositionReadBytes[idx] = sizeof(Vector3);
                if ((esp::data::IsDroppedWorldItem(worldItemDefs[idx],
                        static_cast<esp::data::WorldEntityClass>(worldClassKinds[idx])) ||
                     worldClassKinds[idx] == static_cast<uint8_t>(esp::data::WorldEntityClass::Inferno)) &&
                    (worldPositionReadBytes[idx] != sizeof(Vector3) || !isValidWorldPos(worldPositions[idx])) &&
                    worldTransformPositionReadBytes[idx] == sizeof(Vector3) &&
                    isValidWorldPos(worldTransformPositions[idx])) {
                    worldPositions[idx] = worldTransformPositions[idx];
                    worldPositionReadBytes[idx] = sizeof(Vector3);
                }
                if (esp::data::IsWorldFieldReadComplete(
                        worldPositionReadBytes[idx],
                        sizeof(Vector3)) &&
                    isValidWorldPos(worldPositions[idx])) {
                    s_cachedWorldPositions[idx] = worldPositions[idx];
                    s_cachedWorldPositionSampleUs[idx] = nowUs;
                } else {
                    // Invalid full reads are not fresh position evidence either.
                    if (esp::data::ShouldCountWorldPositionReadMiss(
                            s_positionReadAttemptedMask[idx] != 0u,
                            esp::data::IsWorldFieldReadComplete(
                                worldPositionReadBytes[idx], sizeof(Vector3)),
                            isValidWorldPos(worldPositions[idx])))
                        ++worldPositionReadMisses;
                    worldPositionReadBytes[idx] = 0;
                    const auto effectClass = static_cast<esp::data::WorldEntityClass>(worldClassKinds[idx]);
                    const bool confirmedEffectAnchor =
                        (effectClass == esp::data::WorldEntityClass::SmokeProjectile &&
                         s_worldSmokeLatched[idx] && s_worldUtilityDeadlinesUs[idx][0] > nowUs) ||
                        (effectClass == esp::data::WorldEntityClass::Inferno &&
                         s_worldInfernoLatched[idx] && s_worldUtilityDeadlinesUs[idx][1] > nowUs);
                    worldPositions[idx] = (confirmedEffectAnchor || esp::data::IsWorldMarkerSourceFresh(
                        s_cachedWorldPositionSampleUs[idx], nowUs))
                            ? s_cachedWorldPositions[idx] : Vector3{};
                }
            } else if (!worldEntities[idx]) {
                s_cachedWorldSceneNodes[idx] = 0;
                s_cachedWorldOwnerHandles[idx] = 0;
                s_cachedWorldOwnerEvidence[idx] =
                    static_cast<uint8_t>(esp::data::WorldOwnerEvidence::Unknown);
                s_cachedWorldOwnerSampleUs[idx] = 0;
                s_cachedWorldPositions[idx] = {};
                s_cachedWorldPositionSampleUs[idx] = 0;
                s_worldEntityClassKinds[idx] = 0;
                worldDormantFlags[idx] = 0;
            }
            const bool c4Candidate =
                worldItemDefs[idx] == kWeaponC4Id ||
                s_worldEntityItemIds[idx] == kWeaponC4Id;
            if (c4Candidate && worldDomainBombRescueDue) {
                const bool positionComplete =
                    esp::data::IsWorldFieldReadComplete(
                        worldPositionReadBytes[idx],
                        sizeof(Vector3)) &&
                    isValidWorldPos(worldPositions[idx]);
                const bool ownerComplete =
                    ofs.C_BaseEntity_m_hOwnerEntity <= 0 ||
                    esp::data::IsWorldFieldReadComplete(
                        worldOwnerReadBytes[idx],
                        sizeof(uint32_t));
                const bool dropTickComplete =
                    ofs.C_CSWeaponBase_m_nDropTick <= 0 ||
                    esp::data::IsWorldFieldReadComplete(
                        worldDropTickReadBytes[idx],
                        sizeof(uint32_t));
                const bool dormantComplete =
                    ofs.CGameSceneNode_m_bDormant <= 0 ||
                    esp::data::IsWorldFieldReadComplete(
                        worldDormantReadBytes[idx],
                        sizeof(uint8_t));
                worldC4DynamicComplete[idx] =
                    worldBasicDetailsComplete[idx] &&
                    positionComplete &&
                    ownerComplete &&
                    dropTickComplete &&
                    dormantComplete
                        ? 1u
                        : 0u;
                if (!worldC4DynamicComplete[idx])
                    worldScanC4ReadIncomplete = true;
            }
        }
        int worldProcessCount = 0;
        for (int candidateIdx = 0; candidateIdx < worldCandidateCount; ++candidateIdx) {
            const int idx = s_worldCandidateIndices[candidateIdx];
            const bool hadTrackedState = shouldTrackWorldSlotForDueDomains(idx);
            if (!worldEntityReadComplete[idx])
                continue;
            if (!worldEntities[idx]) {
                if (hadTrackedState)
                    s_worldProcessIndices[worldProcessCount++] = idx;
                continue;
            }
            if (!worldBasicDetailsComplete[idx])
                continue;
            const uint16_t itemId = worldItemDefs[idx];
            const uint32_t subclassId = worldSubclassIds[idx];
            const bool featureRelevantEntity =
                (itemId == kWeaponC4Id && worldDomainBombRescueDue) ||
                (worldDomainDroppedItemsDue &&
                 esp::data::IsDroppedWorldItem(itemId,
                     static_cast<esp::data::WorldEntityClass>(worldClassKinds[idx]))) ||
                (worldDomainActiveUtilityDue &&
                 (isUtilityWorldItemId(itemId) ||
                  esp::data::IsWorldUtilityClass(
                      static_cast<esp::data::WorldEntityClass>(worldClassKinds[idx])) ||
                  hasTrackedSubclass(s_worldSmokeSubclassIds, subclassId) ||
                  hasTrackedSubclass(s_worldDecoySubclassIds, subclassId) ||
                  hasTrackedSubclass(s_worldHeSubclassIds, subclassId) ||
                  hasTrackedSubclass(s_worldInfernoSubclassIds, subclassId) ||
                  hasTrackedSubclass(s_worldMolotovSubclassIds, subclassId) ||
                  s_worldUtilityHasHistory[idx]));
            if (s_worldEntityChangedFlags[idx] != 0 ||
                hadTrackedState ||
                featureRelevantEntity) {
                s_worldProcessIndices[worldProcessCount++] = idx;
            }
        }
        static thread_local int s_worldUtilityProcessIndices[kMaxTrackedWorldEntities + 1];
        int worldUtilityProcessCount = 0;
        for (int processIdx = 0; processIdx < worldProcessCount; ++processIdx) {
            const int idx = s_worldProcessIndices[processIdx];
            if (!worldEntities[idx])
                continue;

            const uint16_t itemId = worldItemDefs[idx];
            const uint32_t subclassId = worldSubclassIds[idx];
            const bool utilityCandidate =
                worldDomainActiveUtilityDue &&
                (isUtilityWorldItemId(itemId) ||
                 esp::data::IsWorldUtilityClass(
                     static_cast<esp::data::WorldEntityClass>(worldClassKinds[idx])) ||
                 hasTrackedSubclass(s_worldSmokeSubclassIds, subclassId) ||
                 hasTrackedSubclass(s_worldDecoySubclassIds, subclassId) ||
                  hasTrackedSubclass(s_worldHeSubclassIds, subclassId) ||
                  hasTrackedSubclass(s_worldInfernoSubclassIds, subclassId) ||
                  hasTrackedSubclass(s_worldMolotovSubclassIds, subclassId) ||
                  hasTrackedWorldUtilityState(idx));
            if (utilityCandidate)
                s_worldUtilityProcessIndices[worldUtilityProcessCount++] = idx;
        }

        int worldClassifiedCandidateCount = 0;
        int worldIdentityPendingCount = 0;
        uint64_t worldReadCounts = 0;
        for (int candidateIdx = 0; candidateIdx < worldCandidateCount; ++candidateIdx) {
            const int idx = s_worldCandidateIndices[candidateIdx];
            if (!worldEntities[idx])
                continue;
            if (worldEntityReadComplete[idx])
                worldReadCounts += 1ull;
            if (worldBasicDetailsComplete[idx])
                worldReadCounts += 1ull << 16;
            if (worldIdentityReadBytes[idx] == sizeof(uintptr_t) && isLikelyGamePointer(worldIdentities[idx]))
                worldReadCounts += 1ull << 32;
            if (!esp::data::ReadWorldDesignerName(worldDesignerNames[idx], sizeof(worldDesignerNames[idx]), worldDesignerNameReadBytes[idx]).empty())
                worldReadCounts += 1ull << 48;
            if (worldClassKinds[idx] != static_cast<uint8_t>(esp::data::WorldEntityClass::Unknown)) {
                ++worldClassifiedCandidateCount;
            } else if (worldSubclassIds[idx] != 0u || worldItemDefs[idx] != 0u) {
                ++worldIdentityPendingCount;
            }
        }
        s_worldTrackedEntityCountStat.store(s_worldTrackedIndexCount, std::memory_order_relaxed);
        s_worldCandidateCountStat.store(worldCandidateCount, std::memory_order_relaxed);
        s_worldUtilityCandidateCountStat.store(worldUtilityProcessCount, std::memory_order_relaxed);
        s_worldClassifiedCandidateCountStat.store(worldClassifiedCandidateCount, std::memory_order_relaxed);
        s_worldIdentityPendingCountStat.store(worldIdentityPendingCount, std::memory_order_relaxed);
        s_worldReadCountsStat.store(worldReadCounts, std::memory_order_relaxed);
        s_worldReadDemandStat.store(
            (wantsEspWorld ? 1u : 0u) | (webRadarDemandActive ? 2u : 0u) |
            (wantsWorldUtilityData ? 4u : 0u) | (worldDomainActiveUtilityDue ? 8u : 0u) |
            (ofs.CEntityInstance_m_pEntity > 0 && ofs.CEntityIdentity_m_designerName > 0 ? 16u : 0u) |
            (ofs.C_SmokeGrenadeProjectile_m_vSmokeDetonationPos > 0 ? 32u : 0u) |
            (ofs.C_Inferno_m_firePositions > 0 ? 64u : 0u), std::memory_order_relaxed);

        std::bitset<kMaxTrackedWorldEntities + 1> worldProcessMask;
        for (int i = 0; i < worldProcessCount; ++i)
            worldProcessMask.set(static_cast<size_t>(s_worldProcessIndices[i]));
        #include "world_parts/world_marker_synthesis.inl"
        #include "world_parts/world_utility_probe.inl"
        #include "world_parts/world_process_entities.inl"
    } else if (kDebugWorldUtility) {
        static uint32_t s_dbgSkipScan = 0;
        ++s_dbgSkipScan;
        if ((s_dbgSkipScan % 24u) == 0u) {
            const bool modeEnabled = (wantsEspWorld || wantsEspBombInfo || wantsRadarShowBomb || webRadarDemandActive);
            const uint64_t sinceLast = nowUs - s_lastWorldScanUs;
            DmaLogPrintf(
                "[DEBUG] WorldESP skipped: mode=%d gate=%d since=%llu entityList=%d posOfs=%d highest=%d shouldScan=%d",
                modeEnabled ? 1 : 0,
                (sinceLast >= 120000) ? 1 : 0,
                static_cast<unsigned long long>(sinceLast),
                entityList ? 1 : 0,
                (ofs.CGameSceneNode_m_vecAbsOrigin > 0) ? 1 : 0,
                highestEntityIndex,
                shouldScanWorld ? 1 : 0);
        }
    }
    if (!liveWorldContext) {
        for (int i = 0; i < s_worldMarkerCount && i < 256; ++i)
            s_worldMarkers[i].valid = false;
        s_worldMarkerCount = 0;
        s_lastWorldScanUs = 0;
        s_worldTrackedEntityCountStat.store(0, std::memory_order_relaxed);
        s_worldCandidateCountStat.store(0, std::memory_order_relaxed);
        s_worldUtilityCandidateCountStat.store(0, std::memory_order_relaxed);
        s_worldClassifiedCandidateCountStat.store(0, std::memory_order_relaxed);
        s_worldIdentityPendingCountStat.store(0, std::memory_order_relaxed);
    }
    const bool worldSubsystemActive =
        liveWorldContext &&
        (wantsWorldUtilityMarkers ||
         wantsDroppedItemMarkers ||
         wantsBombConsumers);
    if (!worldSubsystemActive) {
        SetSubsystemUnknown(RuntimeSubsystem::World);
    } else if (!shouldScanWorld) {
        MarkSubsystemHealthy(RuntimeSubsystem::World, nowUs);
    } else if (worldScanAttempted && worldScanOk) {
        MarkSubsystemHealthy(RuntimeSubsystem::World, nowUs);
    } else if (worldScanAttempted) {
        MarkSubsystemDegraded(RuntimeSubsystem::World, nowUs);
    } else if (entityList && ofs.CGameSceneNode_m_vecAbsOrigin > 0) {
        MarkSubsystemDegraded(RuntimeSubsystem::World, nowUs);
    } else {
        SetSubsystemUnknown(RuntimeSubsystem::World);
    }
    uint16_t localWeaponIdResolved = s_localWeaponId;
