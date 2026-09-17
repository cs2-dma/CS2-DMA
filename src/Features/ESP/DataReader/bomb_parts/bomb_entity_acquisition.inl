    if (bombPlantedByRules) {
        uintptr_t plantedRootCandidate = sanitizePointer(plantedC4Entity);
        uintptr_t resolvedPlantedC4 = 0;
        if (plantedRootCandidate &&
            plantedRootCandidate == s_cachedPlantedRootCandidate &&
            s_cachedPlantedMetaEntity &&
            s_cachedPlantedMetaEntity != s_explodedEntityTaintPtr) {
            resolvedPlantedC4 = s_cachedPlantedMetaEntity;
        } else {
            resolvedPlantedC4 = resolvePlantedC4Entity(plantedRootCandidate);
        }
        if (!resolvedPlantedC4 && ofs.dwPlantedC4 > 0) {
            uintptr_t plantedRoot = 0;
            if (readPointer(g::clientBase + ofs.dwPlantedC4, &plantedRoot)) {
                plantedRootCandidate = plantedRoot;
                resolvedPlantedC4 = resolvePlantedC4Entity(plantedRoot);
            }
        }
        if (resolvedPlantedC4) {
            s_cachedPlantedRootCandidate = plantedRootCandidate;
            s_cachedPlantedRootSeenUs = nowUs;
        } else if (esp::data::ShouldHoldPlantedC4Entity(
                       bombPlantedByRules, plantedRootCandidate,
                       s_cachedPlantedRootCandidate, s_cachedPlantedMetaEntity,
                       s_cachedPlantedHasExploded != 0 || s_cachedPlantedBombDefused != 0,
                       s_cachedPlantedRootSeenUs, nowUs)) {
            // Keep reading the known entity during a short root-pointer gap.
            // Do not refresh rootSeenUs from this fallback or cross an epoch.
            resolvedPlantedC4 = s_cachedPlantedMetaEntity;
        }
        plantedC4Entity = resolvedPlantedC4;
    } else {
        plantedC4Entity = 0;
        s_cachedPlantedRootCandidate = 0;
    }

    static uint64_t s_lastWeaponC4ProbeUs = 0;
    static uint64_t s_weaponC4ProbeResetEpoch = 0;
    if (s_weaponC4ProbeResetEpoch != bombCacheEpoch) {
        s_weaponC4ProbeResetEpoch = bombCacheEpoch;
        s_lastWeaponC4ProbeUs = 0;
    }
    const bool cachedCarryEvidence =
        esp::data::IsCachedBombCarryEvidenceFresh(
            s_cachedBombCarryOwnerSlot,
            s_cachedBombCarryOwnerUs,
            nowUs);
    const bool cachedDroppedEntityResolved =
        bombDroppedByRules &&
        s_mergedWeaponEntity != 0 &&
        s_mergedWeaponEntityUs > 0 &&
        nowUs >= s_mergedWeaponEntityUs &&
        (nowUs - s_mergedWeaponEntityUs) <=
            esp::data::kWeaponC4EntityHoldUs;
    const bool canDeferWeaponC4Probe =
        esp::data::ShouldDeferWeaponC4Probe(
            bombPlantedByRules,
            bombDroppedByRules,
            cachedDroppedEntityResolved,
            inventoryC4CarrierEvidence,
            cachedCarryEvidence,
            s_bombState.planted,
            s_bombState.dropped);
    const bool weaponC4ProbeDue =
        esp::data::IsWeaponC4ProbeDue(canDeferWeaponC4Probe, s_lastWeaponC4ProbeUs, nowUs);

    bool weaponEntityFromHold = false;
    bool droppedEntityCacheFresh = false;
    if (!bombPlantedByRules) {
        uintptr_t weaponRootCandidate = sanitizePointer(weaponC4Entity);
        droppedEntityCacheFresh =
            esp::data::IsDroppedC4EntityCacheFresh(
                bombDroppedByRules,
                bombDroppedRulesRoseThisTick,
                worldScanFoundC4,
                s_mergedWeaponEntity,
                s_mergedWeaponEntityUs,
                nowUs);
        if (weaponC4ProbeDue) {
            s_lastWeaponC4ProbeUs = nowUs;
            if (ofs.dwWeaponC4 > 0) {
                uintptr_t probedWeaponRoot = 0;
                if (readPointer(g::clientBase + ofs.dwWeaponC4, &probedWeaponRoot)) {
                    weaponRootCandidate = sanitizePointer(probedWeaponRoot);
                }
            }
        }

        uintptr_t resolvedWeaponC4 = 0;
        const bool carriedEntityCacheFresh =
            !bombDroppedByRules &&
            !weaponC4ProbeDue &&
            weaponRootCandidate != 0 &&
            weaponRootCandidate == s_cachedWeaponRootCandidate &&
            s_mergedWeaponEntity != 0 &&
            s_mergedWeaponEntityUs > 0 &&
            nowUs >= s_mergedWeaponEntityUs &&
            (nowUs - s_mergedWeaponEntityUs) <=
                esp::data::kWeaponC4EntityHoldUs;
        if (carriedEntityCacheFresh ||
            (droppedEntityCacheFresh && !weaponC4ProbeDue)) {
            resolvedWeaponC4 = s_mergedWeaponEntity;
            weaponC4ListOrdinal = s_mergedWeaponListOrdinal;
            weaponEntityFromHold = true;
            weaponC4ResolutionMode = 5;
        } else {
            resolvedWeaponC4 = resolveWeaponC4Entity(weaponRootCandidate);
        }

        const bool heldWeaponEntityFresh =
            !bombDroppedByRules &&
            s_mergedWeaponEntity &&
            s_mergedWeaponEntityUs > 0 &&
            nowUs >= s_mergedWeaponEntityUs &&
            (nowUs - s_mergedWeaponEntityUs) <= esp::data::kWeaponC4EntityHoldUs &&
            hasC4ItemDefinition(s_mergedWeaponEntity);
        if (resolvedWeaponC4) {
            weaponC4Entity = resolvedWeaponC4;
            s_cachedWeaponRootCandidate = weaponRootCandidate;
        } else if (heldWeaponEntityFresh) {
            weaponC4Entity = s_mergedWeaponEntity;
            weaponC4ListOrdinal = s_mergedWeaponListOrdinal;
            weaponEntityFromHold = true;
            weaponC4ResolutionMode = 5;
        } else {
            weaponC4Entity = 0;
            s_cachedWeaponRootCandidate = 0;
        }
    } else {
        weaponC4Entity = 0;
        s_cachedWeaponRootCandidate = 0;
    }

    const bool weaponDynamicCacheStable =
        weaponC4Entity &&
        weaponC4Entity == s_cachedWeaponDynamicEntity;
    if (weaponDynamicCacheStable) {
        weaponC4OwnerHandle = s_cachedWeaponOwnerHandle;
        weaponC4DropTick = s_cachedWeaponDropTick;
        weaponC4CanBePickedUp = s_cachedWeaponCanBePickedUp;
        weaponC4CanBePickedUpKnown = s_cachedWeaponCanBePickedUpKnown;
        weaponC4WorldPos = s_cachedWeaponWorldPos;
        weaponC4Velocity = s_cachedWeaponVelocity;
        weaponC4PosValid = s_cachedWeaponPosValid;
    }
    const bool weaponCarryEvidence =
        inventoryC4CarrierEvidence ||
        cachedCarryEvidence ||
        (!s_bombState.planted &&
         !s_bombState.dropped &&
         (s_bombState.sourceFlags &
          (BombResolveSourceCarrySignal |
           BombResolveSourceCarrySticky |
           BombResolveSourceAttachGrace)) != 0u);
    const bool weaponDynamicUrgent =
        bombDroppedByRules ||
        s_bombState.dropped;
    const uint64_t weaponDynamicRefreshUs =
        esp::data::SelectWeaponC4DynamicRefreshUs(
            bombDroppedByRules,
            s_bombState.dropped,
            weaponCarryEvidence);
    const bool higherPriorityLaneActive =
        _playerHierarchyActiveTick ||
        _playerAuxActiveTick ||
        _inventoryActiveTick ||
        _inventoryFullTick ||
        _boneReadsActiveTick ||
        worldScanCommitted;
    const bool weaponDynamicReadDue =
        esp::data::IsWeaponC4DynamicReadDue(
            weaponC4Entity != 0,
            weaponDynamicCacheStable,
            bombDroppedRulesRoseThisTick,
            weaponDynamicUrgent,
            higherPriorityLaneActive,
            s_cachedWeaponDynamicUs,
            nowUs,
            weaponDynamicRefreshUs);

    // A probe can expire between dynamic reads. Keep the matching node on
    // scheduled no-read ticks; otherwise we erase it, then erase the C4's
    // confirmation key despite still having a current position sample.
    const bool preserveScheduledWeaponNode =
        !weaponDynamicReadDue && weaponDynamicCacheStable &&
        weaponC4Entity == s_mergedWeaponEntity && !bombDroppedRulesRoseThisTick;

    const bool plantedNodeCached =
        plantedC4Entity && plantedC4Entity == s_mergedPlantedEntity && s_mergedBombSceneNode;
    // Drop edges clear this cache. Between bounded entity probes the same
    // generation may safely reuse its scene node.
    const bool weaponNodeCached =
        !bombDroppedRulesRoseThisTick &&
        weaponC4Entity &&
        weaponC4Entity == s_mergedWeaponEntity &&
        s_mergedWeaponC4SceneNode &&
        (!bombDroppedByRules || droppedEntityCacheFresh || preserveScheduledWeaponNode);
    const bool plantedMetaEntityStable =
        plantedC4Entity && plantedC4Entity == s_cachedPlantedMetaEntity;
    const bool plantedPosEntityStable =
        plantedC4Entity &&
        plantedC4Entity == s_cachedPlantedPosEntity &&
        isValidWorldPos(s_cachedPlantedWorldPos);
    auto restoreCachedPlantedMetadata = [&]() {
        if (plantedMetaEntityStable) {
            bombTicking = s_cachedPlantedTicking;
            bombBeingDefused = s_cachedPlantedBeingDefused;
            bombHasExploded = s_cachedPlantedHasExploded;
            bombDefused = s_cachedPlantedBombDefused;
            bombActivated = s_cachedPlantedActivated;
            bombDefuserHandle = s_cachedPlantedDefuserHandle;
            bombBlowTime = s_cachedPlantedBlowTime;
            bombTimerLength = s_cachedPlantedTimerLength;
            bombDefuseEndTime = s_cachedPlantedDefuseEndTime;
            bombDefuseLength = s_cachedPlantedDefuseLength;
            return;
        }
        clearPlantedMetadataSample();
    };
    restoreCachedPlantedMetadata();
    if (plantedPosEntityStable)
        bombWorldPos = s_cachedPlantedWorldPos;
    const uint64_t plantedMetaRefreshUs =
        (s_cachedPlantedBeingDefused || plantedDefuseProbeHint)
            ? esp::data::kPlantedC4DefuseMetaRefreshUs
            : esp::data::kPlantedC4MetaRefreshUs;
    const bool plantedMetaForced =
        plantedC4Entity &&
        (!plantedMetaEntityStable ||
         bombPlantedRulesRoseThisTick ||
         s_cachedPlantedMetaUs == 0);
    const bool plantedMetaNominallyDue =
        plantedC4Entity &&
        (plantedMetaForced ||
         (nowUs - s_cachedPlantedMetaUs) >= plantedMetaRefreshUs);
    const uint64_t plantedMetaAgeUs =
        s_cachedPlantedMetaUs > 0 && nowUs >= s_cachedPlantedMetaUs
            ? nowUs - s_cachedPlantedMetaUs
            : 0u;
    const bool deferPlantedMetaForBusyLane =
        plantedMetaNominallyDue &&
        esp::data::ShouldDeferBombReadForBusyLane(
            higherPriorityLaneActive,
            s_cachedPlantedBeingDefused || plantedDefuseProbeHint,
            plantedMetaForced,
            plantedMetaEntityStable,
            plantedMetaAgeUs,
            plantedMetaRefreshUs);
    const bool plantedMetaDue =
        plantedMetaNominallyDue &&
        !deferPlantedMetaForBusyLane;
    const bool plantedPosDue =
        plantedC4Entity &&
        !plantedPosEntityStable;
    const bool mergeScatters =
        (plantedNodeCached || !plantedC4Entity) &&
        (weaponNodeCached || !weaponC4Entity || !weaponDynamicReadDue);

    bool plantedMetaReadSucceeded = !plantedMetaDue;
    bool weaponDynamicReadSucceeded = !weaponDynamicReadDue;
    const bool requestBombTicking =
        plantedMetaDue && ofs.C_PlantedC4_m_bBombTicking > 0;
    const bool requestBombBeingDefused =
        plantedMetaDue && ofs.C_PlantedC4_m_bBeingDefused > 0;
    const bool requestBombHasExploded =
        plantedMetaDue && ofs.C_PlantedC4_m_bHasExploded > 0;
    const bool requestBombDefused =
        plantedMetaDue && ofs.C_PlantedC4_m_bBombDefused > 0;
    const bool requestBombActivated =
        plantedMetaDue && ofs.C_PlantedC4_m_bC4Activated > 0;
    const bool requestBombDefuserHandle =
        plantedMetaDue && ofs.C_PlantedC4_m_hBombDefuser > 0;
    const bool requestBombBlowTime =
        plantedMetaDue && ofs.C_PlantedC4_m_flC4Blow > 0;
    const bool requestBombTimerLength =
        plantedMetaDue && ofs.C_PlantedC4_m_flTimerLength > 0;
    const bool requestBombDefuseEndTime =
        plantedMetaDue && ofs.C_PlantedC4_m_flDefuseCountDown > 0;
    const bool requestBombDefuseLength =
        plantedMetaDue && ofs.C_PlantedC4_m_flDefuseLength > 0;
    const bool requestPlantedSceneNode =
        plantedC4Entity &&
        !plantedNodeCached &&
        ofs.C_BaseEntity_m_pGameSceneNode > 0;
    const bool requestWeaponSceneNode =
        weaponC4Entity &&
        weaponDynamicReadDue &&
        !weaponNodeCached &&
        ofs.C_BaseEntity_m_pGameSceneNode > 0;
    const bool requestWeaponOwner =
        weaponC4Entity &&
        weaponDynamicReadDue &&
        ofs.C_BaseEntity_m_hOwnerEntity > 0;
    const bool requestWeaponDropTick =
        weaponC4Entity &&
        weaponDynamicReadDue &&
        ofs.C_CSWeaponBase_m_nDropTick > 0;
    const bool requestWeaponCanBePickedUp =
        weaponC4Entity &&
        weaponDynamicReadDue &&
        ofs.C_CSWeaponBase_m_bCanBePickedUp > 0;
    const bool requestWeaponVelocity =
        weaponC4Entity &&
        weaponDynamicReadDue &&
        ofs.C_BaseEntity_m_vecVelocity > 0;
    DWORD bombSceneNodeBytesRead = 0;
    DWORD weaponSceneNodeBytesRead = 0;
    DWORD bombTickingBytesRead = 0;
    DWORD bombBeingDefusedBytesRead = 0;
    DWORD bombHasExplodedBytesRead = 0;
    DWORD bombDefusedBytesRead = 0;
    DWORD bombActivatedBytesRead = 0;
    DWORD bombDefuserHandleBytesRead = 0;
    DWORD bombBlowTimeBytesRead = 0;
    DWORD bombTimerLengthBytesRead = 0;
    DWORD bombDefuseEndTimeBytesRead = 0;
    DWORD bombDefuseLengthBytesRead = 0;
    DWORD weaponOwnerBytesRead = 0;
    DWORD weaponDropTickBytesRead = 0;
    DWORD weaponCanBePickedUpBytesRead = 0;
    DWORD weaponVelocityBytesRead = 0;
    DWORD bombWorldPositionBytesRead = 0;
    DWORD weaponWorldPositionBytesRead = 0;
    bool plantedPositionReadAttempted = false;
    bool plantedPositionReadComplete = plantedPosEntityStable;
    bool weaponPositionReadAttempted = false;
    bool weaponPositionReadComplete =
        !weaponDynamicReadDue &&
        weaponDynamicCacheStable &&
        s_cachedWeaponPosValid;
    {
        uintptr_t rawBombSceneNode = plantedNodeCached ? s_mergedBombSceneNode : 0;
        uintptr_t rawWeaponC4SceneNode = weaponNodeCached ? s_mergedWeaponC4SceneNode : 0;
        bool queuedBatch1 = false;
        bool batch1Ok = true;

        bombCollisionMins = {};
        bombCollisionMaxs = {};
        weaponC4CollisionMins = {};
        weaponC4CollisionMaxs = {};

        if (plantedC4Entity) {
            if (requestPlantedSceneNode) {
                mem.AddScatterReadRequest(
                    handle,
                    plantedC4Entity + ofs.C_BaseEntity_m_pGameSceneNode,
                    &rawBombSceneNode,
                    sizeof(rawBombSceneNode),
                    &bombSceneNodeBytesRead);
                queuedBatch1 = true;
            }
            if (requestBombTicking) {
                mem.AddScatterReadRequest(
                    handle,
                    plantedC4Entity + ofs.C_PlantedC4_m_bBombTicking,
                    &bombTicking,
                    sizeof(bombTicking),
                    &bombTickingBytesRead);
                queuedBatch1 = true;
            }
            if (requestBombBeingDefused) {
                mem.AddScatterReadRequest(
                    handle,
                    plantedC4Entity + ofs.C_PlantedC4_m_bBeingDefused,
                    &bombBeingDefused,
                    sizeof(bombBeingDefused),
                    &bombBeingDefusedBytesRead);
                queuedBatch1 = true;
            }
            if (requestBombHasExploded) {
                mem.AddScatterReadRequest(
                    handle,
                    plantedC4Entity + ofs.C_PlantedC4_m_bHasExploded,
                    &bombHasExploded,
                    sizeof(bombHasExploded),
                    &bombHasExplodedBytesRead);
                queuedBatch1 = true;
            }
            if (requestBombDefused) {
                mem.AddScatterReadRequest(
                    handle,
                    plantedC4Entity + ofs.C_PlantedC4_m_bBombDefused,
                    &bombDefused,
                    sizeof(bombDefused),
                    &bombDefusedBytesRead);
                queuedBatch1 = true;
            }
            if (requestBombActivated) {
                mem.AddScatterReadRequest(
                    handle,
                    plantedC4Entity + ofs.C_PlantedC4_m_bC4Activated,
                    &bombActivated,
                    sizeof(bombActivated),
                    &bombActivatedBytesRead);
                queuedBatch1 = true;
            }
            if (requestBombDefuserHandle) {
                mem.AddScatterReadRequest(
                    handle,
                    plantedC4Entity + ofs.C_PlantedC4_m_hBombDefuser,
                    &bombDefuserHandle,
                    sizeof(bombDefuserHandle),
                    &bombDefuserHandleBytesRead);
                queuedBatch1 = true;
            }
            if (requestBombBlowTime) {
                mem.AddScatterReadRequest(
                    handle,
                    plantedC4Entity + ofs.C_PlantedC4_m_flC4Blow,
                    &bombBlowTime,
                    sizeof(bombBlowTime),
                    &bombBlowTimeBytesRead);
                queuedBatch1 = true;
            }
            if (requestBombTimerLength) {
                mem.AddScatterReadRequest(
                    handle,
                    plantedC4Entity + ofs.C_PlantedC4_m_flTimerLength,
                    &bombTimerLength,
                    sizeof(bombTimerLength),
                    &bombTimerLengthBytesRead);
                queuedBatch1 = true;
            }
            if (requestBombDefuseEndTime) {
                mem.AddScatterReadRequest(
                    handle,
                    plantedC4Entity + ofs.C_PlantedC4_m_flDefuseCountDown,
                    &bombDefuseEndTime,
                    sizeof(bombDefuseEndTime),
                    &bombDefuseEndTimeBytesRead);
                queuedBatch1 = true;
            }
            if (requestBombDefuseLength) {
                mem.AddScatterReadRequest(
                    handle,
                    plantedC4Entity + ofs.C_PlantedC4_m_flDefuseLength,
                    &bombDefuseLength,
                    sizeof(bombDefuseLength),
                    &bombDefuseLengthBytesRead);
                queuedBatch1 = true;
            }
            if (mergeScatters && s_mergedBombSceneNode && plantedPosDue && ofs.CGameSceneNode_m_vecAbsOrigin > 0) {
                plantedPositionReadAttempted = true;
                mem.AddScatterReadRequest(
                    handle,
                    s_mergedBombSceneNode + ofs.CGameSceneNode_m_vecAbsOrigin,
                    &bombWorldPos,
                    sizeof(bombWorldPos),
                    &bombWorldPositionBytesRead);
                queuedBatch1 = true;
            }
        }

        if (weaponC4Entity && weaponDynamicReadDue) {
            if (requestWeaponSceneNode) {
                mem.AddScatterReadRequest(
                    handle,
                    weaponC4Entity + ofs.C_BaseEntity_m_pGameSceneNode,
                    &rawWeaponC4SceneNode,
                    sizeof(rawWeaponC4SceneNode),
                    &weaponSceneNodeBytesRead);
                queuedBatch1 = true;
            }
            if (requestWeaponOwner) {
                mem.AddScatterReadRequest(
                    handle,
                    weaponC4Entity + ofs.C_BaseEntity_m_hOwnerEntity,
                    &weaponC4OwnerHandle,
                    sizeof(weaponC4OwnerHandle),
                    &weaponOwnerBytesRead);
                queuedBatch1 = true;
            }
            if (requestWeaponDropTick) {
                mem.AddScatterReadRequest(
                    handle,
                    weaponC4Entity + ofs.C_CSWeaponBase_m_nDropTick,
                    &weaponC4DropTick,
                    sizeof(weaponC4DropTick),
                    &weaponDropTickBytesRead);
                queuedBatch1 = true;
            }
            if (requestWeaponCanBePickedUp) {
                mem.AddScatterReadRequest(
                    handle,
                    weaponC4Entity + ofs.C_CSWeaponBase_m_bCanBePickedUp,
                    &weaponC4CanBePickedUp,
                    sizeof(weaponC4CanBePickedUp),
                    &weaponCanBePickedUpBytesRead);
                queuedBatch1 = true;
            }
            if (requestWeaponVelocity) {
                mem.AddScatterReadRequest(
                    handle,
                    weaponC4Entity + ofs.C_BaseEntity_m_vecVelocity,
                    &weaponC4Velocity,
                    sizeof(weaponC4Velocity),
                    &weaponVelocityBytesRead);
                queuedBatch1 = true;
            }
            if (mergeScatters && s_mergedWeaponC4SceneNode && ofs.CGameSceneNode_m_vecAbsOrigin > 0) {
                weaponPositionReadAttempted = true;
                mem.AddScatterReadRequest(
                    handle,
                    s_mergedWeaponC4SceneNode + ofs.CGameSceneNode_m_vecAbsOrigin,
                    &weaponC4WorldPos,
                    sizeof(weaponC4WorldPos),
                    &weaponWorldPositionBytesRead);
                queuedBatch1 = true;
            }
        }

        if (queuedBatch1)
            batch1Ok = executeOptionalScatterRead();
        if (plantedMetaDue) {
            const bool hasAuthoritativeMetadataRequest =
                requestBombTicking ||
                requestBombActivated ||
                requestBombBlowTime;
            const bool metadataReadsComplete =
                esp::data::IsBombFieldReadComplete(
                    requestBombTicking,
                    bombTickingBytesRead,
                    sizeof(bombTicking)) &&
                esp::data::IsBombFieldReadComplete(
                    requestBombBeingDefused,
                    bombBeingDefusedBytesRead,
                    sizeof(bombBeingDefused)) &&
                esp::data::IsBombFieldReadComplete(
                    requestBombHasExploded,
                    bombHasExplodedBytesRead,
                    sizeof(bombHasExploded)) &&
                esp::data::IsBombFieldReadComplete(
                    requestBombDefused,
                    bombDefusedBytesRead,
                    sizeof(bombDefused)) &&
                esp::data::IsBombFieldReadComplete(
                    requestBombActivated,
                    bombActivatedBytesRead,
                    sizeof(bombActivated)) &&
                esp::data::IsBombFieldReadComplete(
                    requestBombDefuserHandle,
                    bombDefuserHandleBytesRead,
                    sizeof(bombDefuserHandle)) &&
                esp::data::IsBombFieldReadComplete(
                    requestBombBlowTime,
                    bombBlowTimeBytesRead,
                    sizeof(bombBlowTime)) &&
                esp::data::IsBombFieldReadComplete(
                    requestBombTimerLength,
                    bombTimerLengthBytesRead,
                    sizeof(bombTimerLength)) &&
                esp::data::IsBombFieldReadComplete(
                    requestBombDefuseEndTime,
                    bombDefuseEndTimeBytesRead,
                    sizeof(bombDefuseEndTime)) &&
                esp::data::IsBombFieldReadComplete(
                    requestBombDefuseLength,
                    bombDefuseLengthBytesRead,
                    sizeof(bombDefuseLength));
            const bool metadataPlausible =
                esp::data::IsPlausiblePlantedC4Metadata({
                    bombTicking,
                    bombBeingDefused,
                    bombHasExploded,
                    bombDefused,
                    bombActivated,
                    bombBlowTime,
                    bombTimerLength,
                    bombDefuseEndTime,
                    bombDefuseLength,
                });
            plantedMetaReadSucceeded =
                queuedBatch1 &&
                hasAuthoritativeMetadataRequest &&
                metadataReadsComplete &&
                metadataPlausible;
        }
        if (weaponDynamicReadDue) {
            weaponC4CanBePickedUpKnown =
                esp::data::IsValidWeaponPickableSample(
                    requestWeaponCanBePickedUp,
                    weaponCanBePickedUpBytesRead,
                    weaponC4CanBePickedUp);
            if (!weaponC4CanBePickedUpKnown)
                weaponC4CanBePickedUp = 0;
            const bool weaponCoreReadsComplete =
                esp::data::IsBombFieldReadComplete(
                    requestWeaponSceneNode,
                    weaponSceneNodeBytesRead,
                    sizeof(rawWeaponC4SceneNode)) &&
                esp::data::IsBombFieldReadComplete(
                    requestWeaponOwner,
                    weaponOwnerBytesRead,
                    sizeof(weaponC4OwnerHandle)) &&
                esp::data::IsBombFieldReadComplete(
                    requestWeaponDropTick,
                    weaponDropTickBytesRead,
                    sizeof(weaponC4DropTick));
            weaponDynamicReadSucceeded =
                queuedBatch1 &&
                batch1Ok &&
                weaponCoreReadsComplete;
            if (!esp::data::IsBombFieldReadComplete(
                    requestWeaponVelocity,
                    weaponVelocityBytesRead,
                    sizeof(weaponC4Velocity))) {
                weaponC4Velocity = {};
            }
        }
        if (plantedPositionReadAttempted) {
            plantedPositionReadComplete =
                batch1Ok &&
                bombWorldPositionBytesRead == sizeof(bombWorldPos);
        }
        if (weaponPositionReadAttempted) {
            weaponPositionReadComplete =
                batch1Ok &&
                weaponWorldPositionBytesRead == sizeof(weaponC4WorldPos);
        }

        if (requestPlantedSceneNode &&
            bombSceneNodeBytesRead != sizeof(rawBombSceneNode)) {
            rawBombSceneNode = 0;
        }
        if (requestWeaponSceneNode &&
            weaponSceneNodeBytesRead != sizeof(rawWeaponC4SceneNode)) {
            rawWeaponC4SceneNode = 0;
        }

        bombSceneNode = sanitizePointer(rawBombSceneNode);
        weaponC4SceneNode = sanitizePointer(rawWeaponC4SceneNode);
    }

    if (plantedMetaDue && !plantedMetaReadSucceeded)
        restoreCachedPlantedMetadata();

    if (!mergeScatters) {
        bool queuedBatch2 = false;
        bool plantedPositionQueuedBatch2 = false;
        bool weaponPositionQueuedBatch2 = false;
        if (bombSceneNode && plantedPosDue && ofs.CGameSceneNode_m_vecAbsOrigin > 0) {
            plantedPositionReadAttempted = true;
            plantedPositionQueuedBatch2 = true;
            mem.AddScatterReadRequest(
                handle,
                bombSceneNode + ofs.CGameSceneNode_m_vecAbsOrigin,
                &bombWorldPos,
                sizeof(bombWorldPos),
                &bombWorldPositionBytesRead);
            queuedBatch2 = true;
        }
        if (weaponDynamicReadDue &&
            weaponC4SceneNode &&
            ofs.CGameSceneNode_m_vecAbsOrigin > 0) {
            weaponPositionReadAttempted = true;
            weaponPositionQueuedBatch2 = true;
            mem.AddScatterReadRequest(
                handle,
                weaponC4SceneNode + ofs.CGameSceneNode_m_vecAbsOrigin,
                &weaponC4WorldPos,
                sizeof(weaponC4WorldPos),
                &weaponWorldPositionBytesRead);
            queuedBatch2 = true;
        }
        if (queuedBatch2) {
            const bool batch2Ok = executeOptionalScatterRead();
            if (plantedPositionQueuedBatch2) {
                plantedPositionReadComplete =
                    batch2Ok &&
                    bombWorldPositionBytesRead == sizeof(bombWorldPos);
            }
            if (weaponPositionQueuedBatch2) {
                weaponPositionReadComplete =
                    batch2Ok &&
                    weaponWorldPositionBytesRead == sizeof(weaponC4WorldPos);
            }
        }
    }

    if (weaponDynamicReadDue) {
        weaponC4PosValid =
            weaponPositionReadComplete &&
            isDroppedC4PositionPlausible(weaponC4WorldPos);
    }

    if (bombPlantedByRules &&
        plantedC4Entity &&
        !bombSceneNode &&
        ofs.C_BaseEntity_m_pGameSceneNode > 0) {
        uintptr_t retrySceneNode = 0;
        if (readValue(plantedC4Entity + ofs.C_BaseEntity_m_pGameSceneNode, &retrySceneNode, sizeof(retrySceneNode)))
            bombSceneNode = sanitizePointer(retrySceneNode);
    }
    if (bombPlantedByRules &&
        bombSceneNode &&
        !isValidWorldPos(bombWorldPos) &&
        ofs.CGameSceneNode_m_vecAbsOrigin > 0) {
        if (readValue(
                bombSceneNode + ofs.CGameSceneNode_m_vecAbsOrigin,
                &bombWorldPos,
                sizeof(bombWorldPos))) {
            plantedPositionReadAttempted = true;
            plantedPositionReadComplete = true;
        }
    }
    if (!bombPlantedByRules &&
        weaponC4Entity &&
        weaponDynamicReadDue &&
        !weaponC4SceneNode &&
        ofs.C_BaseEntity_m_pGameSceneNode > 0) {
        uintptr_t retrySceneNode = 0;
        if (readValue(weaponC4Entity + ofs.C_BaseEntity_m_pGameSceneNode, &retrySceneNode, sizeof(retrySceneNode)))
            weaponC4SceneNode = sanitizePointer(retrySceneNode);
    }
    if (!bombPlantedByRules &&
        weaponDynamicReadDue &&
        weaponC4SceneNode &&
        !weaponC4PosValid &&
         ofs.CGameSceneNode_m_vecAbsOrigin > 0 &&
         readValue(weaponC4SceneNode + ofs.CGameSceneNode_m_vecAbsOrigin, &weaponC4WorldPos, sizeof(weaponC4WorldPos))) {
        weaponPositionReadAttempted = true;
        weaponPositionReadComplete = true;
        weaponC4PosValid = isDroppedC4PositionPlausible(weaponC4WorldPos);
    }

    // Prefer CTransform m_nodeToWorld position when abs origin is latched/stale.
    if (!bombPlantedByRules &&
        weaponC4SceneNode &&
        weaponDynamicReadDue) {
        Vector3 ntwPos = { NAN, NAN, NAN };
        if (readValue(weaponC4SceneNode + 0x10u, &ntwPos, sizeof(ntwPos)) &&
            isDroppedC4PositionPlausible(ntwPos)) {
            weaponPositionReadAttempted = true;
            weaponPositionReadComplete = true;
            if (!weaponC4PosValid) {
                weaponC4WorldPos = ntwPos;
                weaponC4PosValid = true;
            } else {
                const float dx = weaponC4WorldPos.x - ntwPos.x;
                const float dy = weaponC4WorldPos.y - ntwPos.y;
                const float dz = weaponC4WorldPos.z - ntwPos.z;
                if ((dx * dx + dy * dy + dz * dz) > (16.0f * 16.0f)) {
                    weaponC4WorldPos = ntwPos;
                    weaponC4PosValid = true;
                }
            }
        }
    }

    if (weaponDynamicReadDue) {
        if (!weaponDynamicReadSucceeded) {
            if (weaponDynamicCacheStable) {
                weaponC4OwnerHandle = s_cachedWeaponOwnerHandle;
                weaponC4DropTick = s_cachedWeaponDropTick;
                weaponC4CanBePickedUp = s_cachedWeaponCanBePickedUp;
                weaponC4CanBePickedUpKnown =
                    s_cachedWeaponCanBePickedUpKnown;
                weaponC4Velocity = s_cachedWeaponVelocity;
            } else {
                weaponC4OwnerHandle = 0;
                weaponC4DropTick = 0;
                weaponC4CanBePickedUp = 0;
                weaponC4CanBePickedUpKnown = false;
                weaponC4Velocity = {};
            }
        }
        if (!weaponPositionReadComplete) {
            if (weaponDynamicCacheStable) {
                weaponC4WorldPos = s_cachedWeaponWorldPos;
                weaponC4PosValid = s_cachedWeaponPosValid;
            } else {
                weaponC4WorldPos = { NAN, NAN, NAN };
                weaponC4PosValid = false;
            }
        }
    }

    const bool weaponDropTickAdvancedThisRead =
        weaponDynamicReadDue &&
        weaponDynamicReadSucceeded &&
        esp::data::IsWeaponDropTickAdvance(
            weaponC4Entity != 0 &&
                weaponC4Entity == s_cachedWeaponDynamicEntity,
            s_cachedWeaponDynamicUs > 0,
            s_cachedWeaponDropTick,
            weaponC4DropTick);
    if (weaponDropTickAdvancedThisRead) {
        s_recentDropTickEdgeEntity = weaponC4Entity;
        s_recentDropTickEdge = weaponC4DropTick;
        s_recentDropTickEdgeUntilUs =
            nowUs + esp::data::kWeaponC4DropTickEdgeHoldUs;
        s_recentOwnerAttachEntity = 0;
        s_recentOwnerAttachUntilUs = 0;
    }
    const bool currentWeaponOwnerValid =
        weaponC4OwnerHandle != 0u &&
        weaponC4OwnerHandle != 0xFFFFFFFFu;
    const bool previousWeaponOwnerValid =
        s_cachedWeaponOwnerHandle != 0u &&
        s_cachedWeaponOwnerHandle != 0xFFFFFFFFu;
    const bool sameCachedWeaponEntity =
        weaponC4Entity != 0 &&
        weaponC4Entity == s_cachedWeaponDynamicEntity;
    if (weaponDynamicReadDue && weaponDynamicReadSucceeded) {
        if (!currentWeaponOwnerValid) {
            s_ownerDetachedWeaponEntity = weaponC4Entity;
            s_recentOwnerAttachEntity = 0;
            s_recentOwnerAttachUntilUs = 0;
        } else {
            const bool ownerAttachedAfterDetach =
                esp::data::IsWeaponC4OwnerAttachTransition(
                    sameCachedWeaponEntity,
                    s_cachedWeaponDynamicUs > 0,
                    previousWeaponOwnerValid,
                    currentWeaponOwnerValid,
                    s_cachedWeaponOwnerHandle != weaponC4OwnerHandle,
                    s_ownerDetachedWeaponEntity == weaponC4Entity,
                    weaponDropTickAdvancedThisRead);
            if (ownerAttachedAfterDetach) {
                s_recentOwnerAttachEntity = weaponC4Entity;
                s_recentOwnerAttachUntilUs =
                    nowUs + esp::data::kBombOwnerAttachGraceUs;
                s_ownerDetachedWeaponEntity = 0;
                s_recentDropTickEdgeEntity = 0;
                s_recentDropTickEdge = 0;
                s_recentDropTickEdgeUntilUs = 0;
            }
        }
    }
    s_mergedPlantedEntity = plantedC4Entity;
    s_mergedWeaponEntity = weaponC4Entity;
    if (weaponC4Entity && !weaponEntityFromHold) {
        s_mergedWeaponEntityUs = nowUs;
        s_mergedWeaponListOrdinal = weaponC4ListOrdinal;
    } else if (!weaponC4Entity ||
             bombPlantedByRules ||
             s_mergedWeaponEntityUs == 0 ||
             nowUs < s_mergedWeaponEntityUs ||
             (nowUs - s_mergedWeaponEntityUs) > esp::data::kWeaponC4EntityHoldUs) {
        s_mergedWeaponEntityUs = 0;
        s_mergedWeaponListOrdinal = 0;
    }
    s_mergedBombSceneNode = bombSceneNode;
    s_mergedWeaponC4SceneNode = weaponC4SceneNode;
    if (weaponC4Entity) {
        if (weaponDynamicReadDue && weaponDynamicReadSucceeded) {
            const bool weaponEntityChanged =
                s_cachedWeaponDynamicEntity != weaponC4Entity;
            s_cachedWeaponDynamicEntity = weaponC4Entity;
            s_cachedWeaponOwnerHandle = weaponC4OwnerHandle;
            s_cachedWeaponDropTick = weaponC4DropTick;
            s_cachedWeaponCanBePickedUp = weaponC4CanBePickedUp;
            s_cachedWeaponCanBePickedUpKnown =
                weaponC4CanBePickedUpKnown;
            s_cachedWeaponVelocity = weaponC4Velocity;
            s_cachedWeaponDynamicUs = nowUs;
            if (weaponEntityChanged && !weaponPositionReadComplete) {
                s_cachedWeaponWorldPos = { NAN, NAN, NAN };
                s_cachedWeaponPosValid = false;
                s_cachedWeaponPositionUs = 0;
            }
        }
        if (weaponDynamicReadDue &&
            weaponPositionReadComplete &&
            weaponC4PosValid &&
            s_cachedWeaponDynamicEntity == weaponC4Entity) {
            s_cachedWeaponWorldPos = weaponC4WorldPos;
            s_cachedWeaponPosValid = true;
            s_cachedWeaponPositionUs = nowUs;
        }
    } else {
        s_cachedWeaponDynamicEntity = 0;
        s_cachedWeaponDynamicUs = 0;
        s_cachedWeaponPositionUs = 0;
        s_cachedWeaponOwnerHandle = 0;
        s_cachedWeaponDropTick = 0;
        s_cachedWeaponCanBePickedUp = 0;
        s_cachedWeaponCanBePickedUpKnown = false;
        s_cachedWeaponWorldPos = { NAN, NAN, NAN };
        s_cachedWeaponVelocity = {};
        s_cachedWeaponPosValid = false;
    }
    if (s_recentDropTickEdgeEntity != weaponC4Entity ||
        nowUs > s_recentDropTickEdgeUntilUs ||
        (s_recentDropTickEdge != 0u &&
         weaponC4DropTick != s_recentDropTickEdge)) {
        s_recentDropTickEdgeEntity = 0;
        s_recentDropTickEdge = 0;
        s_recentDropTickEdgeUntilUs = 0;
    }
    if (s_recentOwnerAttachEntity != weaponC4Entity ||
        nowUs > s_recentOwnerAttachUntilUs) {
        s_recentOwnerAttachEntity = 0;
        s_recentOwnerAttachUntilUs = 0;
    }
    weaponC4RecentDropTickEdge =
        weaponC4Entity != 0 &&
        s_recentDropTickEdgeEntity == weaponC4Entity &&
        s_recentDropTickEdgeUntilUs >= nowUs;
    weaponC4RecentOwnerAttach =
        weaponC4Entity != 0 &&
        s_recentOwnerAttachEntity == weaponC4Entity &&
        s_recentOwnerAttachUntilUs >= nowUs;
    const bool weaponC4PositionFresh =
        esp::data::IsWeaponC4PositionSampleCurrent(
            weaponC4Entity != 0 &&
                weaponC4Entity == s_cachedWeaponDynamicEntity,
            weaponC4PosValid,
            s_cachedWeaponDynamicUs,
            s_cachedWeaponPositionUs,
            nowUs);
    weaponC4DetachedCurrent =
        esp::data::IsAuthoritativeDetachedWeaponC4(
            weaponC4PositionFresh,
            weaponC4CanBePickedUpKnown,
            weaponC4CanBePickedUp != 0u,
            weaponC4RecentDropTickEdge);
    if (plantedC4Entity) {
        if (plantedMetaDue && plantedMetaReadSucceeded) {
            s_cachedPlantedMetaEntity = plantedC4Entity;
            s_cachedPlantedMetaUs = nowUs;
            s_cachedPlantedTicking = bombTicking;
            s_cachedPlantedBeingDefused = bombBeingDefused;
            s_cachedPlantedHasExploded = bombHasExploded;
            s_cachedPlantedBombDefused = bombDefused;
            s_cachedPlantedActivated = bombActivated;
            s_cachedPlantedDefuserHandle = bombDefuserHandle;
            s_cachedPlantedBlowTime = bombBlowTime;
            s_cachedPlantedTimerLength = bombTimerLength;
            s_cachedPlantedDefuseEndTime = bombDefuseEndTime;
            s_cachedPlantedDefuseLength = bombDefuseLength;
        }
        if (isValidWorldPos(bombWorldPos) &&
            (plantedPosEntityStable || plantedPositionReadComplete)) {
            s_cachedPlantedPosEntity = plantedC4Entity;
            s_cachedPlantedWorldPos = bombWorldPos;
        }
    } else {
        clearCachedPlantedRuntimeData();
    }
    const bool plantedMetaFresh =
        esp::data::IsBombMetadataFresh(
            plantedC4Entity != 0,
            plantedC4Entity == s_cachedPlantedMetaEntity,
            s_cachedPlantedMetaUs,
            nowUs);
