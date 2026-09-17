        uint32_t bombRawDebugFlags = 0;
        uint64_t bombDropPublicationDebug = 0;
        int bombDefuserSlotDebug = -1;
        if (bombPlantedByRules)
            bombRawDebugFlags |= 1u << 0;
        if (bombDroppedByRules)
            bombRawDebugFlags |= 1u << 1;
        if (plantedC4Entity)
            bombRawDebugFlags |= 1u << 2;
        if (weaponC4Entity)
            bombRawDebugFlags |= 1u << 3;
        if (bombTicking != 0)
            bombRawDebugFlags |= 1u << 4;
        if (bombBeingDefused != 0)
            bombRawDebugFlags |= 1u << 5;
        if (bombActivated != 0)
            bombRawDebugFlags |= 1u << 6;
        if (bombHasExploded != 0)
            bombRawDebugFlags |= 1u << 7;
        if (bombDefused != 0)
            bombRawDebugFlags |= 1u << 8;
        if (isValidEntityHandle(bombDefuserHandle))
            bombRawDebugFlags |= 1u << 9;
        if (isValidWorldPos(bombWorldPos))
            bombRawDebugFlags |= 1u << 10;
        if (plantedMetaDue)
            bombRawDebugFlags |= 1u << 11;
        if (plantedC4Entity && plantedMetaReadSucceeded)
            bombRawDebugFlags |= 1u << 12;
        if (weaponC4ResolutionMode == 1)
            bombRawDebugFlags |= 1u << 20;
        else if (weaponC4ResolutionMode == 2)
            bombRawDebugFlags |= 1u << 21;
        else if (weaponC4ResolutionMode == 3)
            bombRawDebugFlags |= 1u << 22;
        else if (weaponC4ResolutionMode == 4)
            bombRawDebugFlags |= 1u << 23;
        else if (weaponC4ResolutionMode == 5)
            bombRawDebugFlags |= 1u << 28;
        else if (weaponC4ResolutionMode == 6)
            bombRawDebugFlags |= 1u << 29;
        if (weaponC4PosValid)
            bombRawDebugFlags |= 1u << 24;
        if (weaponC4OwnerValid)
            bombRawDebugFlags |= 1u << 25;
        if (weaponC4OwnerPlayerIndex >= 0)
            bombRawDebugFlags |= 1u << 26;
        if (inventoryC4CarrierEvidence)
            bombRawDebugFlags |= 1u << 27;

        const int bombCommitSignOnState = s_engineSignOnState.load(std::memory_order_relaxed);
        const bool bombCommitLiveContext =
            s_engineInGame.load(std::memory_order_relaxed) &&
            !s_engineMenu.load(std::memory_order_relaxed) &&
            bombCommitSignOnState == 6;
        static uint64_t s_timerContinuityEpoch = 0;
        static uint64_t s_lastAuthoritativeBombTimerUs = 0;
        static uint64_t s_lastAuthoritativeDefuseUs = 0;
        const uint64_t timerContinuityEpoch =
            s_bombEpoch.load(std::memory_order_relaxed);
        if (s_timerContinuityEpoch != timerContinuityEpoch) {
            s_timerContinuityEpoch = timerContinuityEpoch;
            s_lastAuthoritativeBombTimerUs = 0;
            s_lastAuthoritativeDefuseUs = 0;
        }

        if (!bombCommitLiveContext) {
            s_bombState = {};
            s_bombState.position = { NAN, NAN, NAN };
            s_lastAuthoritativeBombTimerUs = 0;
            s_lastAuthoritativeDefuseUs = 0;
        } else {
        const bool previousBombTimerLive = s_bombState.ticking;
        const float previousBlowTime = s_bombState.blowTime;
        const float previousTimerLength = s_bombState.timerLength;
        const bool previousDefuseLive = s_bombState.beingDefused;
        const float previousDefuseEndTime = s_bombState.defuseEndTime;
        const float previousDefuseLength = s_bombState.defuseLength;
        const bool bombHasDroppedEvidence = IsDroppedBombResolveKind(bombResolve.kind);
        const uint32_t stickyPositionSources =
            BombResolveSourceStickyDrop |
            BombResolveSourceStickyState |
            BombResolveSourcePositionFallback;
        const bool bombDroppedEvidenceIsCurrent =
            bombHasDroppedEvidence &&
            (bombResolve.sourceFlags & stickyPositionSources) == 0u;
        const bool bombTerminalNow =
            (ofs.C_PlantedC4_m_bHasExploded > 0 && bombHasExploded != 0) ||
            (ofs.C_PlantedC4_m_bBombDefused > 0 && bombDefused != 0);

        const bool bombDefinitelyCarried =
            bombResolve.kind == BombResolveKind::Carried;
        
        
        
        
        
        const bool bombPlantedTransition =
            bombResolve.kind == BombResolveKind::Planted;
        if (bombDefinitelyCarried || bombPlantedTransition) {
            s_lastDroppedBombPos = { NAN, NAN, NAN };
            s_lastDroppedBombPosUs = 0;
            if (bombDefinitelyCarried) {
                s_lastVisibleBombPos = { NAN, NAN, NAN };
                s_lastVisibleBombBoundsMins = {};
                s_lastVisibleBombBoundsMaxs = {};
                s_lastVisibleBombBoundsValid = false;
                s_lastVisibleBombPosUs = 0;
                s_lastConfirmedBombState = {};
                s_lastConfirmedBombStateUs = 0;
            }
        }

        static esp::data::C4PositionConfirmation s_dropConfirmation;
        static uint64_t s_unconfirmedBombResetEpoch = 0;
        const uint64_t unconfirmedBombEpoch =
            s_bombEpoch.load(std::memory_order_relaxed);
        if (s_unconfirmedBombResetEpoch != unconfirmedBombEpoch) {
            s_unconfirmedBombResetEpoch = unconfirmedBombEpoch;
            s_dropConfirmation.Reset();
        }
        const esp::data::C4EntityKey currentKey = {
            weaponC4Entity,
            weaponC4ListOrdinal,
            weaponC4SceneNode,
            s_bombDropGeneration,
            weaponC4DropTick
        };
        const bool hasFreshWeaponCandidate =
            bombHasDroppedEvidence &&
            (bombResolve.sourceFlags & BombResolveSourceWeaponEntity) != 0u &&
            (bombResolve.sourceFlags & stickyPositionSources) == 0u &&
            esp::data::IsBombPositionSampleFresh(
                bombResolve.positionSampleTimeUs, nowUs, esp::data::kWeaponC4PositionFreshUs) &&
            currentKey.IsValid() &&
            bombResolve.positionSampleTimeUs > 0 &&
            isValidWorldPos(bombResolve.position);
        if (bombDefinitelyCarried || bombPlantedTransition || bombTerminalNow)
            s_dropConfirmation.Reset();
        const bool matchedFreshSamples = s_dropConfirmation.Observe(
            currentKey, hasFreshWeaponCandidate && !bombTerminalNow,
            bombResolve.positionSampleTimeUs, nowUs,
            bombResolve.position.x, bombResolve.position.y, bombResolve.position.z);
        const bool weaponCandidateKeyChanged = s_dropConfirmation.keyChanged;

        if (esp::data::ShouldInvalidateChangedDropCandidateCache(
                weaponCandidateKeyChanged,
                s_bombState.dropped)) {
            s_lastDroppedBombPos = { NAN, NAN, NAN };
            s_lastDroppedBombPosUs = 0;
            s_lastVisibleBombPos = { NAN, NAN, NAN };
            s_lastVisibleBombBoundsMins = {};
            s_lastVisibleBombBoundsMaxs = {};
            s_lastVisibleBombBoundsValid = false;
            s_lastVisibleBombPosUs = 0;
            if (s_lastConfirmedBombState.dropped) {
                s_lastConfirmedBombState = {};
                s_lastConfirmedBombStateUs = 0;
            }
        }

        const bool worldScanConfirmed =
            (bombResolve.sourceFlags & BombResolveSourceWorldC4) != 0u &&
            (bombResolve.sourceFlags & stickyPositionSources) == 0u;
        const bool unconfirmedAllowed =
            esp::data::IsBombPositionSampleFresh(
                bombResolve.positionSampleTimeUs, nowUs, esp::data::kWeaponC4PositionFreshUs) &&
            esp::data::IsUnconfirmedDroppedC4PublicationAllowed(
                bombResolve.sourceFlags,
                bombResolve.confidence,
                matchedFreshSamples,
                worldScanConfirmed);
        const uint8_t publishedBombConfidence =
            esp::data::SelectPublishedDroppedC4Confidence(
                bombResolve.confidence,
                matchedFreshSamples,
                worldScanConfirmed);
        if (matchedFreshSamples)
            bombRawDebugFlags |= 1u << 30;
        if (unconfirmedAllowed)
            bombRawDebugFlags |= 1u << 31;

        bool droppedBombPosFromCurrentResolve = false;
        uint64_t selectedDroppedSampleUs = 0;
        // The resolver's candidate is not yet authorized for display. In
        // particular, do not leave its coordinates in these shared locals when
        // the confirmation gate rejects it and no valid cache exists.
        droppedBombPos = { NAN, NAN, NAN };
        droppedBombPosValid = false;
        droppedBombBoundsMins = {};
        droppedBombBoundsMaxs = {};
        droppedBombBoundsValid = false;
        const auto droppedPositionSource = esp::data::SelectDroppedC4PositionSource(
            bombHasDroppedEvidence && !bombTerminalNow,
            isValidWorldPos(bombResolve.position), unconfirmedAllowed,
            !bombEpochJustWiped && !weaponCandidateKeyChanged && !bombDefinitelyCarried,
            isValidWorldPos(s_lastDroppedBombPos), s_lastDroppedBombPosUs, nowUs,
            droppedStickyUs);
        if (droppedPositionSource == esp::data::DroppedC4PositionSource::Current) {
            droppedBombPos = bombResolve.position;
            droppedBombPosValid = true;
            droppedBombBoundsMins = bombResolve.boundsMins;
            droppedBombBoundsMaxs = bombResolve.boundsMaxs;
            droppedBombBoundsValid = bombResolve.boundsValid;
            droppedBombPosFromCurrentResolve = true;
            selectedDroppedSampleUs = bombResolve.positionSampleTimeUs;
            if (bombDroppedEvidenceIsCurrent) {
                s_lastDroppedBombPos = droppedBombPos;
                s_lastDroppedBombPosUs = selectedDroppedSampleUs;
            }
        } else if (droppedPositionSource == esp::data::DroppedC4PositionSource::Cache) {
            droppedBombPos = s_lastDroppedBombPos;
            droppedBombPosValid = true;
            selectedDroppedSampleUs = s_lastDroppedBombPosUs;
            droppedBombBoundsMins = {};
            droppedBombBoundsMaxs = {};
            droppedBombBoundsValid = false;
        }

        using DropStatus = esp::data::DroppedC4PublicationStatus;
        const bool candidatePositionValid = isValidWorldPos(bombResolve.position);
        const bool candidatePositionFresh = esp::data::IsBombPositionSampleFresh(
            bombResolve.positionSampleTimeUs, nowUs, esp::data::kWeaponC4PositionFreshUs);
        const DropStatus dropStatus =
            droppedPositionSource == esp::data::DroppedC4PositionSource::Current ? DropStatus::Current :
            droppedPositionSource == esp::data::DroppedC4PositionSource::Cache ? DropStatus::Cache :
            !bombHasDroppedEvidence && !bombDroppedByRules ? DropStatus::Inactive :
            !candidatePositionValid ? DropStatus::NoCandidate :
            !candidatePositionFresh ? DropStatus::StaleSample :
            !worldScanConfirmed && !currentKey.IsValid() ? DropStatus::InvalidIdentity :
            !worldScanConfirmed && !matchedFreshSamples ? DropStatus::Confirming : DropStatus::SourceRejected;
        // One atomic diagnostic word keeps gate reason/count/lookup provenance
        // from different data cycles from being mixed by the UI.
        bombDropPublicationDebug = static_cast<uint64_t>(dropStatus) |
            (static_cast<uint64_t>(s_dropConfirmation.samples) << 8) |
            (static_cast<uint64_t>(weaponC4ResolutionMode) << 16) |
            (static_cast<uint64_t>(candidatePositionValid) << 24) |
            (static_cast<uint64_t>(currentKey.IsValid()) << 25) |
            (static_cast<uint64_t>(candidatePositionFresh) << 26) |
            (static_cast<uint64_t>(weaponC4ListOrdinal) << 32);

        s_bombState.planted = (bombResolve.kind == BombResolveKind::Planted);
        s_bombState.ticking = false;
        s_bombState.beingDefused = false;
        s_bombState.dropped = bombHasDroppedEvidence;
        const bool usingDroppedPositionFallback =
            bombHasDroppedEvidence &&
            droppedBombPosValid &&
            !droppedBombPosFromCurrentResolve;
        if (usingDroppedPositionFallback) {
            const bool reusableConfirmedDrop =
                s_lastConfirmedBombState.dropped &&
                isValidWorldPos(s_lastConfirmedBombState.position);
            s_bombState.sourceFlags = BombResolveSourceStickyDrop |
                (reusableConfirmedDrop ? s_lastConfirmedBombState.sourceFlags : 0u);
            s_bombState.confidence =
                reusableConfirmedDrop
                    ? s_lastConfirmedBombState.confidence
                    : static_cast<uint8_t>(96);
        } else {
            s_bombState.sourceFlags = bombResolve.sourceFlags;
            s_bombState.confidence = publishedBombConfidence;
        }
        s_bombState.velocity = {};
        s_bombState.positionSampleTimeUs = 0;

        Vector3 targetBombPos = { NAN, NAN, NAN };
        Vector3 targetBombBoundsMins = {};
        Vector3 targetBombBoundsMaxs = {};
        bool targetBombBoundsValid = false;
        bool targetBombFromCurrentEvidence = false;
        uint64_t targetBombSampleUs = 0;

        if (bombResolve.kind == BombResolveKind::Planted && isValidWorldPos(bombResolve.position)) {
            targetBombPos = bombResolve.position;
            targetBombSampleUs = bombResolve.positionSampleTimeUs;
            targetBombBoundsMins = bombResolve.boundsMins;
            targetBombBoundsMaxs = bombResolve.boundsMaxs;
            targetBombBoundsValid =
                bombResolve.boundsValid &&
                isValidBombBounds(targetBombBoundsMins, targetBombBoundsMaxs);
            targetBombFromCurrentEvidence =
                (bombResolve.sourceFlags & stickyPositionSources) == 0u;
        } else if (bombHasDroppedEvidence && droppedBombPosValid && isValidWorldPos(droppedBombPos)) {
            targetBombPos = droppedBombPos;
            targetBombSampleUs = selectedDroppedSampleUs;
            targetBombBoundsMins = droppedBombBoundsMins;
            targetBombBoundsMaxs = droppedBombBoundsMaxs;
            targetBombBoundsValid =
                droppedBombBoundsValid &&
                isValidBombBounds(targetBombBoundsMins, targetBombBoundsMaxs);
            targetBombFromCurrentEvidence =
                droppedBombPosFromCurrentResolve &&
                (bombResolve.sourceFlags & stickyPositionSources) == 0u;
        } else if (!sceneSettling &&
                   s_lastVisibleBombPosUs > 0 &&
                   nowUs >= s_lastVisibleBombPosUs &&
                   (nowUs - s_lastVisibleBombPosUs) <= esp::intervals::kBombStickyVisibleUs &&
                   isValidWorldPos(s_lastVisibleBombPos) &&
                   esp::data::IsBombPositionModeCompatible(
                       s_bombState.planted, s_bombState.dropped,
                       s_lastConfirmedBombState.planted, s_lastConfirmedBombState.dropped) &&
                   (s_bombState.planted || s_bombState.dropped)) {
            targetBombPos = s_lastVisibleBombPos;
            targetBombSampleUs = s_lastVisibleBombPosUs;
            s_bombState.sourceFlags |= BombResolveSourcePositionFallback;
            targetBombBoundsMins = s_lastVisibleBombBoundsMins;
            targetBombBoundsMaxs = s_lastVisibleBombBoundsMaxs;
            targetBombBoundsValid = s_lastVisibleBombBoundsValid;
        } else if (!sceneSettling &&
                   !bombTerminalNow &&
                   !bombDefinitelyCarried &&
                   esp::data::IsBombPositionModeCompatible(
                       s_bombState.planted, s_bombState.dropped,
                       s_lastConfirmedBombState.planted, s_lastConfirmedBombState.dropped, true) &&
                   s_lastConfirmedBombStateUs > 0 &&
                   nowUs >= s_lastConfirmedBombStateUs &&
                   (nowUs - s_lastConfirmedBombStateUs) <= 300000 &&
                   isValidWorldPos(s_lastConfirmedBombState.position) &&
                   (s_lastConfirmedBombState.planted || s_lastConfirmedBombState.dropped)) {
            if (!s_bombState.planted && !s_bombState.dropped) {
                s_bombState.planted = s_lastConfirmedBombState.planted;
                s_bombState.ticking = false;
                s_bombState.beingDefused = false;
                s_bombState.dropped = s_lastConfirmedBombState.dropped;
                s_bombState.sourceFlags = s_lastConfirmedBombState.sourceFlags;
                s_bombState.confidence = s_lastConfirmedBombState.confidence;
                s_bombState.blowTime = 0.0f;
                s_bombState.timerLength = 0.0f;
                s_bombState.defuseEndTime = 0.0f;
                s_bombState.defuseLength = 0.0f;
            }
            targetBombPos = s_lastConfirmedBombState.position;
            targetBombSampleUs = s_lastConfirmedBombStateUs;
            s_bombState.sourceFlags |= BombResolveSourcePositionFallback;
            targetBombBoundsMins = s_lastConfirmedBombState.boundsMins;
            targetBombBoundsMaxs = s_lastConfirmedBombState.boundsMaxs;
            targetBombBoundsValid = s_lastConfirmedBombState.boundsValid;
        }

        if (!targetBombBoundsValid && isValidWorldPos(targetBombPos)) {
            if (s_bombState.planted) {
                targetBombBoundsMins = Vector3(-9.0f, -9.0f, -1.0f);
                targetBombBoundsMaxs = Vector3(9.0f, 9.0f, 20.0f);
            } else {
                targetBombBoundsMins = Vector3(-10.0f, -10.0f, -3.0f);
                targetBombBoundsMaxs = Vector3(10.0f, 10.0f, 10.0f);
            }
            targetBombBoundsValid = true;
        }

        if (isValidWorldPos(targetBombPos)) {
            if (targetBombFromCurrentEvidence) {
                s_lastVisibleBombPos = targetBombPos;
                s_lastVisibleBombBoundsMins = targetBombBoundsMins;
                s_lastVisibleBombBoundsMaxs = targetBombBoundsMaxs;
                s_lastVisibleBombBoundsValid = targetBombBoundsValid;
                s_lastVisibleBombPosUs = targetBombSampleUs;
            }
            s_bombState.position = targetBombPos;
            s_bombState.velocity = bombResolve.velocity;
            s_bombState.positionSampleTimeUs = targetBombSampleUs;
            s_bombState.positionGeneration = s_bombDropGeneration;
            if (targetBombFromCurrentEvidence) {
                s_bombState.positionEntity = s_bombState.planted ? plantedC4Entity :
                    ((bombResolve.sourceFlags & BombResolveSourceWorldC4) != 0u
                        ? worldScanC4Entity : weaponC4Entity);
            }
        } else {
            s_bombState.position = { NAN, NAN, NAN };
            s_bombState.positionGeneration = 0;
            s_bombState.positionEntity = 0;
        }

        s_bombState.boundsMins = targetBombBoundsMins;
        s_bombState.boundsMaxs = targetBombBoundsMaxs;
        s_bombState.boundsValid = targetBombBoundsValid;

        const bool gameTimeValid =
            std::isfinite(currentGameTime) &&
            currentGameTime >= 0.0f &&
            currentGameTime < 100000.0f;
        const bool blowTimeValid =
            std::isfinite(bombBlowTime) &&
            gameTimeValid &&
            bombBlowTime > currentGameTime &&
            bombBlowTime < currentGameTime + 120.0f;
        const bool defuseTimeValid =
            std::isfinite(bombDefuseEndTime) &&
            gameTimeValid &&
            bombDefuseEndTime > currentGameTime &&
            bombDefuseEndTime < currentGameTime + 30.0f;
        const bool timerLengthValid =
            std::isfinite(bombTimerLength) &&
            bombTimerLength >= 5.0f &&
            bombTimerLength <= 90.0f;
        const bool defuseLengthValid =
            std::isfinite(bombDefuseLength) &&
            bombDefuseLength >= 4.0f &&
            bombDefuseLength <= 11.0f;
        if (gameTimeValid)
            bombRawDebugFlags |= 1u << 13;
        if (blowTimeValid)
            bombRawDebugFlags |= 1u << 14;
        if (defuseTimeValid)
            bombRawDebugFlags |= 1u << 15;
        if (plantedMetaFresh)
            bombRawDebugFlags |= 1u << 18;

        const bool c4DefuserHandleValid =
            isValidEntityHandle(bombDefuserHandle);
        int c4DefuserSlot = -1;
        if (c4DefuserHandleValid) {
            for (int resolvedIdx = 0; resolvedIdx < playerResolvedSlotCount; ++resolvedIdx) {
                const int i = playerResolvedSlots[resolvedIdx];
                if (i >= 0 && i < 64 && pawnHandles[i] == bombDefuserHandle) {
                    c4DefuserSlot = i;
                    break;
                }
            }
        }
        if (c4DefuserSlot >= 0) {
            bombDefuserSlotDebug = c4DefuserSlot;
            bombRawDebugFlags |= 1u << 17;
        }
        if (bombBeingDefused != 0)
            bombRawDebugFlags |= 1u << 16;
        const bool resolvedDefuserActivelyDefusing =
            c4DefuserSlot >= 0 &&
            c4DefuserSlot < 64 &&
            defusingFlags[c4DefuserSlot] == 1u;
        if (resolvedDefuserActivelyDefusing)
            bombRawDebugFlags |= 1u << 19;

        constexpr float kFallbackDefuseNoKitSeconds = 10.0f;
        const bool plantedLiveNow =
            s_bombState.planted &&
            !bombTerminalNow;
        const bool defuseWindowValid =
            defuseTimeValid &&
            bombDefuseEndTime <=
                currentGameTime +
                (defuseLengthValid ? bombDefuseLength : kFallbackDefuseNoKitSeconds) +
                1.0f;
        const bool authoritativeTickingNow =
            esp::data::IsAuthoritativeBombTimer(
                plantedLiveNow,
                bombTerminalNow,
                plantedMetaFresh,
                bombActivated != 0,
                bombTicking != 0,
                blowTimeValid);
        if (authoritativeTickingNow)
            s_lastAuthoritativeBombTimerUs = nowUs;
        const bool heldTickingNow =
            !authoritativeTickingNow &&
            esp::data::ShouldHoldTransientBombTimer(
                previousBombTimerLive,
                plantedLiveNow,
                bombTerminalNow,
                s_lastAuthoritativeBombTimerUs,
                nowUs,
                previousBlowTime,
                previousTimerLength,
                currentGameTime);
        const bool tickingLiveNow =
            authoritativeTickingNow || heldTickingNow;
        if (!tickingLiveNow)
            s_lastAuthoritativeBombTimerUs = 0;
        const float selectedBlowTime =
            authoritativeTickingNow ? bombBlowTime : previousBlowTime;
        const bool previousTimerLengthValid =
            std::isfinite(previousTimerLength) &&
            previousTimerLength >= 5.0f &&
            previousTimerLength <= 90.0f;
        const float selectedTimerLength =
            authoritativeTickingNow
                ? (timerLengthValid
                    ? bombTimerLength
                    : (previousTimerLengthValid ? previousTimerLength : 40.0f))
                : previousTimerLength;
        const bool authoritativeDefuseNow =
            esp::data::IsAuthoritativeDefuseTimer(
                plantedLiveNow,
                bombTerminalNow,
                plantedMetaFresh,
                bombBeingDefused != 0,
                c4DefuserHandleValid && resolvedDefuserActivelyDefusing,
                defuseTimeValid,
                defuseWindowValid);
        if (authoritativeDefuseNow)
            s_lastAuthoritativeDefuseUs = nowUs;
        const bool heldDefuseNow =
            !authoritativeDefuseNow &&
            esp::data::ShouldHoldTransientDefuseTimer(
                previousDefuseLive,
                plantedLiveNow,
                bombTerminalNow,
                s_lastAuthoritativeDefuseUs,
                nowUs,
                previousDefuseEndTime,
                currentGameTime);
        const bool defuseLiveNow =
            authoritativeDefuseNow || heldDefuseNow;
        if (!defuseLiveNow)
            s_lastAuthoritativeDefuseUs = 0;
        const float selectedDefuseEndTime =
            authoritativeDefuseNow
                ? bombDefuseEndTime
                : previousDefuseEndTime;
        const bool previousDefuseLengthValid =
            std::isfinite(previousDefuseLength) &&
            previousDefuseLength >= 4.0f &&
            previousDefuseLength <= 11.0f;
        const float selectedDefuseLength =
            authoritativeDefuseNow
                ? (defuseLengthValid
                    ? bombDefuseLength
                    : (previousDefuseLengthValid
                        ? previousDefuseLength
                        : kFallbackDefuseNoKitSeconds))
                : previousDefuseLength;

        s_bombState.currentGameTime = gameTimeValid ? currentGameTime : 0.0f;
        s_bombState.ticking = tickingLiveNow;
        s_bombState.beingDefused = defuseLiveNow;
        s_bombState.blowTime = tickingLiveNow ? selectedBlowTime : 0.0f;
        s_bombState.timerLength =
            tickingLiveNow ? selectedTimerLength : 0.0f;
        s_bombState.defuseEndTime =
            defuseLiveNow ? selectedDefuseEndTime : 0.0f;
        s_bombState.defuseLength =
            defuseLiveNow ? selectedDefuseLength : 0.0f;
        if (bombTerminalNow) {
            s_bombState.planted = false;
            s_bombState.ticking = false;
            s_bombState.beingDefused = false;
            s_bombState.dropped = false;
            s_bombState.velocity = {};
            s_bombState.positionSampleTimeUs = 0;
            s_bombState.blowTime = 0.0f;
            s_bombState.timerLength = 0.0f;
            s_bombState.defuseEndTime = 0.0f;
            s_bombState.defuseLength = 0.0f;
            s_lastAuthoritativeBombTimerUs = 0;
            s_lastAuthoritativeDefuseUs = 0;
        }

        if (targetBombFromCurrentEvidence &&
            isValidWorldPos(s_bombState.position) &&
            (s_bombState.planted || s_bombState.dropped)) {
            s_lastConfirmedBombState = s_bombState;
            s_lastConfirmedBombStateUs = targetBombSampleUs;
        }
        }
