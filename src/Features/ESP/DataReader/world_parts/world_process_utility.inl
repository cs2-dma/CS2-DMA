            bool smokeSubclassKnownNow = knownSmokeSubclass;
            bool decoySubclassKnownNow = knownDecoySubclass;
            bool heSubclassKnownNow = knownHeSubclass;
            bool infernoSubclassKnownNow = knownInfernoSubclass;
            
            const bool unknownUtilityProbeCandidate = isUnknownUtilityProbe[idx];
            if (unknownUtilityProbeCandidate)
                ++unknownUtilityProbeCount;
            const bool relevantUtilityEntity =
                wantsWorldUtilityData &&
                (isUtilityGrenade ||
                 knownSmokeSubclass ||
                 knownDecoySubclass ||
                 knownHeSubclass ||
                 knownInfernoSubclass ||
                 knownMolotovProjectileSubclass ||
                 unknownUtilityProbeCandidate);
            if (!relevantUtilityEntity)
                continue;
            const bool firstHistory = !s_worldUtilityHasHistory[idx];
            if (firstHistory) {
                s_worldUtilityHasHistory[idx] = true;
                refreshTrackedWorldIndex(idx);
            }

            const bool positionFresh =
                esp::data::IsWorldFieldReadComplete(
                    worldPositionReadBytes[idx],
                    sizeof(Vector3)) &&
                IsFiniteVec(pos);
            bool positionUnchanged = false;
            if (positionFresh && s_worldUtilityPositionSampleUs[idx] != 0u) {
                const float dx = pos.x - s_worldPrevPos[idx].x;
                const float dy = pos.y - s_worldPrevPos[idx].y;
                const float dz = pos.z - s_worldPrevPos[idx].z;
                positionUnchanged =
                    (dx * dx + dy * dy + dz * dz) <= (6.0f * 6.0f);
            }
            const bool stationaryConfirmed =
                esp::data::UpdateUtilityStationaryEvidence(
                    positionFresh,
                    positionUnchanged,
                    nowUs,
                    s_worldUtilityPositionSampleUs[idx],
                    s_worldUtilityStationarySinceUs[idx],
                    s_worldUtilityStationarySamples[idx]);

            const auto utilityFieldFresh = [&](DWORD bytesRead, size_t expectedBytes) {
                return !reuseCachedWorldUtilityDetails &&
                       esp::data::IsWorldFieldReadComplete(bytesRead, expectedBytes);
            };
            const bool smokeTickFresh =
                utilityFieldFresh(worldSmokeTickReadBytes[idx], sizeof(int));
            const bool smokeActiveFresh =
                utilityFieldFresh(worldSmokeActiveReadBytes[idx], sizeof(uint8_t));
            const bool smokeVolumeFresh =
                utilityFieldFresh(worldSmokeVolumeReadBytes[idx], sizeof(uint8_t));
            const bool smokeSpawnedFresh =
                utilityFieldFresh(worldSmokeSpawnedReadBytes[idx], sizeof(uint8_t));
            const bool infernoTickFresh =
                utilityFieldFresh(worldInfernoTickReadBytes[idx], sizeof(int));
            const bool infernoLifeFresh =
                utilityFieldFresh(worldInfernoLifeReadBytes[idx], sizeof(float));
            const bool infernoFireCountFresh =
                utilityFieldFresh(worldInfernoFireCountReadBytes[idx], sizeof(int));
            const bool infernoPostEffectFresh =
                utilityFieldFresh(worldInfernoPostEffectReadBytes[idx], sizeof(uint8_t));
            const bool decoyTickFresh =
                utilityFieldFresh(worldDecoyTickReadBytes[idx], sizeof(int));
            const bool decoyClientTickFresh =
                utilityFieldFresh(worldDecoyClientTickReadBytes[idx], sizeof(int));
            const bool explodeTickFresh =
                utilityFieldFresh(worldExplodeTickReadBytes[idx], sizeof(int));
            const bool velocityFresh =
                utilityFieldFresh(worldVelocityReadBytes[idx], sizeof(Vector3));

            using Field = esp::data::UtilityField;
            auto fieldAvailable = [&](Field field, bool fresh) {
                return esp::data::UpdateUtilityFieldFreshness(
                    s_worldUtilityFieldTimes[idx], field, fresh, nowUs);
            };
            const bool smokeTickAvailable = fieldAvailable(Field::SmokeTick, smokeTickFresh);
            const bool smokeActiveAvailable = fieldAvailable(Field::SmokeActive, smokeActiveFresh);
            const bool smokeVolumeAvailable = fieldAvailable(Field::SmokeVolume, smokeVolumeFresh);
            const bool smokeSpawnedAvailable = fieldAvailable(Field::SmokeSpawned, smokeSpawnedFresh);
            const bool infernoTickAvailable = fieldAvailable(Field::InfernoTick, infernoTickFresh);
            const bool infernoLifeAvailable = fieldAvailable(Field::InfernoLife, infernoLifeFresh);
            const bool infernoFireCountAvailable = fieldAvailable(Field::InfernoFireCount, infernoFireCountFresh);
            const bool infernoPostEffectAvailable = fieldAvailable(Field::InfernoPostEffect, infernoPostEffectFresh);
            const bool decoyTickAvailable = fieldAvailable(Field::DecoyTick, decoyTickFresh);
            const bool decoyClientTickAvailable = fieldAvailable(Field::DecoyClientTick, decoyClientTickFresh);
            const bool explodeTickAvailable = fieldAvailable(Field::ExplodeTick, explodeTickFresh);
            const bool velocityAvailable = fieldAvailable(Field::Velocity, velocityFresh);

            float infernoLife =
                infernoLifeFresh
                    ? worldInfernoLife[idx]
                    : (infernoLifeAvailable ? s_worldPrevInfernoLife[idx] : 0.0f);
            if (!std::isfinite(infernoLife) || infernoLife < 0.0f || infernoLife > 30.0f)
                infernoLife = 0.0f;

            const int smokeTick =
                smokeTickFresh
                    ? worldSmokeTick[idx]
                    : (smokeTickAvailable ? s_worldPrevSmokeTick[idx] : 0);
            const uint8_t smokeActive =
                smokeActiveFresh
                    ? worldSmokeActive[idx]
                    : (smokeActiveAvailable ? s_worldPrevSmokeActive[idx] : 0u);
            const uint8_t smokeVolumeDataReceived =
                smokeVolumeFresh
                    ? worldSmokeVolumeDataReceived[idx]
                    : (smokeVolumeAvailable ? s_worldPrevSmokeVolumeDataReceived[idx] : 0u);
            const uint8_t smokeEffectSpawned =
                smokeSpawnedFresh
                    ? worldSmokeEffectSpawned[idx]
                    : (smokeSpawnedAvailable ? s_worldPrevSmokeEffectSpawned[idx] : 0u);
            const int infernoTick =
                infernoTickFresh
                    ? worldInfernoTick[idx]
                    : (infernoTickAvailable ? s_worldPrevInfernoTick[idx] : 0);
            const int infernoFireCount =
                infernoFireCountFresh
                    ? worldInfernoFireCount[idx]
                    : (infernoFireCountAvailable ? s_worldPrevInfernoFireCount[idx] : 0);
            const uint8_t infernoInPostEffect =
                infernoPostEffectFresh
                    ? worldInfernoInPostEffect[idx]
                    : (infernoPostEffectAvailable ? s_worldPrevInfernoInPostEffect[idx] : 0u);
            const int decoyTick =
                decoyTickFresh
                    ? worldDecoyTick[idx]
                    : (decoyTickAvailable ? s_worldPrevDecoyTick[idx] : 0);
            const int decoyClientTick =
                decoyClientTickFresh
                    ? worldDecoyClientTick[idx]
                    : (decoyClientTickAvailable ? s_worldPrevDecoyClientTick[idx] : 0);
            const int explodeTick =
                explodeTickFresh
                    ? worldExplodeTick[idx]
                    : (explodeTickAvailable ? s_worldPrevExplodeTick[idx] : 0);

            constexpr int kMinUsefulTick = 4;
            bool smokeTickValid = (smokeTick >= kMinUsefulTick && smokeTick < 20000000);
            bool infernoTickValid = (infernoTick >= kMinUsefulTick && infernoTick < 20000000);
            bool decoyTickValid = (decoyTick >= kMinUsefulTick && decoyTick < 20000000);
            bool decoyClientTickValid = (decoyClientTick >= kMinUsefulTick && decoyClientTick < 20000000);
            bool explodeTickValid = (explodeTick >= kMinUsefulTick && explodeTick < 20000000);
            const bool smokeActiveSane = (smokeActive <= 1);
            const bool infernoPostEffectSane = (infernoInPostEffect <= 1);
            const bool infernoFireCountSane = (infernoFireCount >= 0 && infernoFireCount <= 64);
            const bool infernoLifeValid = (infernoLife >= 0.5f && infernoLife <= 30.0f);
            const bool infernoStrongState =
                (infernoLifeValid || infernoSubclassKnownNow) &&
                infernoFireCountAvailable &&
                infernoFireCountSane &&
                (infernoFireCount > 0) &&
                (!infernoPostEffectSane || infernoInPostEffect == 0);
            const bool smokeEffectState = esp::data::HasSmokeActivation(
                smokeActiveAvailable, smokeActive, smokeSpawnedAvailable, smokeEffectSpawned);
            const bool decoyTicksAligned = decoyTickValid && decoyClientTickValid && std::abs(decoyTick - decoyClientTick) <= 8;
            const bool decoyTimingValid = decoyTickValid || decoyClientTickValid;
            const bool suspiciousSharedTicks =
                smokeTickValid &&
                infernoTickValid &&
                decoyTickValid &&
                (smokeTick == infernoTick) &&
                (smokeTick == decoyTick);
            if (suspiciousSharedTicks &&
                ((smokeTick <= 64) || !smokeActiveSane || (!isSmokeGrenade && !knownSmokeSubclass && !infernoStrongState && !isDecoyGrenade && !knownDecoySubclass))) {
                smokeTickValid = false;
                infernoTickValid = false;
                decoyTickValid = false;
                explodeTickValid = false;
                decoyClientTickValid = false;
            }
            if (smokeTickValid && !isSmokeGrenade)
                smokeTickValid = smokeSubclassKnownNow || unknownUtilityProbeCandidate;
            if (decoyTickValid && !isDecoyGrenade)
                decoyTickValid = decoySubclassKnownNow || unknownUtilityProbeCandidate;
            if (explodeTickValid && !isHeGrenade)
                explodeTickValid = heSubclassKnownNow || unknownUtilityProbeCandidate;
            if (infernoTickValid && !isInfernoGrenade && !infernoSubclassKnownNow && !infernoStrongState && !unknownUtilityProbeCandidate)
                infernoTickValid = false;

            const float infernoDurationSec = esp::data::ResolveInfernoDuration(
                infernoLife, infernoLifeAvailable);
            const uint64_t infernoFallbackUs = static_cast<uint64_t>(infernoDurationSec * 1000000.0f);
            const float smokeRemaining = calcRemainingFromTick(smokeTick, kSmokeDurationSec);
            const float infernoRemaining = calcRemainingFromTick(infernoTick, infernoDurationSec);
            const int decoyBestTick = decoyTickValid ? decoyTick : (decoyClientTickValid ? decoyClientTick : 0);
            const float decoyRemaining = calcRemainingFromTick(decoyBestTick, kDecoyDurationSec);
            const float explodeRemaining = calcRemainingFromTick(explodeTick, kExplosiveDurationSec);
            const bool smokeTickExpired =
                smokeTickValid && esp::data::IsUtilityTickExpired(
                    smokeTick, kSmokeDurationSec, safeIntervalPerTick, utilityGameTime);
            const bool infernoTickExpired =
                infernoTickValid && esp::data::IsUtilityTickExpired(
                    infernoTick, infernoDurationSec, safeIntervalPerTick, utilityGameTime);
            const bool decoyTickExpired =
                decoyTimingValid && esp::data::IsUtilityTickExpired(
                    decoyBestTick, kDecoyDurationSec, safeIntervalPerTick, utilityGameTime);
            const bool explodeTickExpired =
                explodeTickValid && esp::data::IsUtilityTickExpired(
                    explodeTick, kExplosiveDurationSec, safeIntervalPerTick, utilityGameTime);

            const bool rawSmokeEvidence =
                (isSmokeGrenade || smokeSubclassKnownNow || unknownUtilityProbeCandidate) &&
                smokeActiveSane &&
                ((smokeTickValid && (smokeRemaining > 0.0f || smokeEffectState)) ||
                 (!smokeTickValid && smokeEffectState));
            const bool infernoEffectIdentity =
                exactClass == esp::data::WorldEntityClass::Inferno ||
                knownInfernoSubclass;
            const bool infernoIdentityEvidence =
                (infernoTickValid || infernoStrongState) &&
                (infernoEffectIdentity || unknownUtilityProbeCandidate);
            bumpEvidenceCounter(rawSmokeEvidence, s_worldSmokeEvidenceCount[idx]);
            bumpEvidenceCounter(infernoIdentityEvidence, s_worldInfernoEvidenceCount[idx]);
            const bool infernoTypeConfirmed =
                infernoEffectIdentity ||
                s_worldInfernoEvidenceCount[idx] >= 1u;
            const bool rawInfernoEvidence =
                infernoTypeConfirmed &&
                (infernoTickValid || infernoStrongState) &&
                (infernoRemaining > 0.0f || infernoStrongState);
            const bool rawDecoyEvidence =
                (isDecoyGrenade || decoySubclassKnownNow || unknownUtilityProbeCandidate) &&
                decoyTimingValid &&
                (decoyRemaining > 0.0f || decoyTicksAligned || decoyClientTickValid);
            const bool rawExplosiveEvidence =
                (isHeGrenade || heSubclassKnownNow || unknownUtilityProbeCandidate) &&
                explodeTickValid &&
                (explodeRemaining > 0.0f);
            const bool rawEvidence =
                rawSmokeEvidence ||
                rawInfernoEvidence ||
                rawDecoyEvidence ||
                rawExplosiveEvidence;

            if (!smokeSubclassKnownNow && rawSmokeEvidence) {
                rememberTrackedSubclass(s_worldSmokeSubclassIds, subclassId);
                smokeSubclassKnownNow = true;
            }
            if (!decoySubclassKnownNow && rawDecoyEvidence) {
                rememberTrackedSubclass(s_worldDecoySubclassIds, subclassId);
                decoySubclassKnownNow = true;
            }
            if (!heSubclassKnownNow && rawExplosiveEvidence) {
                rememberTrackedSubclass(s_worldHeSubclassIds, subclassId);
                heSubclassKnownNow = true;
            }
            if (!infernoSubclassKnownNow && rawInfernoEvidence) {
                rememberTrackedSubclass(s_worldInfernoSubclassIds, subclassId);
                infernoSubclassKnownNow = true;
            }

            bumpEvidenceCounter(rawDecoyEvidence, s_worldDecoyEvidenceCount[idx]);
            bumpEvidenceCounter(rawExplosiveEvidence, s_worldExplosiveEvidenceCount[idx]);

            const bool smokeIdentityConfirmed =
                isSmokeGrenade || smokeSubclassKnownNow;
            const bool decoyIdentityConfirmed =
                isDecoyGrenade || decoySubclassKnownNow;
            const bool explosiveIdentityConfirmed =
                isHeGrenade || heSubclassKnownNow;
            const bool smokeTickKnown =
                smokeTickValid &&
                (!esp::data::IsUtilityRemainingUnknown(smokeRemaining) || smokeTickExpired);
            const bool infernoTickKnown =
                infernoTickValid &&
                (!esp::data::IsUtilityRemainingUnknown(infernoRemaining) || infernoTickExpired);
            const bool decoyTickKnown =
                decoyTimingValid &&
                (!esp::data::IsUtilityRemainingUnknown(decoyRemaining) || decoyTickExpired);
            const bool explodeTickKnown =
                explodeTickValid &&
                (!esp::data::IsUtilityRemainingUnknown(explodeRemaining) || explodeTickExpired);
            const bool smokeExplicitTerminal =
                s_worldSmokeLatched[idx] &&
                smokeTickKnown &&
                smokeRemaining <= 0.0f;
            const bool infernoExplicitTerminal =
                s_worldInfernoLatched[idx] &&
                infernoPostEffectFresh &&
                infernoPostEffectSane &&
                infernoInPostEffect != 0u;
            const bool smokeStationaryFallbackAllowed = false;
            const bool decoyStationaryFallbackAllowed =
                exactClass == esp::data::WorldEntityClass::DecoyProjectile ||
                knownDecoySubclass;

            const esp::data::UtilityTimerDecision smokeTimer =
                esp::data::ResolveUtilityTimer(
                    smokeIdentityConfirmed,
                    smokeExplicitTerminal,
                    smokeTickKnown,
                    smokeTickExpired ? 0.0f : smokeRemaining,
                    smokeEffectState,
                    smokeStationaryFallbackAllowed,
                    stationaryConfirmed,
                    s_worldUtilityStationarySinceUs[idx],
                    nowUs,
                    kSmokeDurationSec);
            const esp::data::UtilityTimerDecision infernoTimer =
                esp::data::ResolveUtilityTimer(
                    infernoTypeConfirmed,
                    infernoExplicitTerminal,
                    infernoTickKnown,
                    infernoTickExpired ? 0.0f : infernoRemaining,
                    infernoStrongState,
                    infernoEffectIdentity,
                    stationaryConfirmed,
                    s_worldUtilityStationarySinceUs[idx],
                    nowUs,
                    infernoDurationSec);
            const esp::data::UtilityTimerDecision decoyTimer =
                esp::data::ResolveUtilityTimer(
                    decoyIdentityConfirmed,
                    false,
                    decoyTickKnown,
                    decoyTickExpired ? 0.0f : decoyRemaining,
                    false,
                    decoyStationaryFallbackAllowed,
                    stationaryConfirmed,
                    s_worldUtilityStationarySinceUs[idx],
                    nowUs,
                    kDecoyDurationSec);
            const esp::data::UtilityTimerDecision explosiveTimer =
                esp::data::ResolveUtilityTimer(
                    explosiveIdentityConfirmed,
                    false,
                    explodeTickKnown,
                    explodeTickExpired ? 0.0f : explodeRemaining,
                    false,
                    false,
                    false,
                    0u,
                    nowUs,
                    kExplosiveDurationSec);

            auto storeUtilityHistory = [&]() {
                if (positionFresh)
                    s_worldPrevPos[idx] = pos;
                if (smokeTickFresh) s_worldPrevSmokeTick[idx] = smokeTick;
                if (smokeActiveFresh) s_worldPrevSmokeActive[idx] = smokeActive;
                if (smokeVolumeFresh) s_worldPrevSmokeVolumeDataReceived[idx] = smokeVolumeDataReceived;
                if (smokeSpawnedFresh) s_worldPrevSmokeEffectSpawned[idx] = smokeEffectSpawned;
                if (infernoTickFresh) s_worldPrevInfernoTick[idx] = infernoTick;
                if (infernoLifeFresh) s_worldPrevInfernoLife[idx] = infernoLife;
                if (infernoFireCountFresh) s_worldPrevInfernoFireCount[idx] = infernoFireCount;
                if (infernoPostEffectFresh) s_worldPrevInfernoInPostEffect[idx] = infernoInPostEffect;
                if (decoyTickFresh) s_worldPrevDecoyTick[idx] = decoyTick;
                if (decoyClientTickFresh) s_worldPrevDecoyClientTick[idx] = decoyClientTick;
                if (explodeTickFresh) s_worldPrevExplodeTick[idx] = explodeTick;
                if (velocityFresh)
                    s_worldPrevVelocity[idx] = worldVelocities[idx];
            };
            auto pushProjectileMarker = [&](bool suppressSmoke, bool suppressInferno, bool suppressDecoy) {
                if (!wantsWorldProjectiles)
                    return;
                const Vector3 velocity =
                    velocityFresh ? worldVelocities[idx] :
                        (velocityAvailable ? s_worldPrevVelocity[idx] : Vector3{});
                const bool finiteVelocity = IsFiniteVec(velocity);
                const float speedSq =
                    finiteVelocity
                    ? (velocity.x * velocity.x + velocity.y * velocity.y + velocity.z * velocity.z)
                    : 0.0f;
                const bool movingProjectile = speedSq > (18.0f * 18.0f);
                const bool likelyProjectile = hasOwner || movingProjectile;

                if (smokeIdentityConfirmed && !suppressSmoke && likelyProjectile)
                    pushWorldMarker(WorldMarkerType::SmokeProjectile, pos, itemId != 0 ? itemId : 45, 0.0f, nowUs + esp::data::kProjectileMarkerHoldUs);
                else if (!infernoEffectIdentity && (isInfernoGrenade || knownMolotovProjectileSubclass) && !suppressInferno && likelyProjectile)
                    pushWorldMarker(WorldMarkerType::MolotovProjectile, pos, itemId != 0 ? itemId : 46, 0.0f, nowUs + esp::data::kProjectileMarkerHoldUs);
                else if (decoyIdentityConfirmed && !suppressDecoy && likelyProjectile)
                    pushWorldMarker(WorldMarkerType::DecoyProjectile, pos, itemId != 0 ? itemId : 47, 0.0f, nowUs + esp::data::kProjectileMarkerHoldUs);

                if (!infernoEffectIdentity && subclassId != 0u &&
                    (isInfernoGrenade || knownMolotovProjectileSubclass) &&
                    likelyProjectile)
                    rememberTrackedSubclass(s_worldMolotovSubclassIds, subclassId);
            };
            const bool exactEntityClassKnown =
                worldClassKinds[idx] != static_cast<uint8_t>(esp::data::WorldEntityClass::Unknown);
            const bool deferPersistentUtility =
                (firstHistory && !exactEntityClassKnown) ||
                (warmupWorldScan && unknownUtilityProbeCandidate);
            if (deferPersistentUtility) {
                pushProjectileMarker(rawSmokeEvidence, rawInfernoEvidence, rawDecoyEvidence);
                storeUtilityHistory();
                continue;
            }

            const bool smokeSignal = wantsWorldUtilityMarkers && smokeTimer.active;
            const bool infernoSignal = wantsWorldUtilityMarkers && infernoTimer.active;
            const bool decoySignal = wantsWorldUtilityMarkers && decoyTimer.active;
            const bool explosiveSignal = wantsWorldUtilityMarkers && explosiveTimer.active;

            if (subclassId != 0u) {
                if (isSmokeGrenade && (smokeSignal || smokeTickValid))
                    rememberTrackedSubclass(s_worldSmokeSubclassIds, subclassId);
                else if (isDecoyGrenade && (decoySignal || decoyTickValid || decoyTicksAligned))
                    rememberTrackedSubclass(s_worldDecoySubclassIds, subclassId);
                else if (isHeGrenade && (explosiveSignal || explodeTickValid))
                    rememberTrackedSubclass(s_worldHeSubclassIds, subclassId);
                else if (infernoSignal && !isInfernoGrenade)
                    rememberTrackedSubclass(s_worldInfernoSubclassIds, subclassId);
            }

            if (kDebugWorldUtility) {
                auto countTimerSource = [&](const esp::data::UtilityTimerDecision& timer) {
                    if (timer.terminal) {
                        ++dbgTimerTerminal;
                        return;
                    }
                    switch (timer.source) {
                    case esp::data::UtilityTimerSource::GameTick:
                        ++dbgTimerTick;
                        break;
                    case esp::data::UtilityTimerSource::EffectState:
                        ++dbgTimerEffect;
                        break;
                    case esp::data::UtilityTimerSource::StationaryFallback:
                        ++dbgTimerStationary;
                        break;
                    default:
                        break;
                    }
                };
                countTimerSource(smokeTimer);
                countTimerSource(infernoTimer);
                countTimerSource(decoyTimer);
                countTimerSource(explosiveTimer);
                if (smokeTickValid) ++dbgRawSmoke;
                if (infernoTickValid || infernoStrongState) ++dbgRawInferno;
                if (decoyTickValid) ++dbgRawDecoy;
                if (explodeTickValid) ++dbgRawExplosive;
                if (rawEvidence) ++dbgEvidenceEntities;
                if (smokeSignal) ++dbgSignalSmoke;
                if (infernoSignal) ++dbgSignalInferno;
                if (decoySignal) ++dbgSignalDecoy;
                if (explosiveSignal) ++dbgSignalExplosive;
                if (rawEvidence && !smokeSignal && !infernoSignal && !decoySignal && !explosiveSignal && dbgNoSignalSamples < 3) {
                    DebugSample& s = dbgSamples[dbgNoSignalSamples++];
                    s.idx = idx;
                    s.itemId = itemId;
                    s.owner = owner;
                    s.smokeTick = smokeTick;
                    s.infernoTick = infernoTick;
                    s.decoyTick = decoyTick;
                    s.decoyClientTick = decoyClientTick;
                    s.explodeTick = explodeTick;
                    s.infernoFireCount = infernoFireCount;
                    s.smokeActive = static_cast<int>(smokeActive);
                    s.smokeRemaining = smokeRemaining;
                    s.infernoRemaining = infernoRemaining;
                    s.decoyRemaining = decoyRemaining;
                    s.explodeRemaining = explodeRemaining;
                }
            }

            pushUtility(smokeSignal, smokeTimer.terminal, smokeTimer.remainingSec, smokeTimer.source, s_worldSmokeLatched[idx], s_worldSmokeStartUs[idx], s_worldUtilityDeadlinesUs[idx][0], kSmokeFallbackUs, WorldMarkerType::Smoke, 45, pos, 0.0f);
            pushUtility(infernoSignal, infernoTimer.terminal, infernoTimer.remainingSec, infernoTimer.source, s_worldInfernoLatched[idx], s_worldInfernoStartUs[idx], s_worldUtilityDeadlinesUs[idx][1], infernoFallbackUs, WorldMarkerType::Inferno, isIncendiaryGrenade ? 48 : 46, pos, infernoLife);
            pushUtility(decoySignal, decoyTimer.terminal, decoyTimer.remainingSec, decoyTimer.source, s_worldDecoyLatched[idx], s_worldDecoyStartUs[idx], s_worldUtilityDeadlinesUs[idx][2], kDecoyFallbackUs, WorldMarkerType::Decoy, 47, pos, 0.0f);
            pushUtility(explosiveSignal, explosiveTimer.terminal, explosiveTimer.remainingSec, explosiveTimer.source, s_worldExplosiveLatched[idx], s_worldExplosiveStartUs[idx], s_worldUtilityDeadlinesUs[idx][3], kExplosiveFallbackUs, WorldMarkerType::Explosive, 44, pos, 0.0f);

            pushProjectileMarker(smokeSignal || smokeTimer.terminal,
                infernoSignal || infernoTimer.terminal, decoySignal || decoyTimer.terminal);
            storeUtilityHistory();
