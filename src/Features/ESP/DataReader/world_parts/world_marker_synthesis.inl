        bool worldPositionsOk = worldScanOk;
        const bool reuseCachedWorldUtilityDetails =
            worldScanOk &&
            worldPositionsOk &&
            wantsWorldUtilityData &&
            !shouldReadWorldUtilityDetails;

        int scannedDroppedWeaponCount = 0;
        int scannedDroppedBombCount = 0;
        int scannedUtilityEffectCount = 0;
        int scannedProjectileCount = 0;
        uintptr_t emittingWorldEntity = 0;
        int emittingWorldSlot = 0;
        uint64_t emittingWorldSampleUs = 0;
        auto pushWorldMarker = [&](WorldMarkerType type, const Vector3& pos, uint16_t weaponId, float lifeHint, uint64_t expiresAtUs) {
            if (!IsFiniteVec(pos))
                return;
            if (std::fabs(pos.x) < 1.0f && std::fabs(pos.y) < 1.0f && std::fabs(pos.z) < 1.0f)
                return;
            if (scannedMarkerCount >= static_cast<int>(std::size(scannedMarkers)))
                return;
            if (type == WorldMarkerType::DroppedWeapon && weaponId == kWeaponC4Id) {
                if (scannedDroppedBombCount >= esp::data::kMaxDroppedBombMarkers)
                    return;
                ++scannedDroppedBombCount;
            } else if (type == WorldMarkerType::DroppedWeapon) {
                if (scannedDroppedWeaponCount >= esp::data::kMaxDroppedWeaponMarkers)
                    return;
                ++scannedDroppedWeaponCount;
            } else {
                const bool isProjectile =
                    type == WorldMarkerType::SmokeProjectile ||
                    type == WorldMarkerType::MolotovProjectile ||
                    type == WorldMarkerType::DecoyProjectile;
                if (isProjectile) {
                    if (scannedProjectileCount >= esp::data::kMaxProjectileMarkers)
                        return;
                    ++scannedProjectileCount;
                } else {
                    if (scannedUtilityEffectCount >= esp::data::kMaxUtilityEffectMarkers)
                        return;
                    ++scannedUtilityEffectCount;
                }
            }
            WorldMarker& m = scannedMarkers[scannedMarkerCount++];
            m.valid = true;
            m.type = type;
            m.position = pos;
            m.weaponId = weaponId;
            m.lifeHint = lifeHint;
            m.expiresUs = expiresAtUs;
            m.sourceEntity = emittingWorldEntity;
            m.sourceSlot = static_cast<uint16_t>(emittingWorldSlot);
            m.sourceSampleUs = emittingWorldSampleUs;
        };

        // World domains run at independent cadences. Preserve markers owned by
        // domains that were not refreshed by this scan; otherwise a bomb-only
        // rescue pass can erase a valid smoke/inferno marker between utility
        // passes.
        for (int markerIndex = 0; markerIndex < s_worldMarkerCount && markerIndex < 256; ++markerIndex) {
            const WorldMarker& marker = s_worldMarkers[markerIndex];
            if (!marker.valid || (marker.expiresUs > 0 && marker.expiresUs <= nowUs))
                continue;

            esp::data::WorldMarkerDomain markerDomain =
                esp::data::WorldMarkerDomain::ActiveUtility;
            if (marker.type == WorldMarkerType::DroppedWeapon) {
                markerDomain = marker.weaponId == kWeaponC4Id
                    ? esp::data::WorldMarkerDomain::DroppedBomb
                    : esp::data::WorldMarkerDomain::DroppedItem;
            }
            const int sourceSlot = static_cast<int>(marker.sourceSlot);
            const bool validSourceSlot = sourceSlot >= esp::data::kFirstWorldEntitySlot &&
                sourceSlot <= kMaxTrackedWorldEntities;
            const bool sourceReplaced = validSourceSlot &&
                s_worldCandidateMask[sourceSlot] != 0 &&
                worldEntityReadComplete[sourceSlot] != 0 &&
                worldEntities[sourceSlot] != marker.sourceEntity;
            if (sourceReplaced)
                continue;
            const bool processedThisScan = validSourceSlot && worldProcessMask.test(sourceSlot);
            const bool retainIncompleteSlot = validSourceSlot &&
                esp::data::ShouldHoldWorldMarkerOnReadGap(
                    marker.sourceEntity, s_worldCandidateMask[sourceSlot] != 0,
                    worldEntityReadComplete[sourceSlot] != 0,
                    worldEntities[sourceSlot], worldBasicDetailsComplete[sourceSlot] != 0,
                    marker.sourceSampleUs, nowUs);
            if (!processedThisScan && (retainIncompleteSlot || esp::data::ShouldPreserveWorldMarker(
                    markerDomain,
                    worldDomainBombRescueDue,
                    worldDomainDroppedItemsDue,
                    worldDomainActiveUtilityDue))) {
                const int before = scannedMarkerCount;
                pushWorldMarker(
                    marker.type,
                    marker.position,
                    marker.weaponId,
                    marker.lifeHint,
                    marker.expiresUs);
                if (scannedMarkerCount > before) {
                    scannedMarkers[scannedMarkerCount - 1] = marker;
                    if (retainIncompleteSlot && s_worldCandidateMask[sourceSlot] != 0u)
                        ++worldMarkerReadGapHolds;
                }
            }
        }

        const bool gameTimeWrapped = (currentGameTime > 0.0f && s_prevWorldGameTime > 5.0f && (currentGameTime + 1.0f) < s_prevWorldGameTime);
        if (gameTimeWrapped || (currentGameTime > 0.0f && currentGameTime < 1.0f && s_prevWorldGameTime > 60.0f)) {
            ResetWorldUtilityTrackingState();
            std::memset(scannedMarkers, 0, sizeof(scannedMarkers));
            scannedMarkerCount = 0;
            scannedDroppedWeaponCount = 0;
            scannedDroppedBombCount = 0;
            scannedUtilityEffectCount = 0;
            scannedProjectileCount = 0;
            s_worldWarmupScans = 0;
            s_lastWorldUtilityDetailScanUs = 0;
            s_lastWorldUtilityProbeScanUs = 0;
        }
        if (currentGameTime > 0.0f)
            s_prevWorldGameTime = currentGameTime;
        if (s_worldWarmupScans < 2u)
            ++s_worldWarmupScans;
        const bool warmupWorldScan = (s_worldWarmupScans < 2u);

        if (intervalPerTick > 0.0001f && intervalPerTick < 0.10f)
            s_lastStableIntervalPerTick = intervalPerTick;
        const float safeIntervalPerTick = s_lastStableIntervalPerTick;
        // A frozen extrapolated clock is not a new measurement. Repeatedly
        // converting it into now + remaining would keep extending every timer.
        const float utilityGameTime = currentGameTimeFresh ||
            esp::data::IsWorldMarkerSourceFresh(s_lastStableGameTimeUs, nowUs)
                ? currentGameTime : 0.0f;
        auto calcRemainingFromTick = [&](int tickStart, float durationSec) -> float {
            return esp::data::CalculateUtilityRemainingFromTick(
                tickStart,
                durationSec,
                safeIntervalPerTick,
                utilityGameTime);
        };

        auto pushUtility = [&](bool signal,
                               bool terminal,
                               float remainingSec,
                               esp::data::UtilityTimerSource timerSource,
                               bool& latched,
                               uint64_t& startUs,
                               uint64_t& deadlineUs,
                               uint64_t fallbackDurationUs,
                               WorldMarkerType type,
                               uint16_t weaponId,
                               const Vector3& pos,
                               float lifeHint) {
            const uint64_t expiresAt = esp::data::UpdateUtilityMarkerDeadline(
                signal, terminal, remainingSec, nowUs, fallbackDurationUs, deadlineUs, timerSource);
            latched = expiresAt > nowUs;
            if (!latched) {
                startUs = 0;
                return;
            }
            if (startUs == 0)
                startUs = nowUs;
            pushWorldMarker(type, pos, weaponId, lifeHint, expiresAt);
        };

        constexpr float kSmokeDurationSec = esp::data::kSmokeDurationSec;
        constexpr float kDecoyDurationSec = esp::data::kDecoyDurationSec;
        constexpr float kExplosiveDurationSec = esp::data::kExplosiveDurationSec;
        constexpr uint64_t kSmokeFallbackUs = esp::data::kSmokeFallbackUs;
        constexpr uint64_t kDecoyFallbackUs = esp::data::kDecoyFallbackUs;
        constexpr uint64_t kExplosiveFallbackUs = esp::data::kExplosiveFallbackUs;
        struct DebugSample {
            int idx = 0;
            uint16_t itemId = 0;
            uint32_t owner = 0;
            int smokeTick = 0;
            int infernoTick = 0;
            int decoyTick = 0;
            int decoyClientTick = 0;
            int explodeTick = 0;
            int infernoFireCount = 0;
            int smokeActive = 0;
            float smokeRemaining = 0.0f;
            float infernoRemaining = 0.0f;
            float decoyRemaining = 0.0f;
            float explodeRemaining = 0.0f;
        };
        int dbgRawSmoke = 0, dbgRawInferno = 0, dbgRawDecoy = 0, dbgRawExplosive = 0;
        int dbgSignalSmoke = 0, dbgSignalInferno = 0, dbgSignalDecoy = 0, dbgSignalExplosive = 0;
        int dbgEvidenceEntities = 0, dbgNoSignalSamples = 0;
        int dbgTimerTick = 0, dbgTimerEffect = 0, dbgTimerStationary = 0, dbgTimerTerminal = 0;
        DebugSample dbgSamples[3] = {};
        auto bumpEvidenceCounter = [](bool evidence, uint8_t& counter) {
            if (!evidence) {
                counter = 0;
                return;
            }
            if (counter < 0xFFu)
                ++counter;
        };

        // Capacity selection happens after entity processing, at publication.
