    // weaponDynamic already resolves m_nodeToWorld and validates it. Reuse that
    // live transform for the matching world candidate instead of issuing the
    // same direct reads a second time.
    if (bombDroppedByRules &&
        worldScanFoundC4 &&
        worldScanC4Entity &&
        worldScanC4Entity == weaponC4Entity &&
        weaponC4PosValid) {
        worldScanC4Pos = weaponC4WorldPos;
    }

    bool bombEntityTainted =
        s_explodedEntityTaintPtr != 0 &&
        plantedC4Entity != 0 &&
        plantedC4Entity == s_explodedEntityTaintPtr &&
        s_explodedEntityTaintUntilUs > nowUs;
    const bool bombBlowTimeFresh =
        std::isfinite(bombBlowTime) &&
        std::isfinite(currentGameTime) &&
        bombBlowTime > currentGameTime + 0.05f &&
        bombBlowTime < currentGameTime + 120.0f;
    const bool bombTerminalSignal = esp::data::IsPlantedC4Terminal(
        bombHasExploded != 0,
        bombDefused != 0);
    const bool strongFreshPlantSignal =
        bombPlantedByRules &&
        plantedMetaFresh &&
        !bombTerminalSignal &&
        (bombActivated != 0 || bombTicking != 0) &&
        bombBlowTimeFresh;
    if (bombEntityTainted && strongFreshPlantSignal) {
        s_explodedEntityTaintPtr = 0;
        s_explodedEntityTaintUntilUs = 0;
        bombEntityTainted = false;
    }
    if (bombTerminalSignal && plantedC4Entity) {
        s_explodedEntityTaintPtr = plantedC4Entity;
        s_explodedEntityTaintUntilUs = nowUs + esp::data::kStalePlantedC4TaintUs;
    }
    const bool plantedBombFromEntity = esp::data::IsLivePlantedC4(
        bombPlantedByRules,
        plantedC4Entity != 0,
        plantedMetaFresh,
        bombEntityTainted,
        bombTerminalSignal,
        bombActivated != 0,
        bombTicking != 0,
        bombBeingDefused != 0,
        bombBlowTimeFresh);
    const bool bombPlantedNow = plantedBombFromEntity;
    const bool bombPlantTransitionPending =
        bombPlantedByRules &&
        !bombTerminalSignal;
    Vector3 droppedBombPos = { NAN, NAN, NAN };
    Vector3 droppedBombBoundsMins = {};
    Vector3 droppedBombBoundsMaxs = {};
    bool droppedBombPosValid = false;
    bool droppedBombBoundsValid = false;
    int droppedBombScore = (std::numeric_limits<int>::min)();
    uint32_t droppedBombSourceFlags = 0;

    
    
    
    
    
    auto bombPositionInsideAlivePlayer = [&](const Vector3& bombPos) -> bool {
        if (bombDroppedByRules || weaponC4DetachedCurrent)
            return false;
        for (int resolvedIdx = 0; resolvedIdx < playerResolvedSlotCount; ++resolvedIdx) {
            const int i = playerResolvedSlots[resolvedIdx];
            if (bombCarryCandidateDistance2DSq(bombPos, i) < FLT_MAX)
                return true;
        }
        return false;
    };

    
    
    
    
    
    constexpr int kDroppedC4AcceptScore = 80;

    const bool broadRecentWeaponC4CarryEvidence =
        anyBombCarrierNow ||
        treatWeaponC4OwnerAsCarrier ||
        weaponC4StrongCarrySignal ||
        weaponC4SpatialCarryVeto ||
        (s_cachedBombCarryOwnerSlot >= 0 &&
         s_cachedBombCarryOwnerUs > 0 &&
         (nowUs - s_cachedBombCarryOwnerUs) <= kBombOwnerCarryStickyUs) ||
        (s_cachedBombAttachedOwnerSlot >= 0 &&
         s_cachedBombAttachedOwnerUs > 0 &&
         (nowUs - s_cachedBombAttachedOwnerUs) <= kBombOwnerAttachGraceUs);
    const bool strictCurrentCarryEvidence =
        !weaponC4DetachedCurrent &&
         (esp::data::HasStrictCurrentCarryEvidence(
             weaponC4OwnerAlive,
             weaponC4OwnerSelectedC4,
             weaponC4OwnerFreshInventoryBomb,
             strictInventoryC4CarrierEvidence) ||
         (weaponC4OwnerAlive && weaponC4RecentOwnerAttach));
    const bool recentWeaponC4CarryEvidence =
        esp::data::HasRulesCompatibleCarryEvidence(
            bombDroppedByRules,
            strictCurrentCarryEvidence,
            broadRecentWeaponC4CarryEvidence);
    const bool worldSelectedWeaponDetached =
        weaponC4DetachedCurrent &&
        worldScanC4Entity != 0 &&
        worldScanC4Entity == weaponC4Entity;
    const bool worldScanC4DetachedFromLiveOwner =
        worldSelectedWeaponDetached ||
        esp::data::IsDetachedWorldC4Candidate(
            bombDroppedByRules,
            worldScanC4NoOwner,
            worldScanC4OwnerIdx,
            worldScanC4OwnerAlive);
    const bool worldC4DropEvidence =
        worldScanFoundC4 &&
        (bombDroppedByRules ? isDroppedC4PositionPlausible(worldScanC4Pos)
                            : isDroppedC4WeakPositionPlausible(worldScanC4Pos)) &&
        worldScanC4Score >= kDroppedC4AcceptScore &&
        worldScanC4DetachedFromLiveOwner;
    const bool weakWeaponDropAllowed =
        weaponC4DetachedCurrent ||
        bombDroppedByRules ||
        (worldC4DropEvidence &&
         !recentWeaponC4CarryEvidence &&
         !bombPositionInsideAlivePlayer(worldScanC4Pos));

    // Weapon path:
    //  - rules-dropped: allowed as LOW-score seed (world overrides when found).
    //    Fully disabling it caused Hidden with raw dropped bit set (status
    //    raw=0x2002, source=0, confidence=0) when world scan missed C4.
    //  - otherwise: previous unowned / distance heuristics.
    const bool weaponC4TrulyUnowned = !weaponC4OwnerValid;
    const bool weaponC4DroppedByRulesNow =
        weaponC4PositionFresh &&
        bombDroppedByRules &&
        !weaponC4StrongCarrySignal &&
        isDroppedC4PositionPlausible(weaponC4WorldPos);
    const bool weaponC4DroppedByDetachedSignal =
        weaponC4PositionFresh &&
        weaponC4DetachedCurrent &&
        isDroppedC4WeakPositionPlausible(weaponC4WorldPos);
    const bool weaponC4DroppedByOwnerLoss =
        weaponC4PosValid &&
        weakWeaponDropAllowed &&
        !bombDroppedByRules &&
        weaponC4TrulyUnowned;
    const bool weaponC4DroppedByDistance =
        weaponC4PosValid &&
        weakWeaponDropAllowed &&
        weaponC4OwnerAlive &&
        weaponC4OwnerDist2D > 160.0f &&
        !bombDroppedByRules;
    const bool weaponC4LooksDropped =
        !bombPlantedNow &&
        !bombPlantTransitionPending &&
        (weaponC4DroppedByDetachedSignal ||
         weaponC4DroppedByRulesNow ||
         (isDroppedC4WeakPositionPlausible(weaponC4WorldPos) &&
          !weaponC4StrongCarrySignal &&
          (weaponC4DroppedByOwnerLoss || weaponC4DroppedByDistance)));
    const bool weaponC4DefinitelyNotCarried =
        !anyBombCarrierNow &&
        !treatWeaponC4OwnerAsCarrier &&
        (weaponC4DroppedByDetachedSignal ||
         weaponC4DroppedByRulesNow ||
         weaponC4DroppedByOwnerLoss ||
         weaponC4DroppedByDistance);
    if (!bombPlantedNow &&
        !bombPlantTransitionPending &&
        weaponC4PosValid &&
        (weaponC4LooksDropped || weaponC4DefinitelyNotCarried) &&
        !bombPositionInsideAlivePlayer(weaponC4WorldPos)) {
        droppedBombPos = weaponC4WorldPos;
        droppedBombPosValid = true;
        droppedBombBoundsMins = weaponC4CollisionMins;
        droppedBombBoundsMaxs = weaponC4CollisionMaxs;
        droppedBombBoundsValid = isValidBombBounds(droppedBombBoundsMins, droppedBombBoundsMaxs);
        // The direct weapon pick-up state is authoritative. A drop-tick edge is
        // a short fallback only when that schema field is unavailable.
        droppedBombScore =
            weaponC4CanBePickedUpKnown && weaponC4CanBePickedUp != 0u
                ? 255
                : (weaponC4RecentDropTickEdge
                       ? 220
                       : (bombDroppedByRules ? 90 : 140));
        droppedBombSourceFlags = BombResolveSourceWeaponEntity;
        if (weaponC4CanBePickedUpKnown && weaponC4CanBePickedUp != 0u)
            droppedBombSourceFlags |= BombResolveSourcePickable;
        else if (weaponC4RecentDropTickEdge)
            droppedBombSourceFlags |= BombResolveSourceDropTickEdge;
        if (bombDroppedByRules)
            droppedBombSourceFlags |= BombResolveSourceRules;
    }

    bool worldC4PositionFromCache = false;
    const uint64_t worldC4StickyUs =
        esp::data::SelectWorldC4StickyUs(bombDroppedByRules);
    const bool worldC4MatchesWeapon =
        weaponC4Entity != 0 &&
        worldScanC4Entity != 0 &&
        worldScanC4Entity == weaponC4Entity;
    const bool preferDetachedWorldC4 =
        bombDroppedByRules && worldScanC4DetachedFromLiveOwner;
    const bool worldC4CompatibleWithWeapon =
        esp::data::ShouldWorldC4ReplaceWeaponPosition(
            weaponC4PositionFresh,
            worldC4MatchesWeapon,
            false,
            preferDetachedWorldC4);
    if (worldScanFoundC4 &&
        worldC4CompatibleWithWeapon &&
        (bombDroppedByRules ? isDroppedC4PositionPlausible(worldScanC4Pos)
                            : isDroppedC4WeakPositionPlausible(worldScanC4Pos)) &&
        worldScanC4Score >= kDroppedC4AcceptScore &&
        worldScanC4DetachedFromLiveOwner) {
        s_lastWorldC4Pos = worldScanC4Pos;
        s_lastWorldC4PosUs = nowUs;
        s_lastWorldC4Entity = worldScanC4Entity;
        s_lastWorldC4NoOwner = worldScanC4NoOwner;
        s_lastWorldC4OwnerIdx = worldScanC4OwnerIdx;
        s_lastWorldC4OwnerAlive = worldScanC4OwnerAlive;
        s_lastWorldC4OwnerNearby = worldScanC4OwnerNearby;
    } else if (s_lastWorldC4PosUs > 0 &&
               (nowUs - s_lastWorldC4PosUs) <= worldC4StickyUs &&
               s_lastWorldC4Entity != 0 &&
               esp::data::ShouldWorldC4ReplaceWeaponPosition(
                   weaponC4PositionFresh,
                   weaponC4Entity != 0 && s_lastWorldC4Entity == weaponC4Entity,
                   false,
                   bombDroppedByRules &&
                       esp::data::IsDetachedWorldC4Candidate(
                           bombDroppedByRules,
                           s_lastWorldC4NoOwner,
                           s_lastWorldC4OwnerIdx,
                           s_lastWorldC4OwnerAlive)) &&
               (bombDroppedByRules ? isDroppedC4PositionPlausible(s_lastWorldC4Pos)
                                   : isDroppedC4WeakPositionPlausible(s_lastWorldC4Pos))) {
        worldScanFoundC4 = true;
        worldC4PositionFromCache = true;
        worldScanC4Entity = s_lastWorldC4Entity;
        worldScanC4Pos = s_lastWorldC4Pos;
        worldScanC4NoOwner = s_lastWorldC4NoOwner;
        worldScanC4OwnerIdx = s_lastWorldC4OwnerIdx;
        worldScanC4OwnerAlive = s_lastWorldC4OwnerAlive;
        worldScanC4OwnerNearby = s_lastWorldC4OwnerNearby;
        worldScanC4Score = kDroppedC4AcceptScore;
    }

    auto scoreDroppedC4Candidate = [&](bool noOwner,
                                       int ownerIdx,
                                       bool ownerAlive,
                                       bool ownerNearby,
                                       bool ownerCarrySignal,
                                       const Vector3& pos) -> int {
        int score = 0;
        if (bombDroppedByRules ? isDroppedC4PositionPlausible(pos)
                               : isDroppedC4WeakPositionPlausible(pos))
            score += 40;
        if (bombDroppedByRules)
            score += 180;
        if (noOwner)
            score += 220;
        if (ownerIdx < 0)
            score += 120;
        if (!ownerAlive)
            score += 100;
        if (!ownerNearby)
            score += 80;
        if (ownerCarrySignal)
            score -= bombDroppedByRules ? 80 : 220;
        if (!bombDroppedByRules && ownerAlive && ownerNearby)
            score -= 120;
        if (s_bombState.dropped && IsFiniteVec(s_bombState.position)) {
            const float dx = pos.x - s_bombState.position.x;
            const float dy = pos.y - s_bombState.position.y;
            if ((dx * dx + dy * dy) <= (160.0f * 160.0f))
                score += 45;
        }
        if (s_lastDroppedBombPosUs > 0 && isValidWorldPos(s_lastDroppedBombPos)) {
            const float dx = pos.x - s_lastDroppedBombPos.x;
            const float dy = pos.y - s_lastDroppedBombPos.y;
            if ((dx * dx + dy * dy) <= (160.0f * 160.0f))
                score += 40;
        }
        return score;
    };

    if (worldScanFoundC4) {
        bool worldOwnerCarrySignal = false;
        int worldCandidateScore = (std::numeric_limits<int>::min)();
        if (!bombDroppedByRules &&
            worldScanC4OwnerAlive &&
            worldScanC4OwnerNearby &&
            worldScanC4OwnerIdx >= 0 &&
            worldScanC4OwnerIdx < 64) {
            worldOwnerCarrySignal =
                (activeWeapons[worldScanC4OwnerIdx] != 0 && activeWeapons[worldScanC4OwnerIdx] == worldScanC4Entity) ||
                (weaponIds[worldScanC4OwnerIdx] == kWeaponC4Id) ||
                inventoryHasBombBySlot[worldScanC4OwnerIdx] ||
                (worldScanC4OwnerAlive && worldScanC4OwnerNearby) ||
                (worldScanC4OwnerIdx == s_cachedBombCarryOwnerSlot &&
                 s_cachedBombCarryOwnerUs > 0 &&
                 (nowUs - s_cachedBombCarryOwnerUs) <= kBombOwnerCarryStickyUs) ||
                (worldScanC4OwnerIdx == s_cachedBombAttachedOwnerSlot &&
                 s_cachedBombAttachedOwnerUs > 0 &&
                 (nowUs - s_cachedBombAttachedOwnerUs) <= kBombOwnerAttachGraceUs);
        }
        if (!bombDroppedByRules &&
            worldScanC4OwnerAlive &&
            worldScanC4OwnerNearby &&
            worldScanC4OwnerIdx >= 0 &&
            worldScanC4OwnerIdx < 64 &&
            worldOwnerCarrySignal) {
            bombCarrierBySlot[worldScanC4OwnerIdx] = true;
        }
        if (!bombPlantedNow && !bombPlantTransitionPending) {
            worldCandidateScore = scoreDroppedC4Candidate(
                worldScanC4NoOwner,
                worldScanC4OwnerIdx,
                worldScanC4OwnerAlive,
                worldScanC4OwnerNearby,
                worldOwnerCarrySignal,
                worldScanC4Pos);
            // Rules-dropped: accept any plausible world C4 even if score is
            // modest (owner still nearby for a few hundred ms after drop).
            const bool c4LooksDropped =
                (bombDroppedByRules
                     ? (worldCandidateScore >= 40 || worldScanC4NoOwner)
                     : (worldCandidateScore >= kDroppedC4AcceptScore)) &&
                (bombDroppedByRules ||
                 worldScanC4NoOwner ||
                 worldScanC4OwnerIdx < 0 ||
                 !worldScanC4OwnerAlive);
            float weaponWorldDist2D = FLT_MAX;
            if (droppedBombPosValid && isValidWorldPos(droppedBombPos) && isValidWorldPos(worldScanC4Pos)) {
                const float dx = droppedBombPos.x - worldScanC4Pos.x;
                const float dy = droppedBombPos.y - worldScanC4Pos.y;
                weaponWorldDist2D = std::sqrt(dx * dx + dy * dy);
            }
            const bool worldNoOwnerPreferred =
                bombDroppedByRules &&
                worldScanC4NoOwner &&
                isValidWorldPos(worldScanC4Pos) &&
                isDroppedC4PositionPlausible(worldScanC4Pos);
            const bool preferWorldOverWeapon =
                worldNoOwnerPreferred ||
                esp::data::ShouldPreferDetachedWorldC4Position(
                    bombDroppedByRules,
                    isValidWorldPos(worldScanC4Pos),
                    worldScanC4DetachedFromLiveOwner &&
                        (bombDroppedByRules ? isDroppedC4PositionPlausible(worldScanC4Pos)
                                            : isDroppedC4WeakPositionPlausible(worldScanC4Pos)),
                    droppedBombPosValid && isValidWorldPos(droppedBombPos),
                    weaponWorldDist2D);
            if (c4LooksDropped &&
                (worldCandidateScore > droppedBombScore || preferWorldOverWeapon) &&
                (bombDroppedByRules ? isDroppedC4PositionPlausible(worldScanC4Pos)
                                    : isDroppedC4WeakPositionPlausible(worldScanC4Pos)) &&
                !bombPositionInsideAlivePlayer(worldScanC4Pos)) {
                droppedBombPos = worldScanC4Pos;
                droppedBombPosValid = true;
                droppedBombBoundsMins = {};
                droppedBombBoundsMaxs = {};
                droppedBombBoundsValid = false;
                droppedBombScore = (std::max)(worldCandidateScore, droppedBombScore + 1);
                droppedBombSourceFlags = BombResolveSourceWorldC4;
                if (worldC4PositionFromCache)
                    droppedBombSourceFlags |= BombResolveSourcePositionFallback;
                if (bombDroppedByRules)
                    droppedBombSourceFlags |= BombResolveSourceRules;
            }
        }
    }

    const uint64_t droppedStickyUs =
        esp::data::SelectDroppedBombStickyUs(
            bombDroppedByRules,
            esp::intervals::kBombStickyDroppedUs);
    const uint64_t confirmedDroppedStickyUs =
        esp::data::SelectConfirmedDroppedStickyUs(bombDroppedByRules);
    BombResolveResult bombResolve = {};
    if (bombPlantedNow) {
        bombResolve.kind = BombResolveKind::Planted;
        bombResolve.sourceFlags = BombResolveSourceRules | BombResolveSourceWeaponEntity;
        bombResolve.confidence = 255;
        bombResolve.position = bombWorldPos;
        bombResolve.positionSampleTimeUs = nowUs;
        bombResolve.boundsMins = bombCollisionMins;
        bombResolve.boundsMaxs = bombCollisionMaxs;
        bombResolve.boundsValid = isValidBombBounds(bombCollisionMins, bombCollisionMaxs);
    } else if (bombPlantTransitionPending) {
        const bool usePositionFallback =
            esp::data::ShouldUsePlantedC4PositionFallback(
                true,
                bombEpochJustWiped,
                s_lastVisibleBombPosUs,
                nowUs) &&
            isValidWorldPos(s_lastVisibleBombPos);
        bombResolve.kind = BombResolveKind::Planted;
        bombResolve.sourceFlags = BombResolveSourceRules;
        bombResolve.confidence = usePositionFallback ? 190 : 150;
        if (usePositionFallback) {
            bombResolve.sourceFlags |= BombResolveSourcePositionFallback;
            bombResolve.position = s_lastVisibleBombPos;
            bombResolve.positionSampleTimeUs = s_lastVisibleBombPosUs;
            bombResolve.boundsMins = s_lastVisibleBombBoundsMins;
            bombResolve.boundsMaxs = s_lastVisibleBombBoundsMaxs;
            bombResolve.boundsValid = s_lastVisibleBombBoundsValid;
        }
    } else {
        
        
        
        const bool broadCarryEvidence =
            treatWeaponC4OwnerAsCarrier ||
            weaponC4StrongCarrySignal ||
            anyBombCarrierNow;
        const bool carryResolved =
            esp::data::HasRulesCompatibleCarryEvidence(
                bombDroppedByRules,
                strictCurrentCarryEvidence,
                broadCarryEvidence);
        const bool strongCarryOverrideEvidence =
            !weaponC4DetachedCurrent &&
            ((weaponC4OwnerAlive &&
              (weaponC4OwnerSelectedC4 ||
               weaponC4RecentOwnerAttach)) ||
             freshInventoryC4CarrierEvidence);
        const bool carryOverridesDropped =
            carryResolved &&
            esp::data::ShouldCarryOverrideDroppedBomb(
                droppedBombPosValid && isValidWorldPos(droppedBombPos),
                droppedBombScore,
                bombDroppedByRules,
                strongCarryOverrideEvidence,
                strictCurrentCarryEvidence ||
                    weaponC4OwnerCarrySticky ||
                    weaponC4OwnerAttachGrace);
        const uint32_t carrySourceFlags =
            (treatWeaponC4OwnerAsCarrier ||
             weaponC4StrongCarrySignal ||
             strictCurrentCarryEvidence
                 ? BombResolveSourceCarrySignal
                 : 0u) |
            (weaponC4OwnerCarrySticky ? BombResolveSourceCarrySticky : 0u) |
            (weaponC4OwnerAttachGrace ? BombResolveSourceAttachGrace : 0u);

        if (carryOverridesDropped) {
            bombResolve.kind = BombResolveKind::Carried;
            bombResolve.sourceFlags = carrySourceFlags;
            bombResolve.confidence = 220;
        } else if (droppedBombPosValid && isValidWorldPos(droppedBombPos)) {
            const bool droppedConfirmed = droppedBombScore >= 120;
            bombResolve.kind = droppedConfirmed ? BombResolveKind::DroppedConfirmed : BombResolveKind::DroppedProbable;
            bombResolve.sourceFlags = droppedBombSourceFlags;
            bombResolve.confidence = static_cast<uint8_t>(std::clamp(droppedBombScore, 80, 255));
            bombResolve.position = droppedBombPos;
            // Never extrapolate dropped C4 from weapon velocity; that field
            // often still carries the previous owner's motion and flings ESP
            // to an "unreal" location.
            bombResolve.velocity = {};
            if ((droppedBombSourceFlags & BombResolveSourceWeaponEntity) != 0u)
                bombResolve.positionSampleTimeUs = s_cachedWeaponPositionUs;
            else
                bombResolve.positionSampleTimeUs = s_lastWorldC4PosUs;
            bombResolve.boundsMins = droppedBombBoundsMins;
            bombResolve.boundsMaxs = droppedBombBoundsMaxs;
            bombResolve.boundsValid =
                droppedBombBoundsValid &&
                isValidBombBounds(droppedBombBoundsMins, droppedBombBoundsMaxs);
        } else if (carryResolved) {
            bombResolve.kind = BombResolveKind::Carried;
            bombResolve.sourceFlags = carrySourceFlags;
            bombResolve.confidence = 200;
        } else if (bombDroppedByRules) {
            // GameRules establishes STATE, not coordinates. A missing position
            // must not silently turn a known dropped bomb into Hidden. The
            // publication stage may use only its bounded same-drop cache.
            bombResolve.kind = BombResolveKind::DroppedConfirmed;
            bombResolve.sourceFlags = BombResolveSourceRules;
            bombResolve.confidence = 150;
        } else if (!recentWeaponC4CarryEvidence &&
                   s_lastDroppedBombPosUs > 0 &&
                   (nowUs - s_lastDroppedBombPosUs) <= droppedStickyUs &&
                   isValidWorldPos(s_lastDroppedBombPos)) {
            bombResolve.kind = BombResolveKind::DroppedProbable;
            bombResolve.sourceFlags = BombResolveSourceStickyDrop;
            bombResolve.confidence = 96;
            bombResolve.position = s_lastDroppedBombPos;
            bombResolve.positionSampleTimeUs = s_lastDroppedBombPosUs;
        } else if (!recentWeaponC4CarryEvidence &&
                   s_lastConfirmedBombStateUs > 0 &&
                   (nowUs - s_lastConfirmedBombStateUs) <= confirmedDroppedStickyUs &&
                   isValidWorldPos(s_lastConfirmedBombState.position) &&
                   s_lastConfirmedBombState.dropped) {
            bombResolve.kind = BombResolveKind::DroppedProbable;
            bombResolve.sourceFlags = BombResolveSourceStickyState;
            bombResolve.confidence = std::max<uint8_t>(s_lastConfirmedBombState.confidence, static_cast<uint8_t>(88));
            bombResolve.position = s_lastConfirmedBombState.position;
            bombResolve.velocity = s_lastConfirmedBombState.velocity;
            bombResolve.positionSampleTimeUs = s_lastConfirmedBombState.positionSampleTimeUs;
            bombResolve.boundsMins = s_lastConfirmedBombState.boundsMins;
            bombResolve.boundsMaxs = s_lastConfirmedBombState.boundsMaxs;
            bombResolve.boundsValid = s_lastConfirmedBombState.boundsValid;
        }
    }

    if (narrowDebug.Enabled(esp::diagnostics::kNarrowDebugC4)) {
        static bool s_prevBombPlantedNow = false;
        static bool s_prevDroppedBombPosValid = false;
        static bool s_prevAnyBombCarrierNow = false;
        static bool s_prevWorldScanFoundC4 = false;
        const bool emitC4Debug =
            (bombPlantedNow != s_prevBombPlantedNow) ||
            (droppedBombPosValid != s_prevDroppedBombPosValid) ||
            (anyBombCarrierNow != s_prevAnyBombCarrierNow) ||
            (worldScanFoundC4 != s_prevWorldScanFoundC4);
        if (emitC4Debug) {
            int carrierCount = 0;
            int inventoryBombCount = 0;
            int carrierSlot = -1;
            for (int resolvedIdx = 0; resolvedIdx < playerResolvedSlotCount; ++resolvedIdx) {
                const int i = playerResolvedSlots[resolvedIdx];
                if (bombCarrierBySlot[i]) {
                    ++carrierCount;
                    if (carrierSlot < 0)
                        carrierSlot = i;
                }
                if (inventoryHasBombBySlot[i])
                    ++inventoryBombCount;
            }
            DmaLogPrintf(
                "[DEBUG] C4: rules(drop/planted)=%d/%d plantedNow=%d carriers=%d invSlots=%d ownerValid=%d ownerIdx=%d ownerCarry=%d ownerInv=%d ownerAttachGrace=%d dropNearOwner=%d droppedValid=%d weaponPosValid=%d plantedPosValid=%d worldFallback=%d dropPos=(%.1f,%.1f,%.1f) weaponPos=(%.1f,%.1f,%.1f) plantPos=(%.1f,%.1f,%.1f)",
                bombDroppedByRules ? 1 : 0,
                bombPlantedByRules ? 1 : 0,
                bombPlantedNow ? 1 : 0,
                carrierCount,
                inventoryBombCount,
                weaponC4OwnerValid ? 1 : 0,
                weaponC4OwnerPlayerIndex,
                treatWeaponC4OwnerAsCarrier ? 1 : 0,
                weaponC4OwnerSlowInventoryBomb ? 1 : 0,
                weaponC4OwnerAttachGrace ? 1 : 0,
                weaponC4LooksDroppedNearOwner ? 1 : 0,
                droppedBombPosValid ? 1 : 0,
                weaponC4PosValid ? 1 : 0,
                isValidWorldPos(bombWorldPos) ? 1 : 0,
                worldScanFoundC4 ? 1 : 0,
                droppedBombPos.x,
                droppedBombPos.y,
                droppedBombPos.z,
                weaponC4WorldPos.x,
                weaponC4WorldPos.y,
                weaponC4WorldPos.z,
                bombWorldPos.x,
                bombWorldPos.y,
                bombWorldPos.z);
            if (carrierSlot >= 0) {
                DmaLogPrintf(
                    "[DEBUG] C4 carrier: slot=%d pawn=0x%llX pos=(%.1f,%.1f,%.1f) health=%d life=%u invHasBomb=%d",
                    carrierSlot,
                    static_cast<unsigned long long>(pawns[carrierSlot]),
                    positions[carrierSlot].x,
                    positions[carrierSlot].y,
                    positions[carrierSlot].z,
                    healths[carrierSlot],
                    static_cast<unsigned>(lifeStates[carrierSlot]),
                    inventoryHasBombBySlot[carrierSlot] ? 1 : 0);
            }
        }
        s_prevBombPlantedNow = bombPlantedNow;
        s_prevDroppedBombPosValid = droppedBombPosValid;
        s_prevAnyBombCarrierNow = anyBombCarrierNow;
        s_prevWorldScanFoundC4 = worldScanFoundC4;
    }
