    static int s_lastResolvedBombCarrierSlot = -1;
    static uint64_t s_lastResolvedBombCarrierUs = 0;
    static uint64_t s_bombCarrierCacheResetEpoch = 0;
    const uint64_t bombCarrierEpoch = s_bombEpoch.load(std::memory_order_relaxed);
    if (s_bombCarrierCacheResetEpoch != bombCarrierEpoch) {
        s_bombCarrierCacheResetEpoch = bombCarrierEpoch;
        s_lastResolvedBombCarrierSlot = -1;
        s_lastResolvedBombCarrierUs = 0;
    }

    auto isResolvedBombCandidate = [&](int idx) -> bool {
        if (idx < 0 || idx >= 64)
            return false;
        if (!pawns[idx] || healths[idx] <= 0 || lifeStates[idx] != 0)
            return false;
        if (teams[idx] != esp::data::kBombCarrierTeamT)
            return false;
        return true;
    };
    auto hasBombSignal = [&](int idx) -> bool {
        if (!isResolvedBombCandidate(idx))
            return false;
        const uint16_t weaponId = (weaponIds[idx] < 20000u) ? weaponIds[idx] : 0u;
        return bombCarrierBySlot[idx] || inventoryHasBombBySlot[idx] || weaponId == kWeaponC4Id;
    };

    int resolvedBombCarrierSlot = -1;
    bool resolvedBombCarrierFromCurrentEvidence = false;
    const bool bombDroppedResolved =
        !bombPlantedNow &&
        droppedBombPosValid;
    const bool lastCarrierStillFresh =
        esp::data::IsResolvedBombCarrierFresh(
            s_lastResolvedBombCarrierSlot,
            s_lastResolvedBombCarrierUs,
            nowUs);
    auto selectBestBombSignalCarrier = [&]() -> int {
        int bestSlot = -1;
        int bestScore = -1000000;
        for (int resolvedIdx = 0; resolvedIdx < playerResolvedSlotCount; ++resolvedIdx) {
            const int i = playerResolvedSlots[resolvedIdx];
            if (!hasBombSignal(i))
                continue;

            const uint16_t weaponId = (weaponIds[i] < 20000u) ? weaponIds[i] : 0u;
            int score = 0;
            if (bombCarrierBySlot[i])
                score += 100;
            if (inventoryHasBombBySlot[i])
                score += 40;
            if (weaponId == kWeaponC4Id)
                score += 18;
            if (i == s_lastResolvedBombCarrierSlot)
                score += 8;

            if (score > bestScore) {
                bestScore = score;
                bestSlot = i;
            }
        }
        return bestSlot;
    };
    if (!bombPlantedNow && !bombDroppedResolved) {
        if (treatWeaponC4OwnerAsCarrier &&
            isResolvedBombCandidate(weaponC4OwnerPlayerIndex)) {
            resolvedBombCarrierSlot = weaponC4OwnerPlayerIndex;
            resolvedBombCarrierFromCurrentEvidence = true;
        } else {
            resolvedBombCarrierSlot = selectBestBombSignalCarrier();
            resolvedBombCarrierFromCurrentEvidence = resolvedBombCarrierSlot >= 0;
            if (resolvedBombCarrierSlot < 0 &&
                lastCarrierStillFresh &&
                isResolvedBombCandidate(s_lastResolvedBombCarrierSlot)) {
                resolvedBombCarrierSlot = s_lastResolvedBombCarrierSlot;
            }
        }
    }

    for (int i = 0; i < 64; ++i) {
        const bool isResolvedCarrier = (i == resolvedBombCarrierSlot);
        bombCarrierBySlot[i] = isResolvedCarrier;
        inventoryHasBombBySlot[i] = false;
    }
    if (resolvedBombCarrierSlot >= 0 && resolvedBombCarrierFromCurrentEvidence) {
        s_lastResolvedBombCarrierSlot = resolvedBombCarrierSlot;
        s_lastResolvedBombCarrierUs = nowUs;
    } else if (esp::data::ShouldExpireResolvedBombCarrier(s_lastResolvedBombCarrierUs, nowUs)) {
        s_lastResolvedBombCarrierSlot = -1;
        s_lastResolvedBombCarrierUs = 0;
    }

    static uintptr_t s_cachedCommittedWeaponPawns[64] = {};
    static uint32_t s_cachedCommittedWeaponHandles[64] = {};
    static uintptr_t s_cachedCommittedWeaponEntities[64] = {};
    static uint16_t s_cachedCommittedWeaponIds[64] = {};
    static int s_cachedCommittedAmmoClips[64] = {};
    static uint64_t s_committedWeaponCacheResetSerial = 0;
    const uint64_t committedWeaponResetSerial =
        s_sceneResetSerial.load(std::memory_order_relaxed);
    if (s_committedWeaponCacheResetSerial != committedWeaponResetSerial) {
        s_committedWeaponCacheResetSerial = committedWeaponResetSerial;
        memset(s_cachedCommittedWeaponPawns, 0, sizeof(s_cachedCommittedWeaponPawns));
        memset(s_cachedCommittedWeaponHandles, 0, sizeof(s_cachedCommittedWeaponHandles));
        memset(s_cachedCommittedWeaponEntities, 0, sizeof(s_cachedCommittedWeaponEntities));
        memset(s_cachedCommittedWeaponIds, 0, sizeof(s_cachedCommittedWeaponIds));
        std::fill(
            std::begin(s_cachedCommittedAmmoClips),
            std::end(s_cachedCommittedAmmoClips),
            -1);
    }
    for (int i = 0; i < 64; ++i) {
        if (s_cachedCommittedWeaponPawns[i] == pawns[i])
            continue;
        s_cachedCommittedWeaponPawns[i] = pawns[i];
        s_cachedCommittedWeaponHandles[i] = 0;
        s_cachedCommittedWeaponEntities[i] = 0;
        s_cachedCommittedWeaponIds[i] = 0;
        s_cachedCommittedAmmoClips[i] = -1;
    }

    auto resolveCommittedWeaponState = [&](int idx, uint16_t liveWeaponId, uint16_t& outWeaponId, int& outAmmoClip) {
        outWeaponId = 0;
        outAmmoClip = ammoClips[idx];
        if (idx < 0 || idx >= 64)
            return;

        const uint32_t activeHandle = activeWeaponHandles[idx];
        const bool activeHandleValid = activeHandle != 0u && activeHandle != 0xFFFFFFFFu;
        const uintptr_t activeEntity = activeWeapons[idx];
        const bool sameWeaponIdentity =
            (activeHandleValid && activeHandle == s_cachedCommittedWeaponHandles[idx]) ||
            (activeEntity != 0 && activeEntity == s_cachedCommittedWeaponEntities[idx]);

        if (liveWeaponId != 0u) {
            outWeaponId = liveWeaponId;
            s_cachedCommittedWeaponHandles[idx] = activeHandleValid ? activeHandle : 0u;
            s_cachedCommittedWeaponEntities[idx] = activeEntity;
            s_cachedCommittedWeaponIds[idx] = liveWeaponId;
            s_cachedCommittedAmmoClips[idx] = outAmmoClip;
            return;
        }

        if (sameWeaponIdentity && s_cachedCommittedWeaponIds[idx] != 0u) {
            outWeaponId = s_cachedCommittedWeaponIds[idx];
            outAmmoClip = s_cachedCommittedAmmoClips[idx];
            return;
        }

        if (!activeHandleValid && activeEntity == 0) {
            s_cachedCommittedWeaponHandles[idx] = 0u;
            s_cachedCommittedWeaponEntities[idx] = 0;
            s_cachedCommittedWeaponIds[idx] = 0u;
            s_cachedCommittedAmmoClips[idx] = -1;
            return;
        }

        s_cachedCommittedWeaponHandles[idx] = activeHandleValid ? activeHandle : 0u;
        s_cachedCommittedWeaponEntities[idx] = activeEntity;
        s_cachedCommittedWeaponIds[idx] = 0u;
        s_cachedCommittedAmmoClips[idx] = -1;
    };

    static uintptr_t s_lastGoodBonePawns[64] = {};
    static uint32_t s_lastGoodBonePawnHandles[64] = {};
    static uint64_t s_lastGoodBoneUs[64] = {};
    static Vector3 s_lastGoodBones[64][esp::kPlayerStoredBoneCount] = {};
    static esp::data::BonePoseAnchor s_lastGoodBoneAnchors[64] = {};

    static uint8_t s_lastBoneRejectReason[64] = {};
    static uint64_t s_lastBoneRejectEventUs[64] = {};
    static uint8_t s_lastBoneRejectStreak[64] = {};
    static uint64_t s_boneAnchorMismatchSinceUs[64] = {};
    static uint64_t s_committedBoneCacheResetSerial = 0;
    const uint64_t committedBoneResetSerial =
        s_sceneResetSerial.load(std::memory_order_relaxed);
    if (s_committedBoneCacheResetSerial != committedBoneResetSerial) {
        s_committedBoneCacheResetSerial = committedBoneResetSerial;
        memset(s_lastGoodBonePawns, 0, sizeof(s_lastGoodBonePawns));
        memset(s_lastGoodBonePawnHandles, 0, sizeof(s_lastGoodBonePawnHandles));
        memset(s_lastGoodBoneUs, 0, sizeof(s_lastGoodBoneUs));
        memset(s_lastGoodBones, 0, sizeof(s_lastGoodBones));
        for (auto& anchor : s_lastGoodBoneAnchors)
            anchor = {};
        memset(s_lastBoneRejectReason, 0, sizeof(s_lastBoneRejectReason));
        memset(s_lastBoneRejectEventUs, 0, sizeof(s_lastBoneRejectEventUs));
        memset(s_lastBoneRejectStreak, 0, sizeof(s_lastBoneRejectStreak));
        memset(s_boneAnchorMismatchSinceUs, 0, sizeof(s_boneAnchorMismatchSinceUs));
    }

    auto evaluateStoredBones = [&](const Vector3* storedBones, int playerSlot, uint64_t sampleTimeUs) -> esp::data::BonePlausibilityResult {
        if (!storedBones || playerSlot < 0 || playerSlot >= 64)
            return {};

        return esp::data::EvaluateAnchoredBonePose(storedBones,
            bonePoseAnchors[playerSlot], pawns[playerSlot], sampleTimeUs);
    };

    auto markBonePlausibilityRecovered = [&](int playerSlot) {
        if (playerSlot >= 0 && playerSlot < 64) {
            s_lastBoneRejectReason[playerSlot] = 0;
            s_lastBoneRejectStreak[playerSlot] = 0;
            s_boneAnchorMismatchSinceUs[playerSlot] = 0;
        }
    };

    auto recordBoneReject = [&](int playerSlot, esp::data::BonePlausibilityRejectReason reason) {
        if (playerSlot < 0 ||
            playerSlot >= 64 ||
            reason == esp::data::BonePlausibilityRejectReason::None ||
            reason == esp::data::BonePlausibilityRejectReason::MissingInput) {
            return;
        }

        const uint8_t rejectReason = static_cast<uint8_t>(reason);
        const bool reasonChanged = s_lastBoneRejectReason[playerSlot] != rejectReason;
        if (reasonChanged) {
            s_lastBoneRejectReason[playerSlot] = rejectReason;
            s_lastBoneRejectStreak[playerSlot] = 1;
        } else if (s_lastBoneRejectStreak[playerSlot] < 255) {
            ++s_lastBoneRejectStreak[playerSlot];
        }
        const bool cooldownElapsed =
            s_lastBoneRejectEventUs[playerSlot] == 0 ||
            nowUs <= s_lastBoneRejectEventUs[playerSlot] ||
            (nowUs - s_lastBoneRejectEventUs[playerSlot]) >= 750000u;
        const bool shouldRecordReject =
            esp::data::ShouldReportBoneReject(
                reason,
                s_lastBoneRejectStreak[playerSlot],
                reasonChanged,
                cooldownElapsed);
        if (shouldRecordReject) {
            s_lastBoneRejectEventUs[playerSlot] = nowUs;
            RecordEspEvent({
                .type = EspEventType::BoneRejected,
                .slot = static_cast<uint8_t>(playerSlot),
                .param = static_cast<uint16_t>(rejectReason)
            });
        }
    };

    auto rememberGoodCommittedBones = [&](
        int playerSlot,
        const esp::PlayerData& player) {
        if (playerSlot < 0 || playerSlot >= 64 || !pawns[playerSlot])
            return;
        s_lastGoodBonePawns[playerSlot] = pawns[playerSlot];
        s_lastGoodBonePawnHandles[playerSlot] = pawnHandles[playerSlot];
        s_lastGoodBoneUs[playerSlot] = player.bonesUpdatedAtUs;
        s_lastGoodBoneAnchors[playerSlot] = player.boneAnchorValid
            ? esp::data::BonePoseAnchor{player.pawn, player.bonesUpdatedAtUs, player.boneAnchorPosition}
            : esp::data::BonePoseAnchor{};
        for (int boneIdx = 0; boneIdx < esp::kPlayerStoredBoneCount; ++boneIdx)
            s_lastGoodBones[playerSlot][boneIdx] = player.bones[boneIdx];
    };

    constexpr uint64_t kCommittedBoneHoldUs = 850000;
    constexpr uint64_t kLastGoodBoneHoldUs = 1800000;
    auto copyResolvedBones = [&](int playerSlot, esp::PlayerData& targetPlayer) -> bool {
        esp::data::BonePlausibilityResult currentPlausibility = {};
        if (playerSlot >= 0 && playerSlot < 64 && hasBoneData[playerSlot] &&
            esp::data::IsReusableBoneSample(boneSampleTimeUs[playerSlot], nowUs, kCommittedBoneHoldUs)) {
            Vector3 currentStoredBones[esp::kPlayerStoredBoneCount] = {};
            for (int storedIdx = 0; storedIdx < esp::kPlayerStoredBoneCount; ++storedIdx)
                currentStoredBones[storedIdx] = allBones[playerSlot][storedIdx];

            currentPlausibility = evaluateStoredBones(currentStoredBones, playerSlot, boneSampleTimeUs[playerSlot]);
            if (currentPlausibility.plausible) {
                markBonePlausibilityRecovered(playerSlot);
                targetPlayer.hasBones = true;
                targetPlayer.bonesUpdatedAtUs =
                    boneSampleTimeUs[playerSlot];
                targetPlayer.boneAnchorValid = esp::data::HasMatchingBonePoseAnchor(
                    bonePoseAnchors[playerSlot], pawns[playerSlot], targetPlayer.bonesUpdatedAtUs);
                targetPlayer.boneAnchorPosition = targetPlayer.boneAnchorValid
                    ? bonePoseAnchors[playerSlot].position : Vector3{};
                for (int boneIdx = 0; boneIdx < esp::kPlayerStoredBoneCount; ++boneIdx)
                    targetPlayer.bones[boneIdx] = currentStoredBones[boneIdx];
                targetPlayer.hasHitboxes = hasHitboxData[playerSlot];
                targetPlayer.hitboxCount = hasHitboxData[playerSlot]
                    ? std::min<uint8_t>(
                        hitboxCounts[playerSlot],
                        esp::kMaximumPlayerHitboxes)
                    : 0;
                targetPlayer.hitboxesUpdatedAtUs = targetPlayer.hasHitboxes
                    ? hitboxSampleTimeUs[playerSlot]
                    : 0;
                if (targetPlayer.hasHitboxes) {
                    memcpy(
                        targetPlayer.hitboxes,
                        allHitboxes[playerSlot],
                        sizeof(targetPlayer.hitboxes));
                } else {
                    memset(targetPlayer.hitboxes, 0, sizeof(targetPlayer.hitboxes));
                }
                rememberGoodCommittedBones(
                    playerSlot,
                    targetPlayer);
                return true;
            }
            if (esp::data::ShouldClearCurrentBoneScratch(currentPlausibility.rejectReason)) {
                hasBoneData[playerSlot] = false;
                memset(allBones[playerSlot], 0, sizeof(allBones[playerSlot]));
            }
        }

        esp::data::BonePlausibilityResult previousPlausibility = {};
        if (playerSlot >= 0 && playerSlot < 64 &&
            pawns[playerSlot] != 0 &&
            s_prevCaptureTimeUs != 0 &&
            nowUs > s_prevCaptureTimeUs &&
            (nowUs - s_prevCaptureTimeUs) <= kCommittedBoneHoldUs) {
            const esp::PlayerData& prevPlayer = s_prevPlayers[playerSlot];
            if (prevPlayer.valid && prevPlayer.hasBones && prevPlayer.pawn == pawns[playerSlot] &&
                prevPlayer.pawnHandle == pawnHandles[playerSlot] &&
                esp::data::IsReusableBoneSample(prevPlayer.bonesUpdatedAtUs, nowUs, kCommittedBoneHoldUs))
                previousPlausibility = esp::data::EvaluateRetainedBonePose(prevPlayer.bones,
                    {prevPlayer.pawn, prevPlayer.boneAnchorValid ? prevPlayer.bonesUpdatedAtUs : 0,
                     prevPlayer.boneAnchorPosition}, bonePoseAnchors[playerSlot],
                    pawns[playerSlot], prevPlayer.bonesUpdatedAtUs);
        }
        if (previousPlausibility.plausible) {
            markBonePlausibilityRecovered(playerSlot);
            targetPlayer.hasBones = true;
            targetPlayer.bonesUpdatedAtUs =
                s_prevPlayers[playerSlot].bonesUpdatedAtUs;
            targetPlayer.boneAnchorPosition = s_prevPlayers[playerSlot].boneAnchorPosition;
            targetPlayer.boneAnchorValid = s_prevPlayers[playerSlot].boneAnchorValid;
            for (int boneIdx = 0; boneIdx < esp::kPlayerStoredBoneCount; ++boneIdx)
                targetPlayer.bones[boneIdx] = s_prevPlayers[playerSlot].bones[boneIdx];
            // A display hold after an explicitly rejected new pose is not
            // current ballistic evidence. A pure read gap still retains its
            // original timestamps through the normal committed-copy path.
            targetPlayer.hasHitboxes = false;
            targetPlayer.hitboxCount = 0;
            targetPlayer.hitboxesUpdatedAtUs = 0;
            memset(targetPlayer.hitboxes, 0, sizeof(targetPlayer.hitboxes));
            rememberGoodCommittedBones(
                playerSlot,
                targetPlayer);
            return true;
        }

        esp::data::BonePlausibilityResult lastGoodPlausibility = {};
        if (playerSlot >= 0 && playerSlot < 64 &&
            pawns[playerSlot] != 0 &&
            s_lastGoodBonePawns[playerSlot] == pawns[playerSlot] &&
            s_lastGoodBonePawnHandles[playerSlot] == pawnHandles[playerSlot] &&
            s_lastGoodBoneUs[playerSlot] != 0 &&
            nowUs > s_lastGoodBoneUs[playerSlot] &&
            (nowUs - s_lastGoodBoneUs[playerSlot]) <= kLastGoodBoneHoldUs) {
            lastGoodPlausibility = esp::data::EvaluateRetainedBonePose(s_lastGoodBones[playerSlot],
                s_lastGoodBoneAnchors[playerSlot], bonePoseAnchors[playerSlot],
                pawns[playerSlot], s_lastGoodBoneUs[playerSlot]);
        }
        if (lastGoodPlausibility.plausible) {
            markBonePlausibilityRecovered(playerSlot);
            targetPlayer.hasBones = true;
            targetPlayer.bonesUpdatedAtUs = s_lastGoodBoneUs[playerSlot];
            targetPlayer.boneAnchorValid = esp::data::HasMatchingBonePoseAnchor(
                s_lastGoodBoneAnchors[playerSlot], pawns[playerSlot], targetPlayer.bonesUpdatedAtUs);
            targetPlayer.boneAnchorPosition = s_lastGoodBoneAnchors[playerSlot].position;
            for (int boneIdx = 0; boneIdx < esp::kPlayerStoredBoneCount; ++boneIdx)
                targetPlayer.bones[boneIdx] = s_lastGoodBones[playerSlot][boneIdx];
            targetPlayer.hasHitboxes = false;
            targetPlayer.hitboxCount = 0;
            targetPlayer.hitboxesUpdatedAtUs = 0;
            memset(targetPlayer.hitboxes, 0, sizeof(targetPlayer.hitboxes));
            return true;
        }

        const bool anchorMismatch =
            currentPlausibility.rejectReason ==
                esp::data::BonePlausibilityRejectReason::TooFarFromAnchor ||
            previousPlausibility.rejectReason ==
                esp::data::BonePlausibilityRejectReason::TooFarFromAnchor ||
            lastGoodPlausibility.rejectReason ==
                esp::data::BonePlausibilityRejectReason::TooFarFromAnchor;
        // A current scene anchor confirming a teleport overrides continuity.
        // The short legacy hold is only for an unavailable scene anchor.
        const bool hasCurrentAnchor = playerSlot >= 0 && playerSlot < 64 &&
            esp::data::HasMatchingBonePoseAnchor(bonePoseAnchors[playerSlot],
                pawns[playerSlot], bonePoseAnchors[playerSlot].sampleTimeUs);
        if (anchorMismatch && !hasCurrentAnchor && playerSlot >= 0 && playerSlot < 64 &&
            s_lastGoodBonePawns[playerSlot] == pawns[playerSlot] &&
            s_lastGoodBonePawnHandles[playerSlot] == pawnHandles[playerSlot] &&
            s_lastGoodBoneUs[playerSlot] != 0 &&
            nowUs >= s_lastGoodBoneUs[playerSlot]) {
            if (s_boneAnchorMismatchSinceUs[playerSlot] == 0 ||
                nowUs < s_boneAnchorMismatchSinceUs[playerSlot]) {
                s_boneAnchorMismatchSinceUs[playerSlot] = nowUs;
            }
            const auto structuralPlausibility =
                esp::data::EvaluateStoredBonePlausibility({
                    s_lastGoodBones[playerSlot],
                    {},
                    false,
                });
            if (esp::data::ShouldHoldBoneAnchorMismatch(
                    true,
                    structuralPlausibility.plausible,
                    s_boneAnchorMismatchSinceUs[playerSlot],
                    nowUs)) {
                targetPlayer.hasBones = true;
                targetPlayer.bonesUpdatedAtUs = s_lastGoodBoneUs[playerSlot];
                targetPlayer.boneAnchorValid = esp::data::HasMatchingBonePoseAnchor(
                    s_lastGoodBoneAnchors[playerSlot], pawns[playerSlot], targetPlayer.bonesUpdatedAtUs);
                targetPlayer.boneAnchorPosition = s_lastGoodBoneAnchors[playerSlot].position;
                for (int boneIdx = 0;
                     boneIdx < esp::kPlayerStoredBoneCount;
                     ++boneIdx) {
                    targetPlayer.bones[boneIdx] =
                        s_lastGoodBones[playerSlot][boneIdx];
                }
                targetPlayer.hasHitboxes = false;
                targetPlayer.hitboxCount = 0;
                targetPlayer.hitboxesUpdatedAtUs = 0;
                memset(targetPlayer.hitboxes, 0, sizeof(targetPlayer.hitboxes));
                return true;
            }
        } else if (playerSlot >= 0 && playerSlot < 64) {
            s_boneAnchorMismatchSinceUs[playerSlot] = 0;
        }

        targetPlayer.hasBones = false;
        targetPlayer.boneAnchorValid = false;
        targetPlayer.bonesUpdatedAtUs = 0;
        targetPlayer.hasHitboxes = false;
        targetPlayer.hitboxCount = 0;
        targetPlayer.hitboxesUpdatedAtUs = 0;
        memset(targetPlayer.hitboxes, 0, sizeof(targetPlayer.hitboxes));
        const auto reportedReason = esp::data::SelectReportedBoneRejectReason(
            currentPlausibility.rejectReason,
            previousPlausibility.rejectReason,
            lastGoodPlausibility.rejectReason);
        recordBoneReject(playerSlot, reportedReason);
        if (playerSlot >= 0 && playerSlot < 64 &&
            reportedReason == esp::data::BonePlausibilityRejectReason::TooFarFromAnchor) {
            s_lastGoodBonePawns[playerSlot] = 0;
            s_lastGoodBoneUs[playerSlot] = 0;
            memset(s_lastGoodBones[playerSlot], 0, sizeof(s_lastGoodBones[playerSlot]));
        }
        return false;
    };

    auto collectGrenadesForSlot = [&](int idx, auto& grenadeIdsOut, int& grenadeCountOut) {
        uint16_t previousGrenadeIds[esp::PlayerData::kMaxGrenades] = {};
        const int previousGrenadeCount = std::clamp(grenadeCountOut, 0, esp::PlayerData::kMaxGrenades);
        for (int g = 0; g < previousGrenadeCount; ++g)
            previousGrenadeIds[g] = grenadeIdsOut[g];

        uint16_t resolvedGrenadeIds[esp::PlayerData::kMaxGrenades] = {};
        int resolvedGrenadeCount = 0;
        if (idx < 0 || idx >= 64)
            return;

        auto appendGrenade = [&](uint16_t weaponId) {
            if (weaponId < 43u || weaponId > 48u)
                return;
            if (resolvedGrenadeCount >= esp::PlayerData::kMaxGrenades)
                return;
            resolvedGrenadeIds[resolvedGrenadeCount++] = weaponId;
        };

        const int inventorySlotCount = getInventorySlotCount(idx);
        bool inventoryHadEvidence = inventorySlotCount > 0 && inventoryWeaponHandleArrays[idx] != 0;
        for (int slot = 0; slot < inventorySlotCount && resolvedGrenadeCount < esp::PlayerData::kMaxGrenades; ++slot) {
            if (inventoryWeaponHandles[idx][slot] ||
                inventoryWeapons[idx][slot] ||
                inventoryWeaponIds[idx][slot]) {
                inventoryHadEvidence = true;
            }
            appendGrenade(inventoryWeaponIds[idx][slot]);
        }

        const uint16_t activeWeaponId = (weaponIds[idx] < 20000u) ? weaponIds[idx] : 0u;
        if (activeWeaponId >= 43u && activeWeaponId <= 48u) {
            const uint32_t activeHandle = activeWeaponHandles[idx];
            const uintptr_t activeEntity = activeWeapons[idx];
            bool activeAlreadyRepresented = false;
            for (int slot = 0; slot < inventorySlotCount; ++slot) {
                if (activeHandle && activeHandle != 0xFFFFFFFFu &&
                    inventoryWeaponHandles[idx][slot] == activeHandle) {
                    activeAlreadyRepresented = true;
                    break;
                }
                if (activeEntity && inventoryWeapons[idx][slot] == activeEntity) {
                    activeAlreadyRepresented = true;
                    break;
                }
                if (inventoryWeaponIds[idx][slot] == activeWeaponId) {
                    activeAlreadyRepresented = true;
                    break;
                }
            }
            if (!activeAlreadyRepresented)
                appendGrenade(activeWeaponId);
        }

        if (!inventoryHadEvidence && previousGrenadeCount > 0) {
            grenadeCountOut = previousGrenadeCount;
            memset(grenadeIdsOut, 0, sizeof(grenadeIdsOut));
            for (int g = 0; g < previousGrenadeCount; ++g)
                grenadeIdsOut[g] = previousGrenadeIds[g];
            return;
        }

        grenadeCountOut = resolvedGrenadeCount;
        memset(grenadeIdsOut, 0, sizeof(grenadeIdsOut));
        for (int g = 0; g < resolvedGrenadeCount; ++g)
            grenadeIdsOut[g] = resolvedGrenadeIds[g];
    };

    bool localHasBombResolved = false;
    int localAmmoClipResolved = s_localAmmoClip;
    bool localAmmoValidResolved = s_localAmmoValid;
    uint64_t localAmmoUpdatedAtUsResolved = s_localAmmoUpdatedAtUs;
    uint32_t localWeaponHandleResolved = s_localWeaponHandle;
    uintptr_t localWeaponEntityResolved = s_localWeaponEntity;
    uint64_t localWeaponUpdatedAtUsResolved = s_localWeaponUpdatedAtUs;
    SharedLocalIdentity localIdentity{
        .isDead = s_localIsDead,
        .health = s_localHealth,
        .armor = s_localArmor,
        .money = s_localMoney,
        .hasDefuser = s_localHasDefuser
    };
    memcpy(localIdentity.name, s_localName, sizeof(localIdentity.name));
    uint16_t localGrenadeIdsResolved[esp::PlayerData::kMaxGrenades] = {};
    std::copy(std::begin(s_localGrenadeIds), std::end(s_localGrenadeIds), std::begin(localGrenadeIdsResolved));
    int localGrenadeCountResolved = std::clamp(s_localGrenadeCount, 0, esp::PlayerData::kMaxGrenades);
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
    resolveSharedLocalIdentity(
        localPlayerIndexValid && localPlayerIndexHasLiveEvidence ? localPlayerIndex : -1,
        localIdentity);
    int localLoadoutIndex = -1;
    if (localPlayerIndexValid && localPlayerIndexHasLiveEvidence) {
        localLoadoutIndex = localPlayerIndex;
    } else if (localPlayerIndexValid && s_localPawn != 0 && pawns[localPlayerIndex] == s_localPawn) {
        localLoadoutIndex = localPlayerIndex;
    } else if (localControllerMaskBit > 0 && localControllerMaskBit <= 64) {
        localLoadoutIndex = static_cast<int>(localControllerMaskBit - 1);
    }
    if (localLoadoutIndex < 0 && s_localPawn != 0) {
        for (int i = 0; i < 64; ++i) {
            if (pawns[i] == s_localPawn) {
                localLoadoutIndex = i;
                break;
            }
        }
    }
    if (localLoadoutIndex < 0 &&
        localControllerPawnHandle != 0u &&
        localControllerPawnHandle != 0xFFFFFFFFu) {
        for (int i = 0; i < 64; ++i) {
            if (pawnHandles[i] != 0u &&
                pawnHandles[i] != 0xFFFFFFFFu &&
                pawnHandles[i] == localControllerPawnHandle) {
                localLoadoutIndex = i;
                break;
            }
        }
    }
    if (localLoadoutIndex >= 0 && localLoadoutIndex < 64) {
        const uint16_t liveLocalWeaponId =
            (weaponIds[localLoadoutIndex] < 20000u) ? weaponIds[localLoadoutIndex] : 0;
        uint16_t committedLocalWeaponId = 0;
        int committedLocalAmmoClip = ammoClips[localLoadoutIndex];
        resolveCommittedWeaponState(
            localLoadoutIndex,
            liveLocalWeaponId,
            committedLocalWeaponId,
            committedLocalAmmoClip);
        localWeaponIdResolved = committedLocalWeaponId;
        localAmmoClipResolved = committedLocalAmmoClip;
        if (localWeaponIdResolved != s_localWeaponId) {
            localAmmoValidResolved = false;
            localAmmoUpdatedAtUsResolved = 0;
        }
        if (activeWeaponLaneDue &&
            activeWeaponHandleReadFresh[localLoadoutIndex]) {
            const bool localWeaponIdentityChanged =
                activeWeaponHandles[localLoadoutIndex] !=
                    s_localWeaponHandle ||
                activeWeapons[localLoadoutIndex] !=
                    s_localWeaponEntity;
            localWeaponHandleResolved = activeWeaponHandles[localLoadoutIndex];
            localWeaponEntityResolved = activeWeapons[localLoadoutIndex];
            localWeaponUpdatedAtUsResolved = inventoryNowUs;
            if (localWeaponIdentityChanged) {
                localAmmoClipResolved = -1;
                localAmmoValidResolved = false;
                localAmmoUpdatedAtUsResolved = 0;
            }
            if (activeAmmoClipReadFresh[localLoadoutIndex]) {
                localAmmoClipResolved = ammoClips[localLoadoutIndex];
                localAmmoValidResolved = true;
                localAmmoUpdatedAtUsResolved = inventoryNowUs;
            }
        }
        localHasBombResolved = (localLoadoutIndex == resolvedBombCarrierSlot);
        collectGrenadesForSlot(localLoadoutIndex, localGrenadeIdsResolved, localGrenadeCountResolved);
    } else if (localControllerMaskBit > 0 && localControllerMaskBit <= 64) {
        localHasBombResolved = ((localControllerMaskBit - 1) == resolvedBombCarrierSlot);
    }

    ApplySharedLocalIdentityState(localIdentity);
    s_localWeaponId = localWeaponIdResolved;
    s_localWeaponHandle = localWeaponHandleResolved;
    s_localWeaponEntity = localWeaponEntityResolved;
    s_localAmmoClip = localAmmoClipResolved;
    s_localAmmoValid = localAmmoValidResolved;
    s_localAmmoUpdatedAtUs = localAmmoUpdatedAtUsResolved;
    s_localWeaponUpdatedAtUs = localWeaponUpdatedAtUsResolved;
    {
        static uintptr_t s_telemetryWeapon = 0;
        static uint64_t s_lastTelemetryReadUs = 0;
        if (!wantsTargetWeaponState || !localWeaponEntityResolved ||
            localWeaponEntityResolved != s_telemetryWeapon) {
            if (localWeaponEntityResolved != s_telemetryWeapon) {
                s_telemetryWeapon = localWeaponEntityResolved;
                s_lastTelemetryReadUs = 0;
                s_localWeaponTelemetry = {};
            }
        }
        const bool telemetryDue = wantsTargetWeaponState &&
            localWeaponEntityResolved != 0 &&
            (s_lastTelemetryReadUs == 0 || nowUs < s_lastTelemetryReadUs ||
             nowUs - s_lastTelemetryReadUs >= 12000u);
        if (telemetryDue) {
            s_lastTelemetryReadUs = nowUs;
            esp::WeaponTelemetry telemetry = {};
            uintptr_t weaponVData = 0;
            const bool offsetsReady =
                ofs.C_BaseEntity_m_nSubclassID > 0 &&
                ofs.C_BaseEntity_m_fFlags > 0 &&
                ofs.C_BaseEntity_m_vecVelocity > 0 &&
                ofs.C_CSPlayerPawn_m_bIsWalking > 0 &&
                ofs.C_CSWeaponBase_m_weaponMode > 0 &&
                ofs.C_CSWeaponBase_m_flTurningInaccuracy > 0 &&
                ofs.C_CSWeaponBase_m_fAccuracyPenalty > 0 &&
                ofs.C_CSWeaponBase_m_flRecoilIndex > 0 &&
                ofs.C_CSWeaponBase_m_bInReload > 0 &&
                ofs.C_CSWeaponBase_m_fLastShotTime > 0 &&
                ofs.CCSWeaponBaseVData_m_WeaponType > 0 &&
                ofs.CCSWeaponBaseVData_m_nNumBullets > 0 &&
                ofs.CCSWeaponBaseVData_m_flSpread > 0 &&
                ofs.CCSWeaponBaseVData_m_flMaxSpeed > 0 &&
                ofs.CCSWeaponBaseVData_m_flInaccuracyMove > 0 &&
                ofs.CCSWeaponBaseVData_m_flInaccuracyJumpInitial > 0 &&
                ofs.CCSWeaponBaseVData_m_flInaccuracyJumpApex > 0 &&
                ofs.CCSWeaponBaseVData_m_flInaccuracyStand > 0 &&
                ofs.CCSWeaponBaseVData_m_flInaccuracyCrouch > 0 &&
                ofs.CCSWeaponBaseVData_m_flCycleTime > 0 &&
                ofs.CCSWeaponBaseVData_m_bIsFullAuto > 0 &&
                ofs.CCSWeaponBaseVData_m_nDamage > 0 &&
                ofs.CCSWeaponBaseVData_m_flPenetration > 0 &&
                ofs.CCSWeaponBaseVData_m_flRangeModifier > 0 &&
                ofs.CCSWeaponBaseVData_m_flRange > 0 &&
                ofs.CCSWeaponBaseVData_m_flArmorRatio > 0 &&
                ofs.CCSWeaponBaseVData_m_flHeadshotMultiplier > 0;
            if (offsetsReady) {
                const uintptr_t subclassVDataAddress =
                    localWeaponEntityResolved +
                    static_cast<uintptr_t>(ofs.C_BaseEntity_m_nSubclassID) +
                    0x8u;
                if (!readValue(
                        subclassVDataAddress,
                        &weaponVData,
                        sizeof(weaponVData)) ||
                    !isLikelyGamePointer(weaponVData)) {
                    weaponVData = 0;
                }
            }

            constexpr size_t kWeaponBlockBytes = 0x180u;
            constexpr size_t kVDataBlockBytes = 0x340u;
            std::array<std::byte, kWeaponBlockBytes> weaponBlock = {};
            std::array<std::byte, kVDataBlockBytes> vdataBlock = {};
            const uintptr_t weaponBaseOffset = static_cast<uintptr_t>(
                std::min({
                    ofs.C_CSWeaponBase_m_weaponMode,
                    ofs.C_CSWeaponBase_m_flTurningInaccuracy,
                    ofs.C_CSWeaponBase_m_fAccuracyPenalty,
                    ofs.C_CSWeaponBase_m_flRecoilIndex,
                    ofs.C_CSWeaponBase_m_bInReload,
                    ofs.C_CSWeaponBase_m_fLastShotTime,
                }));
            const uintptr_t vdataBaseOffset = static_cast<uintptr_t>(
                std::min({
                    ofs.CCSWeaponBaseVData_m_WeaponType,
                    ofs.CCSWeaponBaseVData_m_nNumBullets,
                    ofs.CCSWeaponBaseVData_m_flSpread,
                    ofs.CCSWeaponBaseVData_m_flMaxSpeed,
                    ofs.CCSWeaponBaseVData_m_flInaccuracyMove,
                    ofs.CCSWeaponBaseVData_m_flInaccuracyJumpInitial,
                    ofs.CCSWeaponBaseVData_m_flInaccuracyJumpApex,
                    ofs.CCSWeaponBaseVData_m_flInaccuracyStand,
                    ofs.CCSWeaponBaseVData_m_flInaccuracyCrouch,
                    ofs.CCSWeaponBaseVData_m_flCycleTime,
                    ofs.CCSWeaponBaseVData_m_bIsFullAuto,
                    ofs.CCSWeaponBaseVData_m_nDamage,
                    ofs.CCSWeaponBaseVData_m_flPenetration,
                    ofs.CCSWeaponBaseVData_m_flRangeModifier,
                    ofs.CCSWeaponBaseVData_m_flRange,
                    ofs.CCSWeaponBaseVData_m_flArmorRatio,
                    ofs.CCSWeaponBaseVData_m_flHeadshotMultiplier,
                }));
            const bool weaponBlockValid = weaponVData != 0 &&
                readValue(
                    localWeaponEntityResolved + weaponBaseOffset,
                    weaponBlock.data(),
                    weaponBlock.size());
            const bool vdataBlockValid = weaponVData != 0 &&
                readValue(
                    weaponVData + vdataBaseOffset,
                    vdataBlock.data(),
                    vdataBlock.size());
            auto readBlock = [](
                const auto& block,
                uintptr_t blockBase,
                std::ptrdiff_t fieldOffset,
                auto& value) -> bool {
                if (fieldOffset < 0 ||
                    static_cast<uintptr_t>(fieldOffset) < blockBase) {
                    return false;
                }
                const size_t offset = static_cast<size_t>(fieldOffset) -
                    static_cast<size_t>(blockBase);
                if (offset + sizeof(value) > block.size())
                    return false;
                memcpy(&value, block.data() + offset, sizeof(value));
                return true;
            };

            int mode = 0;
            float turningInaccuracy = 0.0f;
            float accuracyPenalty = 0.0f;
            float recoilIndex = 0.0f;
            uint8_t reloading = 0;
            float lastShotTime = 0.0f;
            int weaponType = 0;
            int bullets = 0;
            float spread = 0.0f;
            struct FloatPair { float first; float second; };
            FloatPair maximumSpeed = {};
            FloatPair moveInaccuracy = {};
            FloatPair standingInaccuracy = {};
            FloatPair crouchingInaccuracy = {};
            float jumpInitial = 0.0f;
            float jumpApex = 0.0f;
            float cycleTime = 0.0f;
            uint8_t fullAuto = 0;
            int damage = 0;
            float penetration = 0.0f;
            float rangeModifier = 0.0f;
            float range = 0.0f;
            float armorRatio = 0.0f;
            float headshotMultiplier = 0.0f;
            const bool blocksDecoded = weaponBlockValid && vdataBlockValid &&
                readBlock(weaponBlock, weaponBaseOffset, ofs.C_CSWeaponBase_m_weaponMode, mode) &&
                readBlock(weaponBlock, weaponBaseOffset, ofs.C_CSWeaponBase_m_flTurningInaccuracy, turningInaccuracy) &&
                readBlock(weaponBlock, weaponBaseOffset, ofs.C_CSWeaponBase_m_fAccuracyPenalty, accuracyPenalty) &&
                readBlock(weaponBlock, weaponBaseOffset, ofs.C_CSWeaponBase_m_flRecoilIndex, recoilIndex) &&
                readBlock(weaponBlock, weaponBaseOffset, ofs.C_CSWeaponBase_m_bInReload, reloading) &&
                readBlock(weaponBlock, weaponBaseOffset, ofs.C_CSWeaponBase_m_fLastShotTime, lastShotTime) &&
                readBlock(vdataBlock, vdataBaseOffset, ofs.CCSWeaponBaseVData_m_WeaponType, weaponType) &&
                readBlock(vdataBlock, vdataBaseOffset, ofs.CCSWeaponBaseVData_m_nNumBullets, bullets) &&
                readBlock(vdataBlock, vdataBaseOffset, ofs.CCSWeaponBaseVData_m_flSpread, spread) &&
                readBlock(vdataBlock, vdataBaseOffset, ofs.CCSWeaponBaseVData_m_flMaxSpeed, maximumSpeed) &&
                readBlock(vdataBlock, vdataBaseOffset, ofs.CCSWeaponBaseVData_m_flInaccuracyMove, moveInaccuracy) &&
                readBlock(vdataBlock, vdataBaseOffset, ofs.CCSWeaponBaseVData_m_flInaccuracyJumpInitial, jumpInitial) &&
                readBlock(vdataBlock, vdataBaseOffset, ofs.CCSWeaponBaseVData_m_flInaccuracyJumpApex, jumpApex) &&
                readBlock(vdataBlock, vdataBaseOffset, ofs.CCSWeaponBaseVData_m_flInaccuracyStand, standingInaccuracy) &&
                readBlock(vdataBlock, vdataBaseOffset, ofs.CCSWeaponBaseVData_m_flInaccuracyCrouch, crouchingInaccuracy) &&
                readBlock(vdataBlock, vdataBaseOffset, ofs.CCSWeaponBaseVData_m_flCycleTime, cycleTime) &&
                readBlock(vdataBlock, vdataBaseOffset, ofs.CCSWeaponBaseVData_m_bIsFullAuto, fullAuto) &&
                readBlock(vdataBlock, vdataBaseOffset, ofs.CCSWeaponBaseVData_m_nDamage, damage) &&
                readBlock(vdataBlock, vdataBaseOffset, ofs.CCSWeaponBaseVData_m_flPenetration, penetration) &&
                readBlock(vdataBlock, vdataBaseOffset, ofs.CCSWeaponBaseVData_m_flRangeModifier, rangeModifier) &&
                readBlock(vdataBlock, vdataBaseOffset, ofs.CCSWeaponBaseVData_m_flRange, range) &&
                readBlock(vdataBlock, vdataBaseOffset, ofs.CCSWeaponBaseVData_m_flArmorRatio, armorRatio) &&
                readBlock(vdataBlock, vdataBaseOffset, ofs.CCSWeaponBaseVData_m_flHeadshotMultiplier, headshotMultiplier);

            uint32_t flags = 0;
            Vector3 localVelocity = {};
            uint8_t walking = 0;
            const bool playerStateValid = localPawn != 0 &&
                readValue(
                    localPawn + ofs.C_BaseEntity_m_fFlags,
                    &flags,
                    sizeof(flags)) &&
                readValue(
                    localPawn + ofs.C_BaseEntity_m_vecVelocity,
                    &localVelocity,
                    sizeof(localVelocity)) &&
                readValue(
                    localPawn + ofs.C_CSPlayerPawn_m_bIsWalking,
                    &walking,
                    sizeof(walking)) &&
                IsFiniteVec(localVelocity) && walking <= 1u;
            const bool onGround = (flags & 1u) != 0u;
            const bool crouching = (flags & (1u << 1u)) != 0u;
            const auto selectMode = [mode](const FloatPair& pair) {
                return mode != 0 ? pair.second : pair.first;
            };
            const float selectedMaximumSpeed = selectMode(maximumSpeed);
            const float selectedMoveInaccuracy = selectMode(moveInaccuracy);
            const float selectedBaseInaccuracy = selectMode(
                crouching ? crouchingInaccuracy : standingInaccuracy);
            const float planarSpeed = std::hypot(localVelocity.x, localVelocity.y);
            float moveFactor = 0.0f;
            const float moveLow = selectedMaximumSpeed * 0.34f;
            const float moveHigh = selectedMaximumSpeed * 0.95f;
            if (moveHigh > moveLow) {
                moveFactor = std::clamp(
                    (planarSpeed - moveLow) / (moveHigh - moveLow),
                    0.0f,
                    1.0f);
            } else if (planarSpeed >= moveHigh) {
                moveFactor = 1.0f;
            }
            if (moveFactor > 0.0f && walking == 0u)
                moveFactor = std::pow(moveFactor, 0.25f);
            const float movementPenalty = moveFactor * selectedMoveInaccuracy;
            float airPenalty = 0.0f;
            if (!onGround) {
                constexpr float kDefaultJumpImpulse = 301.993377f;
                const float high = std::sqrt(kDefaultJumpImpulse);
                const float low = high * 0.25f;
                const float vertical = std::sqrt(std::fabs(localVelocity.z));
                const float fraction = high > low
                    ? (vertical - low) / (high - low)
                    : 1.0f;
                airPenalty = jumpApex +
                    fraction * (jumpInitial - jumpApex);
                airPenalty = std::clamp(
                    airPenalty,
                    0.0f,
                    std::max(0.0f, jumpInitial * 2.0f));
            }
            const float inaccuracy = std::min(
                1.0f,
                turningInaccuracy + accuracyPenalty +
                    movementPenalty + airPenalty);
            const bool valuesPlausible = blocksDecoded && playerStateValid &&
                currentGameTimeFresh && std::isfinite(currentGameTime) &&
                currentGameTime >= 0.0f && currentGameTime < 1000000.0f &&
                std::isfinite(intervalPerTick) && intervalPerTick >= 0.001f &&
                intervalPerTick <= 0.1f &&
                mode >= 0 && mode <= 4 && weaponType >= 0 && weaponType <= 20 &&
                bullets >= 1 && bullets <= 64 && reloading <= 1u &&
                fullAuto <= 1u && std::isfinite(cycleTime) &&
                cycleTime > 0.001f && cycleTime < 10.0f &&
                std::isfinite(lastShotTime) && lastShotTime >= 0.0f &&
                std::isfinite(inaccuracy) && inaccuracy >= 0.0f &&
                inaccuracy <= 1.0f && std::isfinite(spread) && spread >= 0.0f &&
                spread <= 1.0f && std::isfinite(recoilIndex) &&
                recoilIndex >= 0.0f && recoilIndex < 100.0f &&
                damage > 0 && damage < 1000 && std::isfinite(penetration) &&
                penetration >= 0.0f && penetration < 20.0f &&
                std::isfinite(range) && range > 1.0f && range < 100000.0f &&
                std::isfinite(rangeModifier) && rangeModifier > 0.0f &&
                rangeModifier <= 1.0f && std::isfinite(armorRatio) &&
                armorRatio > 0.0f && armorRatio < 5.0f &&
                std::isfinite(headshotMultiplier) &&
                headshotMultiplier >= 1.0f && headshotMultiplier < 10.0f;
            if (valuesPlausible) {
                telemetry.vdata = weaponVData;
                telemetry.valid = true;
                telemetry.isReloading = reloading != 0u;
                telemetry.ready = !telemetry.isReloading &&
                    localAmmoValidResolved && localAmmoClipResolved > 0 &&
                    currentGameTime + 0.0001f >= lastShotTime + cycleTime;
                telemetry.isScoped = localLoadoutIndex >= 0 &&
                    scopedFlags[localLoadoutIndex] == 1u;
                telemetry.onGround = onGround;
                telemetry.isWalking = walking != 0u;
                telemetry.fullAuto = fullAuto != 0u;
                telemetry.mode = mode;
                telemetry.weaponType = weaponType;
                telemetry.bullets = bullets;
                telemetry.currentTime = currentGameTime;
                telemetry.intervalPerTick = intervalPerTick;
                telemetry.renderTick = static_cast<int>(std::clamp(
                    std::llround(currentGameTime / intervalPerTick),
                    0ll,
                    static_cast<long long>(INT_MAX)));
                telemetry.cycleTime = cycleTime;
                telemetry.lastShotTime = lastShotTime;
                telemetry.inaccuracy = inaccuracy;
                telemetry.inaccuracyWithoutAir = std::min(
                    1.0f,
                    turningInaccuracy + accuracyPenalty + movementPenalty);
                telemetry.jumpInaccuracyInitial = jumpInitial;
                telemetry.jumpInaccuracyApex = jumpApex;
                telemetry.localVerticalVelocity = localVelocity.z;
                telemetry.spread = spread;
                telemetry.baseInaccuracy = selectedBaseInaccuracy;
                telemetry.recoilIndex = recoilIndex;
                telemetry.damage = static_cast<float>(damage);
                telemetry.penetration = penetration;
                telemetry.range = range;
                telemetry.rangeModifier = rangeModifier;
                telemetry.armorRatio = armorRatio;
                telemetry.headshotMultiplier = headshotMultiplier;
                telemetry.updatedAtUs = nowUs;
                s_localWeaponTelemetry = telemetry;
            } else {
                s_localWeaponTelemetry = {};
            }
        }
    }
    s_localHasBomb = localHasBombResolved;
    s_localGrenadeCount = localGrenadeCountResolved;
    std::copy(std::begin(localGrenadeIdsResolved), std::end(localGrenadeIdsResolved), std::begin(s_localGrenadeIds));

    static uintptr_t s_helmetPawns[64] = {};
    static uint8_t s_helmetFlags[64] = {};
    static uint64_t s_helmetUpdatedAtUs[64] = {};
    static uint64_t s_helmetCacheResetSerial = 0;
    const uint64_t helmetResetSerial =
        s_sceneResetSerial.load(std::memory_order_relaxed);
    if (s_helmetCacheResetSerial != helmetResetSerial) {
        s_helmetCacheResetSerial = helmetResetSerial;
        memset(s_helmetPawns, 0, sizeof(s_helmetPawns));
        memset(s_helmetFlags, 0, sizeof(s_helmetFlags));
        memset(s_helmetUpdatedAtUs, 0, sizeof(s_helmetUpdatedAtUs));
    }
    for (int i = 0; i < 64; ++i) {
        if (s_helmetPawns[i] != pawns[i]) {
            s_helmetPawns[i] = pawns[i];
            s_helmetFlags[i] = 0;
            s_helmetUpdatedAtUs[i] = 0;
        }
        if (!pawns[i] || !itemServices[i] ||
            ofs.CCSPlayer_ItemServices_m_bHasHelmet <= 0) {
            continue;
        }
        if (s_helmetUpdatedAtUs[i] != 0 && nowUs >= s_helmetUpdatedAtUs[i] &&
            nowUs - s_helmetUpdatedAtUs[i] < 100000u) {
            continue;
        }
        uint8_t helmet = 0;
        if (readValue(
                itemServices[i] + ofs.CCSPlayer_ItemServices_m_bHasHelmet,
                &helmet,
                sizeof(helmet)) && helmet <= 1u) {
            s_helmetFlags[i] = helmet;
            s_helmetUpdatedAtUs[i] = nowUs;
        }
    }

#include "commit_players_enrichment.inl"
    for (int i = 0; i < 64; ++i) {
        if (s_players[i].valid &&
            s_players[i].health > 0 &&
            s_players[i].pawn == s_helmetPawns[i]) {
            s_players[i].hasHelmet = s_helmetFlags[i] == 1u;
            s_players[i].hasHelmetValid = s_helmetUpdatedAtUs[i] != 0 &&
                nowUs >= s_helmetUpdatedAtUs[i] &&
                nowUs - s_helmetUpdatedAtUs[i] <= 150000u;
            s_players[i].helmetUpdatedAtUs = s_players[i].hasHelmetValid
                ? s_helmetUpdatedAtUs[i]
                : 0;
        }
    }
#include "commit_world.inl"
#include "commit_bomb.inl"

    
    {
        int activeCount = 0;
        int resolvedLiveCount = 0;
        int committedLiveCount = 0;
        auto& activeCountedSlots = s_playerReadScratch.activeCountedSlots;
        uintptr_t activeCountedPawns[64] = {};
        int activeCountedPawnCount = 0;
        const int populationEngineMaxClients =
            std::clamp(s_engineMaxClients.load(std::memory_order_relaxed), 0, 256);
        const int populationSlotBudget =
            std::clamp(s_playerSlotScanLimitStat.load(std::memory_order_relaxed), 0, 64);
        const int populationSignOnState =
            s_engineSignOnState.load(std::memory_order_relaxed);
        const bool populationEngineInGame =
            s_engineInGame.load(std::memory_order_relaxed);
        const bool populationEngineMenu =
            s_engineMenu.load(std::memory_order_relaxed);
        const bool populationLiveByEngine =
            populationEngineInGame &&
            !populationEngineMenu &&
            (populationSignOnState == 6 ||
             (populationEngineMaxClients >= 2 && populationSlotBudget >= 32));
        const bool populationLiveByShape =
            !populationEngineMenu &&
            populationSlotBudget >= 32 &&
            highestEntityIndex >= 64 &&
            g::clientBase &&
            g::engine2Base;
        const bool populationMapKnown =
            !liveMapKey.empty();
        const bool populationLiveByMap =
            !populationEngineMenu &&
            populationMapKnown &&
            populationSlotBudget >= 32 &&
            highestEntityIndex >= 64 &&
            g::clientBase &&
            g::engine2Base;
        const bool populationLiveContext =
            populationLiveByEngine || populationLiveByShape || populationLiveByMap;
        const bool localControllerPawnHandleValidForCount =
            localControllerPawnHandle != 0u &&
            localControllerPawnHandle != 0xFFFFFFFFu;
        const bool localAliveEvidence =
            populationLiveContext &&
            (localPlayerIndexHasLiveEvidence ||
             localPawnCoreLiveResolved) &&
            !localIdentity.isDead &&
            localIdentity.health > 0;
        auto isFreshLiveCoreSlot = [&](int i) -> bool {
            if (i < 0 || i >= 64 || !pawns[i])
                return false;
            if (!coreReadFresh[i] || !coreReadAlive[i])
                return false;
            if (teams[i] != 2 && teams[i] != 3)
                return false;
            return isValidWorldPos(positions[i]);
        };
        auto isLocalActiveSlot = [&](int i) -> bool {
            if (i < 0 || i >= 64)
                return false;
            if (s_localPawn != 0 && pawns[i] == s_localPawn)
                return true;
            if (localPlayerIndexValid && localPlayerIndexHasLiveEvidence && i == localPlayerIndex)
                return true;
            if (localControllerMaskBit > 0 && localControllerMaskBit <= 64 && i == (localControllerMaskBit - 1))
                return true;
            return localControllerPawnHandleValidForCount &&
                   pawnHandles[i] != 0u &&
                   pawnHandles[i] != 0xFFFFFFFFu &&
                   pawnHandles[i] == localControllerPawnHandle;
        };
        auto isActivePawnCounted = [&](uintptr_t pawn) -> bool {
            if (pawn == 0)
                return false;
            for (int idx = 0; idx < activeCountedPawnCount; ++idx) {
                if (activeCountedPawns[idx] == pawn)
                    return true;
            }
            return false;
        };
        auto markActiveSlotCounted = [&](int i, uintptr_t pawn) {
            activeCountedSlots[i] = true;
            if (pawn != 0 &&
                !isActivePawnCounted(pawn) &&
                activeCountedPawnCount < 64) {
                activeCountedPawns[activeCountedPawnCount++] = pawn;
            }
        };
        for (int resolvedIdx = 0; resolvedIdx < playerResolvedSlotCount; ++resolvedIdx) {
            const int i = playerResolvedSlots[resolvedIdx];
            if (!isFreshLiveCoreSlot(i))
                continue;
            ++resolvedLiveCount;
            if (!activeCountedSlots[i] &&
                !isActivePawnCounted(pawns[i])) {
                markActiveSlotCounted(i, pawns[i]);
                ++activeCount;
            }
        }
        bool localAlreadyCounted = false;
        for (int i = 0; i < 64; ++i) {
            if (activeCountedSlots[i] && isLocalActiveSlot(i)) {
                localAlreadyCounted = true;
                break;
            }
        }
        if (localAliveEvidence && !localAlreadyCounted)
            ++activeCount;
        for (int i = 0; i < 64; ++i) {
            if (!s_players[i].valid || s_players[i].health <= 0)
                continue;
            if (s_players[i].team != 2 && s_players[i].team != 3)
                continue;
            if (!isValidWorldPos(s_players[i].position))
                continue;
            ++committedLiveCount;
            if (activeCountedSlots[i] ||
                isLocalActiveSlot(i) ||
                isActivePawnCounted(s_players[i].pawn)) {
                continue;
            }
            markActiveSlotCounted(i, s_players[i].pawn);
            ++activeCount;
        }

        {
            static uint64_t s_populationWatchdogResetSerial = 0;
            static uint32_t s_populationWatchdogStreak = 0;
            static uint64_t s_populationWatchdogSinceUs = 0;
            static uint64_t s_lastPopulationWatchdogRefreshUs = 0;
            static uint64_t s_lastPopulationWatchdogHardUs = 0;
            static uint64_t s_launchUnderresolvedSinceUs = 0;
            static uint64_t s_lastLaunchUnderresolvedRefreshUs = 0;
            static int s_expectedControllersCount = 0;
            static int s_stableLowControllersValue = 0;
            static uint64_t s_stableLowControllersSinceUs = 0;
            static bool s_didStableLowControllerProbe = false;
            static bool s_didLaunchProbe = false;
            static bool s_didLaunchRepair = false;
            static bool s_didLaunchFull = false;
            const uint64_t watchdogResetSerial = s_sceneResetSerial.load(std::memory_order_relaxed);
            if (s_populationWatchdogResetSerial != watchdogResetSerial) {
                s_populationWatchdogResetSerial = watchdogResetSerial;
                s_populationWatchdogStreak = 0;
                s_populationWatchdogSinceUs = 0;
                s_lastPopulationWatchdogRefreshUs = 0;
                s_lastPopulationWatchdogHardUs = 0;
                s_launchUnderresolvedSinceUs = 0;
                s_lastLaunchUnderresolvedRefreshUs = 0;
                s_expectedControllersCount = 0;
                s_stableLowControllersValue = 0;
                s_stableLowControllersSinceUs = 0;
                s_didStableLowControllerProbe = false;
                s_didLaunchProbe = false;
                s_didLaunchRepair = false;
                s_didLaunchFull = false;
            }

            int resolvedControllersCount = 0;
            for (int idx = 0; idx < 64; ++idx) {
                if (controllers[idx] != 0) {
                    ++resolvedControllersCount;
                }
            }

            const bool liveMatchContext = populationLiveContext;
            uintptr_t populationLocalPawn = 0;
            {
                std::lock_guard<std::mutex> lock(s_dataMutex);
                populationLocalPawn = s_localPawn;
            }
            CameraFrame populationCameraFrame = {};
            const bool hasPopulationCamera = ReadCameraFrame(populationCameraFrame);
            const uint64_t populationCameraSampledAtUs = TickNowUs();
            const bool populationCameraLocalFresh =
                hasPopulationCamera && esp::state::ShouldApplyCameraLocalPosition(
                    populationCameraFrame.sceneSerial,
                    s_sceneResetSerial.load(std::memory_order_relaxed),
                    populationCameraFrame.localPosPawn, populationLocalPawn,
                    populationCameraFrame.localPosValid,
                    populationCameraFrame.localPosUpdatedUs,
                    populationCameraSampledAtUs, kLiveCameraFreshnessUs);
            const bool populationLocalTrackingStalled =
                !populationCameraLocalFresh;
            const auto populationWarmupState =
                static_cast<esp::SceneWarmupState>(s_sceneWarmupState.load(std::memory_order_relaxed));
            const uint64_t populationLastResetUs = s_lastSceneResetUs.load(std::memory_order_relaxed);
            const uint64_t populationWarmupEnteredUs = s_sceneWarmupEnteredUs.load(std::memory_order_relaxed);
            const uint64_t populationResetAgeUs =
                populationLastResetUs > 0 && nowUs >= populationLastResetUs
                ? nowUs - populationLastResetUs
                : 0;
            const uint64_t populationWarmupAgeUs =
                populationWarmupEnteredUs > 0 && nowUs >= populationWarmupEnteredUs
                ? nowUs - populationWarmupEnteredUs
                : 0;
            const bool populationGraceElapsed =
                esp::data::IsPopulationGraceElapsed(
                    populationMapKnown,
                    populationResetAgeUs,
                    populationWarmupAgeUs);
            const int observedPopulation = std::max(activeCount, resolvedLiveCount);
            const bool healthyPopulationSample =
                liveMatchContext &&
                populationWarmupState == esp::SceneWarmupState::Stable &&
                observedPopulation >= 1 &&
                observedPopulation <= 64 &&
                playerResolvedSlotCount >= 1;
            if (healthyPopulationSample) {
                if (resolvedControllersCount > s_expectedControllersCount ||
                    s_expectedControllersCount == 0) {
                    s_expectedControllersCount = resolvedControllersCount;
                    s_stableLowControllersValue = 0;
                    s_stableLowControllersSinceUs = 0;
                    s_didStableLowControllerProbe = false;
                } else if (resolvedControllersCount > 0 &&
                           resolvedControllersCount < s_expectedControllersCount) {
                    if (s_stableLowControllersValue != resolvedControllersCount) {
                        s_stableLowControllersValue = resolvedControllersCount;
                        s_stableLowControllersSinceUs = nowUs;
                        s_didStableLowControllerProbe = false;
                    } else if (esp::data::ShouldAcceptStableLowControllerPopulation(
                                   s_stableLowControllersSinceUs,
                                   nowUs)) {
                        s_expectedControllersCount = resolvedControllersCount;
                        s_stableLowControllersValue = 0;
                        s_stableLowControllersSinceUs = 0;
                        s_didStableLowControllerProbe = false;
                    }
                } else {
                    s_stableLowControllersValue = 0;
                    s_stableLowControllersSinceUs = 0;
                    s_didStableLowControllerProbe = false;
                }
            }

            const bool launchUnderresolved =
                esp::data::IsLaunchUnderresolvedPopulation(
                    liveMatchContext,
                    localAliveEvidence,
                    populationLocalTrackingStalled,
                    populationResetAgeUs,
                    highestEntityIndex,
                    resolvedControllersCount,
                    playerResolvedSlotCount);
            if (launchUnderresolved) {
                if (s_launchUnderresolvedSinceUs == 0)
                    s_launchUnderresolvedSinceUs = nowUs;
                const uint64_t underresolvedAgeUs =
                    nowUs >= s_launchUnderresolvedSinceUs
                    ? nowUs - s_launchUnderresolvedSinceUs
                    : 0;
                if (populationWarmupState == esp::SceneWarmupState::Stable)
                    setSceneWarmupState(esp::SceneWarmupState::HierarchyWarming);
                const bool launchRefreshCooldownElapsed =
                    esp::data::IsLaunchUnderresolvedRefreshCooldownElapsed(
                        s_lastLaunchUnderresolvedRefreshUs,
                        nowUs);
                const auto launchAction =
                    esp::data::SelectLaunchUnderresolvedAction(
                        underresolvedAgeUs,
                        launchRefreshCooldownElapsed,
                        s_didLaunchProbe,
                        s_didLaunchRepair,
                        s_didLaunchFull,
                        populationLocalTrackingStalled);
                if (launchAction == esp::data::LaunchUnderresolvedAction::Probe) {
                        s_didLaunchProbe = true;
                        s_lastLaunchUnderresolvedRefreshUs = nowUs;
                        setSceneWarmupState(esp::SceneWarmupState::HierarchyWarming);
                        refreshDmaCaches("launch_underresolved_players_probe", DmaRefreshTier::Probe, false);
                } else if (launchAction == esp::data::LaunchUnderresolvedAction::Repair) {
                        s_didLaunchRepair = true;
                        s_lastLaunchUnderresolvedRefreshUs = nowUs;
                        setSceneWarmupState(esp::SceneWarmupState::HierarchyWarming);
                        refreshDmaCaches("launch_underresolved_players_repair", DmaRefreshTier::Repair, false);
                } else if (launchAction == esp::data::LaunchUnderresolvedAction::Full) {
                        s_didLaunchFull = true;
                        s_lastLaunchUnderresolvedRefreshUs = nowUs;
                        setSceneWarmupState(esp::SceneWarmupState::Recovery);
                        refreshDmaCaches("launch_underresolved_players_full", DmaRefreshTier::Full, false);
                }
            } else {
                s_launchUnderresolvedSinceUs = 0;
                s_lastLaunchUnderresolvedRefreshUs = 0;
                s_didLaunchProbe = false;
                s_didLaunchRepair = false;
                s_didLaunchFull = false;
            }

            const bool watchdogEligible =
                liveMatchContext &&
                populationGraceElapsed &&
                highestEntityIndex >= 64 &&
                !launchUnderresolved &&
                !s_dmaRecovering.load(std::memory_order_relaxed) &&
                !s_dmaRecoveryRequested.load(std::memory_order_relaxed);
            const bool hierarchyBlackout =
                watchdogEligible &&
                localAliveEvidence &&
                playerResolvedSlotCount == 0;
            const bool coreBlackout =
                watchdogEligible &&
                localAliveEvidence &&
                playerResolvedSlotCount >= 2 &&
                resolvedLiveCount == 0;
            const bool activeBlackout =
                watchdogEligible &&
                localAliveEvidence &&
                activeCount == 0;
            const bool stableLowControllerPopulationPending =
                esp::data::IsStableLowControllerGuardActive(
                    s_stableLowControllersSinceUs,
                    nowUs) &&
                resolvedControllersCount > 0 &&
                resolvedControllersCount < s_expectedControllersCount;
            const bool controllerPopulationCollapsed =
                esp::data::IsControllerPopulationCollapsed(
                    watchdogEligible,
                    localAliveEvidence,
                    s_expectedControllersCount,
                    resolvedControllersCount,
                    stableLowControllerPopulationPending ||
                        s_didStableLowControllerProbe);
            const bool staleCommittedPopulation =
                esp::data::IsStaleCommittedPopulation(
                    watchdogEligible,
                    resolvedLiveCount,
                    committedLiveCount,
                    resolvedControllersCount);
            const bool populationBroken =
                hierarchyBlackout ||
                coreBlackout ||
                activeBlackout ||
                controllerPopulationCollapsed ||
                staleCommittedPopulation;

            if (populationBroken) {
                if (s_populationWatchdogSinceUs == 0)
                    s_populationWatchdogSinceUs = nowUs;
                if (s_populationWatchdogStreak < 0xFFFFFFFFu)
                    ++s_populationWatchdogStreak;

                const uint64_t brokenAgeUs =
                    nowUs >= s_populationWatchdogSinceUs
                    ? nowUs - s_populationWatchdogSinceUs
                    : 0;
                if (esp::data::IsPopulationWatchdogRefreshDue(
                        s_lastPopulationWatchdogRefreshUs,
                        nowUs,
                        s_populationWatchdogStreak,
                        brokenAgeUs)) {
                    s_lastPopulationWatchdogRefreshUs = nowUs;
                    const auto refreshKind =
                        controllerPopulationCollapsed
                            ? esp::data::PopulationWatchdogRefreshKind::Probe
                            : esp::data::SelectPopulationWatchdogRefreshKind(brokenAgeUs);
                    const bool hardRefresh =
                        refreshKind == esp::data::PopulationWatchdogRefreshKind::Full;
                    const bool repairRefresh =
                        refreshKind != esp::data::PopulationWatchdogRefreshKind::Probe;
                    const char* reason =
                        hierarchyBlackout ? "population_watchdog_hierarchy_blackout" :
                        coreBlackout ? "population_watchdog_core_blackout" :
                        activeBlackout ? "population_watchdog_active_blackout" :
                        controllerPopulationCollapsed ? "population_watchdog_active_collapse" :
                        "population_watchdog_stale_committed_players";
                    if (controllerPopulationCollapsed)
                        s_didStableLowControllerProbe = true;
                    if (esp::data::ShouldSoftResetStaleCommittedPopulation(
                            staleCommittedPopulation,
                            brokenAgeUs)) {
                        const auto reset = esp::recovery::EvaluateResetPolicy(
                            esp::recovery::ResetTrigger::PopulationWatchdogStaleCommitted);
                        ResetRuntimeStateSoft(reset.reason);
                        activeCount = 0;
                        resolvedLiveCount = 0;
                    } else if (repairRefresh) {
                        BumpSceneReset(nowUs);
                    }
                    setSceneWarmupState(esp::SceneWarmupState::Recovery);
                    refreshDmaCaches(
                        reason,
                        hardRefresh ? DmaRefreshTier::Full :
                            repairRefresh ? DmaRefreshTier::Repair : DmaRefreshTier::Probe,
                        false);
                    if (esp::data::ShouldRequestPopulationWatchdogRecovery(
                            refreshKind,
                            s_lastPopulationWatchdogHardUs,
                            nowUs)) {
                        s_lastPopulationWatchdogHardUs = nowUs;
                        RequestDmaRecovery(reason);
                    }
                }
            } else {
                s_populationWatchdogStreak = 0;
                s_populationWatchdogSinceUs = 0;
            }

        }

        s_activePlayerCount.store(activeCount, std::memory_order_relaxed);
        s_highestEntityIdxStat.store(highestEntityIndex, std::memory_order_relaxed);
        s_worldMarkerCountStat.store(s_worldMarkerCount, std::memory_order_relaxed);
        uint32_t bombFlags = 0;
        if (s_bombState.planted)
            bombFlags |= 1u << 0;
        if (s_bombState.ticking)
            bombFlags |= 1u << 1;
        if (s_bombState.beingDefused)
            bombFlags |= 1u << 2;
        if (s_bombState.dropped)
            bombFlags |= 1u << 3;
        if (s_bombState.boundsValid)
            bombFlags |= 1u << 4;
        const bool publishedBombPositionValid =
            (s_bombState.planted || s_bombState.dropped) && IsFiniteVec(s_bombState.position);
        if (publishedBombPositionValid)
            bombFlags |= 1u << 5;
        s_bombDebugPositionSampleUs.store(
            publishedBombPositionValid ? s_bombState.positionSampleTimeUs : 0,
            std::memory_order_relaxed);
        s_bombDebugFlags.store(bombFlags, std::memory_order_relaxed);
        s_bombDebugSourceFlags.store(s_bombState.sourceFlags, std::memory_order_relaxed);
        s_bombDebugRawFlags.store(bombRawDebugFlags, std::memory_order_relaxed);
        s_bombDropPublicationDebug.store(bombDropPublicationDebug, std::memory_order_relaxed);
        s_bombDebugConfidence.store(s_bombState.confidence, std::memory_order_relaxed);
        s_bombDebugDefuserSlot.store(bombDefuserSlotDebug, std::memory_order_relaxed);
        const auto countdownMs = [](float endTime, float gameTime) -> int32_t {
            if (!std::isfinite(endTime) ||
                !std::isfinite(gameTime) ||
                endTime <= gameTime) {
                return -1;
            }
            return static_cast<int32_t>(std::clamp(
                (endTime - gameTime) * 1000.0f,
                0.0f,
                120000.0f));
        };
        s_bombDebugBlowLeftMs.store(
            countdownMs(s_bombState.blowTime, s_bombState.currentGameTime),
            std::memory_order_relaxed);
        s_bombDebugDefuseLeftMs.store(
            countdownMs(s_bombState.defuseEndTime, s_bombState.currentGameTime),
            std::memory_order_relaxed);
    }

    static bool s_webRadarCacheLive = false;
    static uint64_t s_webRadarCacheSceneSerial = 0;
    if (webRadarDemandActive) {
        s_webRadarCacheLive = true;
        std::lock_guard<std::mutex> lock(s_dataMutex);
        const uint64_t radarSceneSerial = s_sceneResetSerial.load(std::memory_order_relaxed);
        if (s_webRadarCacheSceneSerial != radarSceneSerial) {
            s_webRadarCacheSceneSerial = radarSceneSerial;
            memset(s_webRadarPlayers, 0, sizeof(s_webRadarPlayers));
        }
        const uint64_t radarNowUs = TickNowUs();
        const bool radarBulkRecovery = esp::render::IsWithinWebRadarBulkRecovery(
            s_lastBulkEvictionUs.load(std::memory_order_relaxed), radarNowUs);
        auto canHoldRadarCore = [&](const esp::PlayerData& player) {
            return player.valid && player.pawn != 0 &&
                esp::render::IsWebRadarCoreSampleFresh(
                    player.coreUpdatedAtUs, radarNowUs, player.health, radarBulkRecovery);
        };
        for (int i = 0; i < 64; ++i) {
            // A watchdog may reset the scene after this cycle's core read.
            // Do not republish pre-reset scratch as a player of the new scene.
            if (radarSceneSerial != coreBatchResetSerial || hierarchyIdentityRejected[i]) {
                s_webRadarPlayers[i] = {};
                continue;
            }
            const bool isLocalByIndex =
                localPlayerIndexValid &&
                localPlayerIndexHasLiveEvidence &&
                (i == localPlayerIndex);
            const bool isLocalByControllerSlot =
                localControllerMaskBit > 0 &&
                localControllerMaskBit <= 64 &&
                (i == (localControllerMaskBit - 1));
            const bool isLocalByPawn =
                s_localPawn != 0 &&
                (pawns[i] == s_localPawn);
            const bool localControllerPawnHandleValid =
                localControllerPawnHandle != 0u &&
                localControllerPawnHandle != 0xFFFFFFFFu;
            const bool isLocalByHandle =
                localControllerPawnHandleValid &&
                pawnHandles[i] != 0u &&
                pawnHandles[i] != 0xFFFFFFFFu &&
                pawnHandles[i] == localControllerPawnHandle;
            const bool isLocalWebRadarSlot =
                isLocalByIndex ||
                isLocalByControllerSlot ||
                isLocalByPawn ||
                isLocalByHandle;
            if (isLocalWebRadarSlot) {
                s_webRadarPlayers[i] = {};
                continue;
            }

            
            
            
            if (!pawns[i]) {
                if (canHoldRadarCore(s_webRadarPlayers[i])) {
                    s_webRadarPlayers[i].staleFrames =
                        std::min(255, s_webRadarPlayers[i].staleFrames + 1);
                    continue;
                }
                s_webRadarPlayers[i] = {};
                continue;
            }

            Vector3 webRadarPos = positions[i];
            bool haveWebRadarPos = isValidWorldPos(webRadarPos);
            const bool teamValid = (teams[i] == 2 || teams[i] == 3);
            const bool coreReliable = coreReadFresh[i] && coreReadPlausible[i];
            const bool vitalSampleFresh =
                coreHealthBytesRead[i] == sizeof(healths[i]) &&
                coreLifeStateBytesRead[i] == sizeof(lifeStates[i]) &&
                healths[i] >= 0 && healths[i] <= 500 && lifeStates[i] <= 2;
            const bool isDead = esp::data::IsAuthoritativeDeadCoreSample(
                vitalSampleFresh, pawns[i], healths[i], lifeStates[i]);
            const bool cachedSamePawn =
                s_webRadarPlayers[i].valid &&
                s_webRadarPlayers[i].pawn == pawns[i];
            if (!haveWebRadarPos && cachedSamePawn && isValidWorldPos(s_webRadarPlayers[i].position)) {
                webRadarPos = s_webRadarPlayers[i].position;
                haveWebRadarPos = true;
            }
            if ((!coreReliable || !teamValid || !haveWebRadarPos) && cachedSamePawn) {
                esp::PlayerData& cached = s_webRadarPlayers[i];
                if (isDead) {
                    cached.health = 0;
                    cached.velocity = {};
                    cached.hasBomb = false;
                    cached.hasBones = false;
                    cached.bonesUpdatedAtUs = 0;
                    cached.coreUpdatedAtUs = playerCoreBatchCaptureUs;
                }
                if (canHoldRadarCore(cached)) {
                    cached.staleFrames = std::min(255, cached.staleFrames + 1);
                    continue;
                }
            }
            if (!coreReliable || !haveWebRadarPos || !teamValid) {
                s_webRadarPlayers[i] = {};
                if (isDead) {
                    // Preserve authoritative death even without a drawable position;
                    // the consumer must not resurrect its last alive cache entry.
                    s_webRadarPlayers[i].valid = true;
                    s_webRadarPlayers[i].pawn = pawns[i];
                    s_webRadarPlayers[i].pawnHandle = pawnHandles[i];
                    s_webRadarPlayers[i].health = 0;
                    s_webRadarPlayers[i].coreUpdatedAtUs = playerCoreBatchCaptureUs;
                }
                continue;
            }
            esp::PlayerData& wp = s_webRadarPlayers[i];
            if (wp.pawn != pawns[i])
                wp = {};
            wp.valid = true;
            wp.pawn = pawns[i];
            wp.pawnHandle = pawnHandles[i];
            wp.coreUpdatedAtUs = playerCoreBatchCaptureUs;
            wp.staleFrames = 0;
            wp.health = isDead ? 0 : healths[i];
            wp.armor = std::clamp(armors[i], 0, 100);
            wp.team = teams[i];
            wp.money = std::max(0, moneys[i]);
            wp.ping = static_cast<int>(pings[i]);
            wp.position = webRadarPos;
            wp.velocity = isDead ? Vector3{} : velocities[i];
            wp.velocityValid = !isDead && velocityReadFresh[i];
            wp.scoped = scopedFlags[i] == 1u;
            wp.defusing = defusingFlags[i] == 1u;
            wp.hasDefuser = hasDefuserFlags[i] == 1u;
            wp.flashDuration = flashDurations[i];
            wp.flashed =
                esp::data::IsValidFlashDurationSample(wp.flashDuration) &&
                wp.flashDuration > esp::data::kFlashFlagReleaseSeconds;
            wp.eyeYaw = eyeAnglesPerPlayer[i].y;
            wp.visible = false;
            wp.visibilityUpdatedAtUs = 0;
            const uint16_t liveWeaponId = (weaponIds[i] < 20000u) ? weaponIds[i] : 0;
            uint16_t committedWeaponId = 0;
            int committedAmmoClip = ammoClips[i];
            resolveCommittedWeaponState(i, liveWeaponId, committedWeaponId, committedAmmoClip);
            wp.ammoClip = committedAmmoClip;
            wp.weaponId = committedWeaponId;
            wp.hasBomb = !isDead && (i == resolvedBombCarrierSlot);
            wp.hasBones = false;
            wp.bonesUpdatedAtUs = 0;
            collectGrenadesForSlot(i, wp.grenadeIds, wp.grenadeCount);
            memcpy(wp.name, names[i], 128);
            wp.name[127] = '\0';
            copyResolvedBones(i, wp);
        }
    } else if (s_webRadarCacheLive) {
        s_webRadarCacheLive = false;
        std::lock_guard<std::mutex> lock(s_dataMutex);
        memset(s_webRadarPlayers, 0, sizeof(s_webRadarPlayers));
    }

    if (minimapBoundsValid)
        HandleMapCalibration(minimapMins, minimapMaxs, liveMapKey);

    PublishCurrentSnapshot();

    s_requiredReadFailureCount = 0;
    MarkDmaReadSuccess();
