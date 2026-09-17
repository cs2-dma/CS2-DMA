    const uint64_t worldSceneResetAge = nowUs - s_lastSceneResetUs.load(std::memory_order_relaxed);
    const bool worldScanAllowed =
        esp::data::IsWorldScanAllowed(liveWorldContext, worldSceneResetAge);
    const bool bombRescueScanAllowed =
        esp::data::IsWorldBombRescueScanAllowed(liveWorldContext, worldSceneResetAge);
    bool worldScanCommitted = false;

    static uint64_t s_worldCacheResetSerial = 0;
    static float s_prevWorldGameTime = 0.0f;
    static uint32_t s_worldWarmupScans = 0;
    static uint64_t s_lastWorldUtilityDetailScanUs = 0;
    static uint64_t s_lastWorldUtilityProbeScanUs = 0;
    static uint32_t s_worldDiscoveryShard = 0;
    static uint32_t s_worldIdleScanStreak = 0;
    static uint64_t s_lastWorldBombRescueScanUs = 0;
    static uint64_t s_lastWorldDroppedItemsScanUs = 0;
    static uint64_t s_lastWorldActiveUtilityScanUs = 0;
    static uint64_t s_lastWorldSlowDiscoveryScanUs = 0;
    static bool s_prevWorldLiveContext = false;

    const bool bombOnlyWorldMode =
        wantsBombConsumers &&
        !wantsDroppedItemMarkers &&
        !wantsWorldUtilityData;
    // Drop / undrop edges: stale C4 entity indices from the previous round must
    // not pin bomb rescue to a recycled ghost at the old origin.
    static bool s_prevWorldBombDroppedByRules = false;
    static bool s_prevWorldBombPlantedByRules = false;
    if ((bombDroppedByRules != s_prevWorldBombDroppedByRules) ||
        (bombPlantedByRules != s_prevWorldBombPlantedByRules) ||
        (!liveWorldContext && s_prevWorldLiveContext)) {
        std::memset(s_worldBombCandidateSlots, 0, sizeof(s_worldBombCandidateSlots));
        s_worldBombCandidateSlotCount = 0;
        s_lastWorldBombRescueScanUs = 0;
    }
    s_prevWorldBombDroppedByRules = bombDroppedByRules;
    s_prevWorldBombPlantedByRules = bombPlantedByRules;

    const bool hasKnownBombCandidateSlots = (s_worldBombCandidateSlotCount != 0u);
    
    
    
    
    
    const bool bombStateHasPosition =
        s_bombState.planted || (s_bombState.dropped && isValidWorldPos(s_bombState.position));
    const bool bombEntityCandidatePosValid =
        weaponC4Entity != 0 &&
        weaponC4PosValid &&
        isValidWorldPos(weaponC4WorldPos);
    const bool bombRulesDroppedWorldValidation =
        wantsBombConsumers &&
        bombDroppedByRules &&
        !bombPlantedByRules;
    const bool bombFallbackRescueNeeded =
        wantsBombConsumers &&
        !bombPlantedByRules &&
        (!bombStateHasPosition ||
         !bombEntityCandidatePosValid ||
         bombRulesDroppedWorldValidation);
    const bool confirmedDroppedBombPosition =
        s_bombState.dropped &&
        s_bombState.confidence >= esp::data::kDroppedC4ConfirmedScore &&
        isValidWorldPos(s_bombState.position);
    const bool periodicBombDiscoveryDue =
        esp::data::ShouldForceBombWorldDiscovery(
            bombRulesDroppedWorldValidation,
            hasKnownBombCandidateSlots,
            confirmedDroppedBombPosition,
            s_lastWorldSlowDiscoveryScanUs,
            nowUs);
    const bool bombRescueDiscoveryNeeded =
        bombFallbackRescueNeeded &&
        (!hasKnownBombCandidateSlots ||
         !confirmedDroppedBombPosition ||
         periodicBombDiscoveryDue);
    const bool forceBombWorldDiscovery =
        bombRescueDiscoveryNeeded &&
        !bombPlantedByRules &&
        wantsBombConsumers;

    const uint64_t worldCacheSceneResetSerial = s_sceneResetSerial.load(std::memory_order_relaxed);
    const bool worldContextReset =
        (s_worldCacheResetSerial != worldCacheSceneResetSerial) ||
        (!liveWorldContext && s_prevWorldLiveContext);
    if (worldContextReset) {
        s_worldCacheResetSerial = worldCacheSceneResetSerial;
        s_prevWorldGameTime = 0.0f;
        s_worldWarmupScans = 0;
        s_lastWorldUtilityDetailScanUs = 0;
        s_lastWorldUtilityProbeScanUs = 0;
        s_worldDiscoveryShard = 0;
        s_worldIdleScanStreak = 0;
        s_lastWorldBombRescueScanUs = 0;
        s_lastWorldDroppedItemsScanUs = 0;
        s_lastWorldActiveUtilityScanUs = 0;
        s_lastWorldSlowDiscoveryScanUs = 0;
        std::memset(s_worldBombCandidateSlots, 0, sizeof(s_worldBombCandidateSlots));
        s_worldBombCandidateSlotCount = 0;
        ResetWorldUtilityTrackingState();
        std::memset(s_worldMarkers, 0, sizeof(s_worldMarkers));
        s_worldMarkerCount = 0;
    }
    if (bombRoundChangedThisTick && !worldContextReset) {
        ResetWorldUtilityTrackingState();
        std::memset(s_worldMarkers, 0, sizeof(s_worldMarkers));
        s_worldMarkerCount = 0;
        s_worldWarmupScans = 0;
        s_lastWorldUtilityDetailScanUs = 0;
        s_lastWorldUtilityProbeScanUs = 0;
    }
    s_prevWorldLiveContext = liveWorldContext;

    const bool wantsGeneralWorldScan = wantsDroppedItemMarkers || wantsWorldUtilityData;
    const bool worldIdleDiscovery = esp::data::ShouldUseWorldIdleDiscovery(
        forceBombWorldDiscovery,
        wantsGeneralWorldScan,
        s_worldIdleScanStreak,
        s_worldTrackedIndexCount,
        s_worldMarkerCount);
    const esp::data::WorldDomainCadence worldCadence =
        esp::data::CalculateWorldDomainCadence(
            highestEntityIndex,
            wantsWorldProjectiles,
            forceBombWorldDiscovery,
            worldIdleDiscovery,
            s_worldWarmupScans);
    const bool heavyWorldCadence = highestEntityIndex > 1200;
    const uint64_t effectiveWorldUtilityDetailIntervalUs =
        worldCadence.utilityDetailIntervalUs;
    const uint64_t effectiveWorldUtilityProbeIntervalUs =
        worldCadence.utilityProbeIntervalUs;
    const uint32_t worldDiscoveryShardCount =
        worldCadence.discoveryShardCount;
    const uint32_t activeWorldDiscoveryShard =
        (worldDiscoveryShardCount > 1u) ? (s_worldDiscoveryShard % worldDiscoveryShardCount) : 0u;

    const uint64_t worldDroppedItemsIntervalUs =
        worldCadence.droppedItemsIntervalUs;
    const uint64_t worldActiveUtilityIntervalUs =
        worldCadence.activeUtilityIntervalUs;
    const uint64_t worldSlowDiscoveryIntervalUs =
        worldCadence.slowDiscoveryIntervalUs;
    const uint64_t worldBombRescueIntervalUs =
        worldCadence.bombRescueIntervalUs;

    const bool worldDomainBombRescueDue =
        bombRescueScanAllowed &&
        bombFallbackRescueNeeded &&
        (nowUs - s_lastWorldBombRescueScanUs) >= worldBombRescueIntervalUs;
    const bool worldDomainDroppedItemsDue =
        worldScanAllowed &&
        wantsDroppedItemMarkers &&
        (nowUs - s_lastWorldDroppedItemsScanUs) >= worldDroppedItemsIntervalUs;
    const bool worldDomainActiveUtilityDue =
        worldScanAllowed &&
        wantsWorldUtilityData &&
        (nowUs - s_lastWorldActiveUtilityScanUs) >= worldActiveUtilityIntervalUs;
    const bool worldDomainSlowDiscoveryDue =
        worldScanAllowed &&
        (wantsGeneralWorldScan || bombRescueDiscoveryNeeded) &&
        (nowUs - s_lastWorldSlowDiscoveryScanUs) >=
            (forceBombWorldDiscovery ? std::min<uint64_t>(worldSlowDiscoveryIntervalUs, 30000u)
                                     : worldSlowDiscoveryIntervalUs);

    const bool worldScanDue =
        worldDomainBombRescueDue ||
        worldDomainDroppedItemsDue ||
        worldDomainActiveUtilityDue ||
        worldDomainSlowDiscoveryDue;
    constexpr uint64_t kWorldHeavyLaneMaximumDeferralUs = 8000u;
    const bool worldMaximumDeferralElapsed =
        (worldDomainDroppedItemsDue &&
         (nowUs - s_lastWorldDroppedItemsScanUs) >=
             worldDroppedItemsIntervalUs + kWorldHeavyLaneMaximumDeferralUs) ||
        (worldDomainActiveUtilityDue &&
         (nowUs - s_lastWorldActiveUtilityScanUs) >=
             worldActiveUtilityIntervalUs + kWorldHeavyLaneMaximumDeferralUs) ||
        (worldDomainSlowDiscoveryDue &&
         (nowUs - s_lastWorldSlowDiscoveryScanUs) >=
             (forceBombWorldDiscovery
                 ? std::min<uint64_t>(worldSlowDiscoveryIntervalUs, 30000u)
                 : worldSlowDiscoveryIntervalUs) +
             kWorldHeavyLaneMaximumDeferralUs);
    const bool higherPriorityWorldLaneActive =
        _playerHierarchyActiveTick ||
        _playerAuxActiveTick ||
        _inventoryActiveTick ||
        _inventoryFullTick ||
        _boneReadsActiveTick;
    const bool shouldScanWorld =
        esp::data::ShouldRunScheduledWorldScan(
            worldScanDue,
            worldDomainBombRescueDue,
            higherPriorityWorldLaneActive,
            worldMaximumDeferralElapsed);
