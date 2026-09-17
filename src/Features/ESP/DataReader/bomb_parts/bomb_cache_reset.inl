    static Vector3 s_lastDroppedBombPos = { NAN, NAN, NAN };
    static uint64_t s_lastDroppedBombPosUs = 0;
    static Vector3 s_lastVisibleBombPos = { NAN, NAN, NAN };
    static Vector3 s_lastVisibleBombBoundsMins = {};
    static Vector3 s_lastVisibleBombBoundsMaxs = {};
    static bool s_lastVisibleBombBoundsValid = false;
    static uint64_t s_lastVisibleBombPosUs = 0;
    static Vector3 s_lastWorldC4Pos = { NAN, NAN, NAN };
    static uint64_t s_lastWorldC4PosUs = 0;
    static uintptr_t s_lastWorldC4Entity = 0;
    static bool s_lastWorldC4NoOwner = false;
    static int s_lastWorldC4OwnerIdx = -1;
    static bool s_lastWorldC4OwnerAlive = false;
    static bool s_lastWorldC4OwnerNearby = false;
    static int s_cachedBombCarryOwnerSlot = -1;
    static uint64_t s_cachedBombCarryOwnerUs = 0;
    static int s_cachedBombAttachedOwnerSlot = -1;
    static uint64_t s_cachedBombAttachedOwnerUs = 0;
    static int s_lastObservedBombOwnerSlot = -1;
    static BombState s_lastConfirmedBombState = {};
    static uint64_t s_lastConfirmedBombStateUs = 0;
    static uintptr_t s_cachedPlantedMetaEntity = 0;
    static uintptr_t s_cachedPlantedRootCandidate = 0;
    static uint64_t s_cachedPlantedRootSeenUs = 0;
    static uint64_t s_cachedPlantedMetaUs = 0;
    static uint8_t s_cachedPlantedTicking = 0;
    static uint8_t s_cachedPlantedBeingDefused = 0;
    static uint8_t s_cachedPlantedHasExploded = 0;
    static uint8_t s_cachedPlantedBombDefused = 0;
    static uint8_t s_cachedPlantedActivated = 0;
    static uint32_t s_cachedPlantedDefuserHandle = 0;
    static float s_cachedPlantedBlowTime = 0.0f;
    static float s_cachedPlantedTimerLength = 0.0f;
    static float s_cachedPlantedDefuseEndTime = 0.0f;
    static float s_cachedPlantedDefuseLength = 0.0f;
    static uintptr_t s_cachedPlantedPosEntity = 0;
    static Vector3 s_cachedPlantedWorldPos = { NAN, NAN, NAN };
    static uintptr_t s_mergedPlantedEntity = 0;
    static uintptr_t s_cachedWeaponRootCandidate = 0;
    static uintptr_t s_mergedWeaponEntity = 0;
    static uint64_t s_mergedWeaponEntityUs = 0;
    static uint32_t s_mergedWeaponListOrdinal = 0;
    static uintptr_t s_mergedBombSceneNode = 0;
    static uintptr_t s_mergedWeaponC4SceneNode = 0;
    static uintptr_t s_cachedWeaponDynamicEntity = 0;
    static uint64_t s_cachedWeaponDynamicUs = 0;
    static uint64_t s_cachedWeaponPositionUs = 0;
    static uint32_t s_cachedWeaponOwnerHandle = 0;
    static uint32_t s_cachedWeaponDropTick = 0;
    static uint8_t s_cachedWeaponCanBePickedUp = 0;
    static bool s_cachedWeaponCanBePickedUpKnown = false;
    static Vector3 s_cachedWeaponWorldPos = { NAN, NAN, NAN };
    static Vector3 s_cachedWeaponVelocity = {};
    static bool s_cachedWeaponPosValid = false;
    static uintptr_t s_recentDropTickEdgeEntity = 0;
    static uint32_t s_recentDropTickEdge = 0;
    static uint64_t s_recentDropTickEdgeUntilUs = 0;
    static uintptr_t s_ownerDetachedWeaponEntity = 0;
    static uintptr_t s_recentOwnerAttachEntity = 0;
    static uint64_t s_recentOwnerAttachUntilUs = 0;
    static int s_freshInventoryC4CarrierSlot = -1;
    static uintptr_t s_freshInventoryC4Entity = 0;
    static uint64_t s_freshInventoryC4CarrierUs = 0;
    static uint64_t s_bombCacheResetEpoch = 0;
    static bool s_prevBombPlantedByRules = false;
    static bool s_prevBombDroppedByRules = false;
    static uint64_t s_bombDroppedRulesRiseUs = 0;
    static uint64_t s_lastBombSceneResetSerial = 0;
    static uint64_t s_bombDropGeneration = 1;
    static uint8_t s_prevBombCachedTicking = 0;
    static float s_prevBombCachedBlowTime = 0.0f;
    static bool s_prevBombCachedTerminal = false;
    static bool s_bombRulesPlantCycleArmed = false;
    static uint64_t s_bombRulesPlantFalseSinceUs = 0;
    static bool s_bombLiveMetadataSeenForCycle = false;
    static bool s_bombTerminalHandledForCycle = false;
    static bool s_bombTerminalAwaitingRulesClear = false;
    static uintptr_t s_explodedEntityTaintPtr = 0;
    static uint64_t s_explodedEntityTaintUntilUs = 0;
    static bool s_prevBombLiveContext = false;
    auto clearCachedPlantedRuntimeData = [&]() {
        s_cachedPlantedRootSeenUs = 0;
        s_cachedPlantedMetaEntity = 0;
        s_cachedPlantedMetaUs = 0;
        s_cachedPlantedTicking = 0;
        s_cachedPlantedBeingDefused = 0;
        s_cachedPlantedHasExploded = 0;
        s_cachedPlantedBombDefused = 0;
        s_cachedPlantedActivated = 0;
        s_cachedPlantedDefuserHandle = 0;
        s_cachedPlantedBlowTime = 0.0f;
        s_cachedPlantedTimerLength = 0.0f;
        s_cachedPlantedDefuseEndTime = 0.0f;
        s_cachedPlantedDefuseLength = 0.0f;
        s_cachedPlantedPosEntity = 0;
        s_cachedPlantedWorldPos = { NAN, NAN, NAN };
    };
    auto clearPlantedMetadataSample = [&]() {
        bombTicking = 0;
        bombBeingDefused = 0;
        bombHasExploded = 0;
        bombDefused = 0;
        bombActivated = 0;
        bombDefuserHandle = 0;
        bombBlowTime = 0.0f;
        bombTimerLength = 0.0f;
        bombDefuseEndTime = 0.0f;
        bombDefuseLength = 0.0f;
    };
    auto clearBombTrackingCache = [&]() {
        s_bombState = {};
        s_bombState.position = { NAN, NAN, NAN };
        s_lastDroppedBombPos = { NAN, NAN, NAN };
        s_lastDroppedBombPosUs = 0;
        s_lastVisibleBombPos = { NAN, NAN, NAN };
        s_lastVisibleBombBoundsMins = {};
        s_lastVisibleBombBoundsMaxs = {};
        s_lastVisibleBombBoundsValid = false;
        s_lastVisibleBombPosUs = 0;
        s_lastWorldC4Pos = { NAN, NAN, NAN };
        s_lastWorldC4PosUs = 0;
        s_lastWorldC4Entity = 0;
        s_lastWorldC4NoOwner = false;
        s_lastWorldC4OwnerIdx = -1;
        s_lastWorldC4OwnerAlive = false;
        s_lastWorldC4OwnerNearby = false;
        s_cachedBombCarryOwnerSlot = -1;
        s_cachedBombCarryOwnerUs = 0;
        s_cachedBombAttachedOwnerSlot = -1;
        s_cachedBombAttachedOwnerUs = 0;
        s_lastObservedBombOwnerSlot = -1;
        s_lastConfirmedBombState = {};
        s_lastConfirmedBombStateUs = 0;
        s_cachedPlantedRootCandidate = 0;
        clearCachedPlantedRuntimeData();
        s_mergedPlantedEntity = 0;
        s_cachedWeaponRootCandidate = 0;
        s_mergedWeaponEntity = 0;
        s_mergedWeaponEntityUs = 0;
        s_mergedWeaponListOrdinal = 0;
        s_mergedBombSceneNode = 0;
        s_mergedWeaponC4SceneNode = 0;
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
        s_recentDropTickEdgeEntity = 0;
        s_recentDropTickEdge = 0;
        s_recentDropTickEdgeUntilUs = 0;
        s_ownerDetachedWeaponEntity = 0;
        s_recentOwnerAttachEntity = 0;
        s_recentOwnerAttachUntilUs = 0;
        s_freshInventoryC4CarrierSlot = -1;
        s_freshInventoryC4Entity = 0;
        s_freshInventoryC4CarrierUs = 0;
    };
    auto clearBombCycleState = [&]() {
        s_prevBombPlantedByRules = false;
        s_prevBombDroppedByRules = false;
        s_bombDroppedRulesRiseUs = 0;
        s_prevBombCachedTicking = 0;
        s_prevBombCachedBlowTime = 0.0f;
        s_prevBombCachedTerminal = false;
        s_bombRulesPlantCycleArmed = false;
        s_bombRulesPlantFalseSinceUs = 0;
        s_bombLiveMetadataSeenForCycle = false;
        s_bombTerminalHandledForCycle = false;
        s_bombTerminalAwaitingRulesClear = false;
        s_explodedEntityTaintPtr = 0;
        s_explodedEntityTaintUntilUs = 0;
    };
    if (bombRoundChangedThisTick) {
        s_bombEpoch.fetch_add(1, std::memory_order_relaxed);
        ++s_bombDropGeneration;
        if (s_bombDropGeneration == 0)
            s_bombDropGeneration = 1;
        bombPlantedByRules = false;
        bombDroppedByRules = false;
        plantedC4Entity = 0;
        weaponC4Entity = 0;
    }
    bool bombEpochJustWiped = false;
    const uint64_t bombEpochAtEntry =
        s_bombEpoch.load(std::memory_order_relaxed);
    if (s_bombCacheResetEpoch != bombEpochAtEntry) {
        s_bombCacheResetEpoch = bombEpochAtEntry;
        bombEpochJustWiped = true;
        clearBombTrackingCache();
        clearBombCycleState();
        s_prevBombLiveContext = false;
    }
    bool bombEpochAdvancedThisTick = false;
    auto advanceBombEpoch = [&]() {
        if (bombEpochAdvancedThisTick)
            return;
        s_bombEpoch.fetch_add(1, std::memory_order_relaxed);
        bombEpochAdvancedThisTick = true;
    };
    const bool bombPlantedRulesRoseThisTick =
        !s_prevBombPlantedByRules && bombPlantedByRules;
    const bool bombDroppedRulesRoseThisTick =
        !s_prevBombDroppedByRules && bombDroppedByRules;
    if (bombDroppedRulesRoseThisTick) {
        s_bombDroppedRulesRiseUs = nowUs;
        ++s_bombDropGeneration;
        if (s_bombDropGeneration == 0)
            s_bombDropGeneration = 1;
    } else if (!bombDroppedByRules) {
        s_bombDroppedRulesRiseUs = 0;
    }

    
    
    
    
    
    
    
    const int bombSignOnState = s_engineSignOnState.load(std::memory_order_relaxed);
    const bool liveBombContext =
        s_engineInGame.load(std::memory_order_relaxed) &&
        !s_engineMenu.load(std::memory_order_relaxed) &&
        bombSignOnState == 6;

    if (!liveBombContext) {
        if (s_prevBombLiveContext)
            advanceBombEpoch();
        s_prevBombLiveContext = false;
        bombPlantedByRules = false;
        bombDroppedByRules = false;
        clearPlantedMetadataSample();
        plantedC4Entity = 0;
        weaponC4Entity = 0;
        bombSceneNode = 0;
        bombWorldPos = { NAN, NAN, NAN };
        bombCollisionMins = {};
        bombCollisionMaxs = {};
        weaponC4SceneNode = 0;
        weaponC4WorldPos = { NAN, NAN, NAN };
        weaponC4Velocity = {};
        weaponC4CollisionMins = {};
        weaponC4CollisionMaxs = {};
        weaponC4OwnerHandle = 0;
        weaponC4DropTick = 0;
        weaponC4CanBePickedUp = 0;
        weaponC4CanBePickedUpKnown = false;
        weaponC4RecentDropTickEdge = false;
        weaponC4RecentOwnerAttach = false;
        weaponC4DetachedCurrent = false;
        weaponC4PosValid = false;
        worldScanFoundC4 = false;
        worldScanC4Entity = 0;
        worldScanC4Pos = {};
        worldScanC4OwnerIdx = -1;
        worldScanC4OwnerAlive = false;
        worldScanC4OwnerNearby = false;
        worldScanC4NoOwner = false;
        worldScanC4Score = (std::numeric_limits<int>::min)();
        clearBombTrackingCache();
        clearBombCycleState();
    } else {
        s_prevBombLiveContext = true;
    }

    const uint64_t bombCacheSceneResetSerial = s_sceneResetSerial.load(std::memory_order_relaxed);
    if (s_lastBombSceneResetSerial != bombCacheSceneResetSerial) {
        s_lastBombSceneResetSerial = bombCacheSceneResetSerial;
        // Soft invalidate only. Hierarchy flat / repair also bumps scene serial
        // (~1 Hz); advanceBombEpoch() there produced bombEpoch=100+ and wiped
        // drop state every second (round-2 bomb vanishes).
        s_mergedWeaponC4SceneNode = 0;
        s_mergedBombSceneNode = 0;
        s_cachedWeaponDynamicEntity = 0;
        s_cachedWeaponDynamicUs = 0;
        s_cachedWeaponPositionUs = 0;
        s_cachedWeaponCanBePickedUp = 0;
        s_cachedWeaponCanBePickedUpKnown = false;
        s_cachedWeaponPosValid = false;
        s_recentDropTickEdgeEntity = 0;
        s_recentDropTickEdge = 0;
        s_recentDropTickEdgeUntilUs = 0;
        s_ownerDetachedWeaponEntity = 0;
        s_recentOwnerAttachEntity = 0;
        s_recentOwnerAttachUntilUs = 0;
        s_freshInventoryC4CarrierSlot = -1;
        s_freshInventoryC4Entity = 0;
        s_freshInventoryC4CarrierUs = 0;
    }
    
    
    
    bool confirmedBombRulesExit = false;
    if (bombPlantedByRules) {
        s_bombRulesPlantFalseSinceUs = 0;
        if (!s_bombTerminalAwaitingRulesClear)
            s_bombRulesPlantCycleArmed = true;
    } else if (s_bombRulesPlantCycleArmed || s_bombTerminalAwaitingRulesClear) {
        if (s_bombRulesPlantFalseSinceUs == 0)
            s_bombRulesPlantFalseSinceUs = nowUs;
        confirmedBombRulesExit =
            esp::data::IsBombRulesExitConfirmed(
                true,
                s_bombRulesPlantFalseSinceUs,
                nowUs);
        if (confirmedBombRulesExit) {
            if (s_bombRulesPlantCycleArmed && !s_bombTerminalHandledForCycle) {
                advanceBombEpoch();
                if (s_cachedPlantedMetaEntity != 0) {
                    s_explodedEntityTaintPtr = s_cachedPlantedMetaEntity;
                    s_explodedEntityTaintUntilUs =
                        nowUs + esp::data::kStalePlantedC4TaintUs;
                }
            }
            s_bombRulesPlantCycleArmed = false;
            s_bombRulesPlantFalseSinceUs = 0;
            s_bombLiveMetadataSeenForCycle = false;
            s_bombTerminalHandledForCycle = false;
            s_bombTerminalAwaitingRulesClear = false;
        }
    }
    // Drop edge: weapon entity / scene-node / sticky positions from a prior
    // round must not survive. Leaving only the weapon pointer cache cleared is
    // not enough; s_lastWorldC4Pos / s_lastDroppedBombPos still bias ESP.
    //
    // Do NOT advanceBombEpoch() here: a round-start dropped-to-carried edge is
    // normal and a full epoch wipe + carrier cache thrash made ESP look like a
    // total collapse right as round 2 began.
    if (bombDroppedRulesRoseThisTick || (s_prevBombDroppedByRules && !bombDroppedByRules)) {
        s_cachedWeaponRootCandidate = 0;
        s_mergedWeaponEntity = 0;
        s_mergedWeaponEntityUs = 0;
        s_mergedWeaponListOrdinal = 0;
        s_mergedWeaponC4SceneNode = 0;
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
        s_recentDropTickEdgeEntity = 0;
        s_recentDropTickEdge = 0;
        s_recentDropTickEdgeUntilUs = 0;
        s_ownerDetachedWeaponEntity = 0;
        s_recentOwnerAttachEntity = 0;
        s_recentOwnerAttachUntilUs = 0;
        s_freshInventoryC4CarrierSlot = -1;
        s_freshInventoryC4Entity = 0;
        s_freshInventoryC4CarrierUs = 0;
        s_lastWorldC4Entity = 0;
        s_lastWorldC4Pos = { NAN, NAN, NAN };
        s_lastWorldC4PosUs = 0;
        s_lastWorldC4NoOwner = false;
        s_lastWorldC4OwnerIdx = -1;
        s_lastWorldC4OwnerAlive = false;
        s_lastWorldC4OwnerNearby = false;
        s_lastDroppedBombPos = { NAN, NAN, NAN };
        s_lastDroppedBombPosUs = 0;
        s_lastVisibleBombPos = { NAN, NAN, NAN };
        s_lastVisibleBombBoundsMins = {};
        s_lastVisibleBombBoundsMaxs = {};
        s_lastVisibleBombBoundsValid = false;
        s_lastVisibleBombPosUs = 0;
        s_lastConfirmedBombState = {};
        s_lastConfirmedBombStateUs = 0;
    }
    
    
    
    
    
    {
        const bool freshLiveMetadata =
            esp::data::IsFreshLiveBombCycleMetadata(
                s_prevBombCachedTicking != 0,
                s_prevBombCachedTerminal,
                s_prevBombCachedBlowTime,
                currentGameTime);
        if (freshLiveMetadata && !s_bombTerminalAwaitingRulesClear) {
            s_bombLiveMetadataSeenForCycle = true;
            s_bombTerminalHandledForCycle = false;
        }
        const bool hadLiveBombLastFrame =
            (s_prevBombCachedTicking != 0) ||
            (std::isfinite(s_prevBombCachedBlowTime) && s_prevBombCachedBlowTime > 0.0f);
        const bool blowTimeElapsed =
            std::isfinite(s_prevBombCachedBlowTime) &&
            s_prevBombCachedBlowTime > 0.0f &&
            std::isfinite(currentGameTime) &&
            s_prevBombCachedBlowTime <= currentGameTime;
        const bool terminalOrElapsed =
            hadLiveBombLastFrame &&
            (blowTimeElapsed || s_prevBombCachedTerminal);
        if (esp::data::ShouldAdvanceBombTerminalEpoch(
                s_bombLiveMetadataSeenForCycle,
                s_bombTerminalHandledForCycle,
                terminalOrElapsed)) {
            advanceBombEpoch();
            s_bombRulesPlantCycleArmed = false;
            s_bombRulesPlantFalseSinceUs = 0;
            s_bombLiveMetadataSeenForCycle = false;
            s_bombTerminalHandledForCycle = true;
            s_bombTerminalAwaitingRulesClear = true;
            if (s_cachedPlantedMetaEntity != 0) {
                s_explodedEntityTaintPtr = s_cachedPlantedMetaEntity;
                s_explodedEntityTaintUntilUs = nowUs + esp::data::kStalePlantedC4TaintUs;
            }
        }
    }
    s_prevBombPlantedByRules = bombPlantedByRules;
    s_prevBombDroppedByRules = bombDroppedByRules;

    const uint64_t bombCacheEpoch = s_bombEpoch.load(std::memory_order_relaxed);
    if (s_bombCacheResetEpoch != bombCacheEpoch) {
        s_bombCacheResetEpoch = bombCacheEpoch;
        bombEpochJustWiped = true;
        clearBombTrackingCache();
        s_prevBombCachedTerminal = false;
    }

    
    
    
    
    
    
    
    
    
    s_prevBombCachedTicking = s_cachedPlantedTicking;
    s_prevBombCachedBlowTime = s_cachedPlantedBlowTime;
    s_prevBombCachedTerminal =
        (s_cachedPlantedHasExploded != 0) ||
        (s_cachedPlantedBombDefused != 0);
