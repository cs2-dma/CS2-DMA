    const uint64_t nowUs = TickNowUs();
    const uint64_t nowMs = nowUs / 1000u;

    uint16_t previewLocalWeaponId = s_localWeaponId;
    int previewLocalAmmoClip = s_localAmmoClip;
    bool previewLocalHasBomb = s_localHasBomb;
    SharedLocalIdentity localIdentity{
        .isDead = s_localIsDead,
        .health = s_localHealth,
        .armor = s_localArmor,
        .money = s_localMoney,
        .hasDefuser = s_localHasDefuser
    };
    memcpy(localIdentity.name, s_localName, sizeof(localIdentity.name));
    uint16_t previewLocalGrenadeIds[esp::PlayerData::kMaxGrenades] = {};
    memcpy(previewLocalGrenadeIds, s_localGrenadeIds, sizeof(previewLocalGrenadeIds));
    int previewLocalGrenadeCount = std::clamp(s_localGrenadeCount, 0, esp::PlayerData::kMaxGrenades);

    const LocalPlayerIndexHints localPlayerIndexHints{
        .controllerMaskBit = localControllerMaskBit,
        .pawnMaskBit = localMaskBit
    };
    const LocalPlayerIndexSource localPlayerIndexSource =
        ResolveLocalPlayerIndexSource(localPlayerIndexHints);
    const int localPlayerIndex = ResolveLocalPlayerIndex(localPlayerIndexHints);
    const bool localPlayerIndexValid = IsValidLocalPlayerIndex(localPlayerIndex);
    const bool localPlayerIndexHasLiveEvidence =
        IsLiveLocalPlayerIndexSource(localPlayerIndexSource);

    auto isLikelyViewMatrix = [](const view_matrix_t& matrix) -> bool {
        return esp::IsLikelyViewMatrix(matrix);
    };

    uintptr_t localPawnResolved = localPawn;
    int localTeamResolved = localTeamLiveResolved ? localTeam : 0;
    Vector3 localPosResolved = localPos;
    bool localPosResolvedValid = localPosReadValid;
    resolveSharedLocalIdentity(
        localPlayerIndexValid && localPlayerIndexHasLiveEvidence ? localPlayerIndex : -1,
        localIdentity);
    if (localPlayerIndexValid && localPlayerIndexHasLiveEvidence) {
        localPawnResolved = pawns[localPlayerIndex] ? pawns[localPlayerIndex] : localPawnResolved;
        if (liveTeamReads[localPlayerIndex] &&
            (teams[localPlayerIndex] == 1 || teams[localPlayerIndex] == 2 || teams[localPlayerIndex] == 3))
            localTeamResolved = teams[localPlayerIndex];
        if (coreReadFresh[localPlayerIndex] &&
            corePositionBytesRead[localPlayerIndex] ==
                sizeof(positions[localPlayerIndex]) &&
            isValidWorldPos(positions[localPlayerIndex])) {
            localPosResolved = positions[localPlayerIndex];
            localPosResolvedValid = true;
        } else if (!localIdentity.isDead && localPosReadValid &&
                   isValidWorldPos(localPos)) {
            localPosResolved = localPos;
            localPosResolvedValid = true;
        }
    }

    const bool localControllerPawnHandleValid =
        localControllerPawnHandle != 0u &&
        localControllerPawnHandle != 0xFFFFFFFFu;
    const bool localControllerTeamValid =
        localControllerTeam == 1 ||
        localControllerTeam == 2 ||
        localControllerTeam == 3;
    if (localControllerTeamValid)
        localTeamResolved = localControllerTeam;

    const bool localIdentityResolved =
        localPlayerIndexHasLiveEvidence ||
        localPawnResolved != 0 ||
        localControllerPawnHandleValid ||
        localControllerMaskBit > 0;
    const bool previousCommittedLocalTeamValid =
        (s_localTeam == 1 || s_localTeam == 2 || s_localTeam == 3);
    const bool localTeamTransitionDetected =
        previousCommittedLocalTeamValid &&
        ((localControllerTeamValid && localControllerTeam != s_localTeam) ||
         localTeamLikelySwitched);
    bool localTeamResolvedValid =
        localTeamResolved == 1 ||
        localTeamResolved == 2 ||
        localTeamResolved == 3;
    const bool localPawnPointerResolved = localPawnResolved != 0;
    const bool localPresenceLiveEvidence =
        localIdentityResolved ||
        localControllerTeamValid ||
        localPawnPointerResolved;
    if (!localTeamResolvedValid &&
        previousCommittedLocalTeamValid &&
        localPresenceLiveEvidence &&
        !localTeamTransitionDetected) {
        localTeamResolved = s_localTeam;
        localTeamResolvedValid = true;
    }

    promoteInGameFrame("commit_core_state");
    auto& refreshed = s_playerReadScratch.commitRefreshed;

    
    if (minimapBoundsValid) {
        auto hashMapBounds = [](const Vector3& mins, const Vector3& maxs) -> uint64_t {
            auto mix = [](uint64_t seed, uint64_t value) -> uint64_t {
                seed ^= value + 0x9E3779B97F4A7C15ull + (seed << 6) + (seed >> 2);
                return seed;
            };
            auto quantize = [](float value) -> uint64_t {
                if (!std::isfinite(value))
                    return 0ull;
                const long long scaled = static_cast<long long>(std::llround(static_cast<double>(value) * 4.0));
                return static_cast<uint64_t>(scaled);
            };

            uint64_t hash = 0x84222325CBF29CE4ull;
            hash = mix(hash, quantize(mins.x));
            hash = mix(hash, quantize(mins.y));
            hash = mix(hash, quantize(mins.z));
            hash = mix(hash, quantize(maxs.x));
            hash = mix(hash, quantize(maxs.y));
            hash = mix(hash, quantize(maxs.z));
            return hash;
        };

        static uint64_t s_pendingMapFingerprint = 0;
        static uint32_t s_pendingMapFingerprintCount = 0;
        static uint64_t s_pendingMapFingerprintResetSerial = 0;
        const uint64_t pendingMapFingerprintResetSerial = s_sceneResetSerial.load(std::memory_order_relaxed);
        if (s_pendingMapFingerprintResetSerial != pendingMapFingerprintResetSerial) {
            s_pendingMapFingerprintResetSerial = pendingMapFingerprintResetSerial;
            s_pendingMapFingerprint = 0;
            s_pendingMapFingerprintCount = 0;
        }
        const uint64_t currentMapFingerprint = hashMapBounds(minimapMins, minimapMaxs);
        const bool recentMapSceneReset = esp::data::IsRecentMapFingerprintSceneReset(
            s_lastSceneResetUs.load(std::memory_order_relaxed),
            nowUs);

        if (s_mapFingerprint == 0) {
            s_mapFingerprint = currentMapFingerprint;
            s_pendingMapFingerprint = 0;
            s_pendingMapFingerprintCount = 0;
        } else if (currentMapFingerprint != s_mapFingerprint) {
            if (recentMapSceneReset) {
                s_mapFingerprint = currentMapFingerprint;
                s_pendingMapFingerprint = 0;
                s_pendingMapFingerprintCount = 0;
            } else {
                if (s_pendingMapFingerprint != currentMapFingerprint) {
                    s_pendingMapFingerprint = currentMapFingerprint;
                    s_pendingMapFingerprintCount = 1;
                } else if (s_pendingMapFingerprintCount < 0xFFFFFFFFu) {
                    ++s_pendingMapFingerprintCount;
                }

                const float boundsDiffX = std::fabs(minimapMins.x - s_minimapMins.x) +
                                          std::fabs(minimapMaxs.x - s_minimapMaxs.x);
                const float boundsDiffY = std::fabs(minimapMins.y - s_minimapMins.y) +
                                          std::fabs(minimapMaxs.y - s_minimapMaxs.y);
                if (s_pendingMapFingerprintCount >= 2u &&
                    (boundsDiffX > 100.0f || boundsDiffY > 100.0f)) {
                    DmaLogPrintf(
                        "[INFO] Map fingerprint changed (bounds shifted by %.0f/%.0f) -> recalibrating map state",
                        static_cast<double>(boundsDiffX),
                        static_cast<double>(boundsDiffY));
                    s_mapEpoch.fetch_add(1, std::memory_order_relaxed);
                    s_bombEpoch.fetch_add(1, std::memory_order_relaxed);
                    s_mapFingerprint = currentMapFingerprint;
                    ResetActiveMapState();
                    BumpSceneReset(nowUs);
                    SetSceneWarmupState(
                        esp::SceneWarmupState::HierarchyWarming,
                        nowUs);
                    s_pendingMapFingerprint = 0;
                    s_pendingMapFingerprintCount = 0;
                }
            }
        } else {
            s_pendingMapFingerprint = 0;
            s_pendingMapFingerprintCount = 0;
        }
    }

    if (coreRosterCommitAllowed) {
        memcpy(s_prevPlayers, s_players, sizeof(s_prevPlayers));
        const uint64_t previousPlayerCoreCaptureUs =
            s_playerCoreCaptureTimeUs.load(std::memory_order_relaxed);
        s_prevPlayerCoreCaptureTimeUs.store(
            previousPlayerCoreCaptureUs,
            std::memory_order_relaxed);
        s_playerCoreCaptureTimeUs.store(
            playerCoreBatchCaptureUs,
            std::memory_order_relaxed);
        s_playerCoreGeneration.fetch_add(1, std::memory_order_relaxed);
    }
    // Report this attempt, not the quality of the last accepted batch.
    s_playerCoreBatchQuality.store(
        static_cast<uint8_t>(playerCoreBatchDecision),
        std::memory_order_relaxed);
    s_prevLocalPos = s_localPos;
    s_prevCaptureTimeUs = s_captureTimeUs;
    s_captureTimeUs = nowUs;

    if (localControllerPawnHandleValid)
        s_localPawnHandleLastSeen = localControllerPawnHandle;

    if (localPawnResolved == 0 && s_localPawn != 0) {
        s_localPawnFarewellPtr = s_localPawn;
        s_localPawnFarewellHandle = localControllerPawnHandleValid
            ? localControllerPawnHandle
            : s_localPawnHandleLastSeen;
        s_localPawnFarewellExpiryUs = nowUs + kLocalPawnFarewellWindowUs;
    } else if (localPawnResolved != 0) {
        s_localPawnFarewellPtr = 0;
        s_localPawnFarewellHandle = 0;
        s_localPawnFarewellExpiryUs = 0;
    } else if (s_localPawnFarewellExpiryUs != 0 && nowUs >= s_localPawnFarewellExpiryUs) {
        s_localPawnFarewellPtr = 0;
        s_localPawnFarewellHandle = 0;
        s_localPawnFarewellExpiryUs = 0;
    }
    const bool localPawnChanged = localPawnResolved != s_localPawn;
    s_localPawn = localPawnResolved;
    s_localPlayerIndex =
        (localPlayerIndexValid && localPlayerIndexHasLiveEvidence)
        ? localPlayerIndex
        : -1;
    s_localTeam = localTeamResolved;
    if (localPosResolvedValid) {
        s_localPos = localPosResolved;
        s_localPosValid = true;
        s_localPosUpdatedAtUs = nowUs;
    } else if (localPawnChanged) {
        s_localPos = {};
        s_localPosValid = false;
        s_localPosUpdatedAtUs = 0;
    }
    const bool localViewOffsetValid = localViewOffsetReadValid &&
        std::isfinite(localViewOffset.x) &&
        std::isfinite(localViewOffset.y) &&
        std::isfinite(localViewOffset.z) &&
        std::fabs(localViewOffset.x) <= 32.0f &&
        std::fabs(localViewOffset.y) <= 32.0f &&
        localViewOffset.z >= 8.0f &&
        localViewOffset.z <= 96.0f;
    if (localViewOffsetValid) {
        s_localViewOffset = localViewOffset;
        s_localViewOffsetValid = true;
        s_localViewOffsetUpdatedAtUs = nowUs;
    } else if (localPawnChanged) {
        s_localViewOffset = { 0.0f, 0.0f, 64.0f };
        s_localViewOffsetValid = false;
        s_localViewOffsetUpdatedAtUs = 0;
    }
    constexpr uint64_t kLocalAimPunchHoldUs = 50000;
    if (localPawnChanged || !wantsTargetRecoil) {
        s_localShotsFired = 0;
        s_localShotsFiredValid = false;
        s_localShotsUpdatedAtUs = 0;
    } else if (localShotsFiredValid) {
        s_localShotsFired = localShotsFired;
        s_localShotsFiredValid = true;
        s_localShotsUpdatedAtUs = nowUs;
    } else if (s_localShotsUpdatedAtUs == 0 || nowUs < s_localShotsUpdatedAtUs ||
               nowUs - s_localShotsUpdatedAtUs > kLocalAimPunchHoldUs) {
        s_localShotsFiredValid = false;
    }

    if (localPawnChanged || !wantsTargetRecoil ||
        (s_localShotsFiredValid && s_localShotsFired <= 0)) {
        s_localAimPunch = {};
        s_localAimPunchValid = false;
        s_localAimPunchUpdatedAtUs = 0;
    } else if (localAimPunchValid && localShotsFired > 0) {
        s_localAimPunch = localAimPunch;
        s_localShotsFired = localShotsFired;
        s_localAimPunchValid = true;
        s_localAimPunchUpdatedAtUs = nowUs;
    } else if (s_localAimPunchValid &&
               s_localAimPunchUpdatedAtUs != 0 &&
               nowUs >= s_localAimPunchUpdatedAtUs &&
               (nowUs - s_localAimPunchUpdatedAtUs) <= kLocalAimPunchHoldUs) {
        if (localShotsFiredValid)
            s_localShotsFired = localShotsFired;
    } else {
        s_localAimPunch = {};
        s_localAimPunchValid = false;
        s_localAimPunchUpdatedAtUs = 0;
    }
    if (std::isfinite(localFovSensitivityAdjust) &&
        localFovSensitivityAdjust >= 0.05f &&
        localFovSensitivityAdjust <= 2.0f) {
        s_fovSensitivityAdjust = localFovSensitivityAdjust;
    } else if (localPawnChanged) {
        s_fovSensitivityAdjust = 1.0f;
    }
    ApplySharedLocalIdentityState(localIdentity);
    s_localWeaponId = previewLocalWeaponId;
    s_localAmmoClip = previewLocalAmmoClip;
    if (localPawnChanged) {
        s_localAmmoValid = false;
        s_localAmmoUpdatedAtUs = 0;
    }
    s_localHasBomb = previewLocalHasBomb;
    s_localGrenadeCount = previewLocalGrenadeCount;
    std::copy(std::begin(previewLocalGrenadeIds), std::end(previewLocalGrenadeIds), std::begin(s_localGrenadeIds));
    if (fallbackViewMatrixBytesRead == sizeof(viewMatrix) &&
        fallbackViewMatrixSampleUs > 0 && isLikelyViewMatrix(viewMatrix)) {
        memcpy(&s_viewMatrix, &viewMatrix, sizeof(view_matrix_t));
        s_viewMatrixUpdatedAtUs = fallbackViewMatrixSampleUs;
    }
    if (std::isfinite(viewAngles.x) && std::isfinite(viewAngles.y) && std::isfinite(viewAngles.z))
        s_viewAngles = viewAngles;
    if (sensValue > 0.0f && sensValue < 100.0f) {
        std::lock_guard<std::mutex> lock(s_dataMutex);
        s_sensitivity = sensValue;
    }
    if (minimapBoundsValid) {
        s_minimapMins = minimapMins;
        s_minimapMaxs = minimapMaxs;
        s_hasMinimapBounds = true;
    }
    
    s_localMaskResolved = localMaskResolved;

#include "commit_players.inl"

    
    {
        
        
        const uint64_t lastBulkEvictUsForStale =
            s_lastBulkEvictionUs.load(std::memory_order_relaxed);
        const uint64_t nowUsForStale = TickNowUs();
        const bool inBulkRecoveryForStale =
            esp::data::IsWithinBulkRecoveryStaleWindow(lastBulkEvictUsForStale, nowUsForStale);
        for (int i = 0; i < 64; ++i) {
            const auto staleDecision = esp::data::EvaluatePlayerSlotStalePolicy({
                refreshed[i],
                s_players[i].valid,
                nowMs,
                s_playerLastSeenMs[i],
                kPlayerStaleEvictionMs,
                inBulkRecoveryForStale,
                s_players[i].staleFrames,
            });

            if (staleDecision.action == esp::data::PlayerSlotStaleAction::Refresh) {
                s_playerLastSeenMs[i] = nowMs;
                s_players[i].staleFrames = staleDecision.nextStaleFrames;
            } else if (staleDecision.action == esp::data::PlayerSlotStaleAction::Hold) {
                s_players[i].staleFrames = staleDecision.nextStaleFrames;
            } else if (staleDecision.action == esp::data::PlayerSlotStaleAction::Evict) {
                s_players[i] = {};
                RecordEspEvent({
                    .type = EspEventType::SlotEvictedStale,
                    .slot = static_cast<uint8_t>(i),
                    .param = static_cast<uint16_t>(
                        std::min<uint64_t>(staleDecision.elapsedMs, 0xFFFFu))
                });
            }
        }
    }
