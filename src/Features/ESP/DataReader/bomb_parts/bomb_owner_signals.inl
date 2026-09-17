    const bool weaponC4OwnerValid = (weaponC4OwnerHandle != 0u && weaponC4OwnerHandle != 0xFFFFFFFFu);
    if (weaponC4OwnerValid)
        weaponC4OwnerPlayerIndex = findPlayerIndexByEntityHandle(weaponC4OwnerHandle);
    if (weaponC4OwnerPlayerIndex >= 0 && !weaponC4DetachedCurrent) {
        if (s_lastObservedBombOwnerSlot != weaponC4OwnerPlayerIndex) {
            s_lastObservedBombOwnerSlot = weaponC4OwnerPlayerIndex;
            s_cachedBombAttachedOwnerSlot = weaponC4OwnerPlayerIndex;
            s_cachedBombAttachedOwnerUs = nowUs;
        }
    } else {
        s_lastObservedBombOwnerSlot = -1;
    }

    bool weaponC4OwnerLooksCarried = false;
    bool weaponC4LooksDroppedNearOwner = false;
    bool weaponC4OwnerAlive = false;
    float weaponC4OwnerDist2D = FLT_MAX;
    bool weaponC4StrongCarrySignal = false;
    bool weaponC4OwnerSelectedC4 = false;
    bool weaponC4OwnerSlowInventoryBomb = false;
    bool weaponC4OwnerNear = false;
    bool weaponC4OwnerAttachedByHandle = false;
    bool weaponC4OwnerGroundEnvelope = false;
    bool weaponC4OwnerCarrySticky = false;
    bool weaponC4OwnerAttachGrace = false;
    bool weaponC4SpatialCarryVeto = false;
    int weaponC4SpatialCarrySlot = -1;
    constexpr uint64_t kBombOwnerCarryStickyUs = 250000u;
    constexpr uint64_t kBombOwnerAttachGraceUs = esp::data::kBombOwnerAttachGraceUs;
    const bool weaponC4OwnerFreshInventoryBomb =
        freshInventoryC4CarrierEvidence &&
        weaponC4OwnerPlayerIndex >= 0 &&
        weaponC4OwnerPlayerIndex == s_freshInventoryC4CarrierSlot;
    if (weaponC4OwnerPlayerIndex >= 0) {
        weaponC4OwnerAlive =
            healths[weaponC4OwnerPlayerIndex] > 0 &&
            lifeStates[weaponC4OwnerPlayerIndex] == 0 &&
            std::isfinite(positions[weaponC4OwnerPlayerIndex].x) &&
            std::isfinite(positions[weaponC4OwnerPlayerIndex].y) &&
            std::isfinite(positions[weaponC4OwnerPlayerIndex].z);
        if (weaponC4OwnerAlive) {
            weaponC4OwnerSelectedC4 =
                (activeWeapons[weaponC4OwnerPlayerIndex] != 0 && activeWeapons[weaponC4OwnerPlayerIndex] == weaponC4Entity) ||
            (weaponIds[weaponC4OwnerPlayerIndex] == kWeaponC4Id);
            weaponC4OwnerSlowInventoryBomb = inventoryHasBombBySlot[weaponC4OwnerPlayerIndex];
            weaponC4OwnerCarrySticky =
                weaponC4OwnerPlayerIndex == s_cachedBombCarryOwnerSlot &&
                s_cachedBombCarryOwnerUs > 0 &&
                (nowUs - s_cachedBombCarryOwnerUs) <= kBombOwnerCarryStickyUs;
            if (!weaponC4PosValid) {
                weaponC4OwnerAttachedByHandle =
                    !bombDroppedByRules ||
                    weaponC4RecentOwnerAttach;
                weaponC4StrongCarrySignal =
                    weaponC4OwnerAttachedByHandle ||
                    weaponC4OwnerSelectedC4 ||
                    weaponC4OwnerSlowInventoryBomb ||
                    weaponC4RecentOwnerAttach ||
                    (!bombDroppedByRules && weaponC4OwnerCarrySticky);
            } else {
                const Vector3& ownerPos = positions[weaponC4OwnerPlayerIndex];
                const float dx = weaponC4WorldPos.x - ownerPos.x;
                const float dy = weaponC4WorldPos.y - ownerPos.y;
                const float dz = weaponC4WorldPos.z - ownerPos.z;
                weaponC4OwnerDist2D = std::sqrt(dx * dx + dy * dy);
                weaponC4OwnerNear = weaponC4OwnerDist2D <= 96.0f;
                weaponC4OwnerAttachedByHandle =
                    weaponC4RecentOwnerAttach ||
                    (!bombDroppedByRules &&
                     weaponC4OwnerDist2D <= 120.0f &&
                     std::fabs(dz) <= 96.0f);
                weaponC4OwnerGroundEnvelope = std::fabs(dz) <= 18.0f;
                weaponC4OwnerAttachGrace =
                    weaponC4OwnerPlayerIndex == s_cachedBombAttachedOwnerSlot &&
                    s_cachedBombAttachedOwnerUs > 0 &&
                    (nowUs - s_cachedBombAttachedOwnerUs) <= kBombOwnerAttachGraceUs &&
                    weaponC4OwnerNear &&
                    weaponC4OwnerGroundEnvelope;
                weaponC4StrongCarrySignal =
                    weaponC4OwnerAttachedByHandle ||
                    weaponC4OwnerSelectedC4 ||
                    weaponC4OwnerSlowInventoryBomb ||
                    weaponC4RecentOwnerAttach ||
                    (!bombDroppedByRules && weaponC4OwnerCarrySticky) ||
                    weaponC4OwnerAttachGrace;
                weaponC4LooksDroppedNearOwner =
                    weaponC4OwnerNear &&
                    weaponC4OwnerGroundEnvelope &&
                    !weaponC4StrongCarrySignal &&
                    bombDroppedByRules;
            }

            weaponC4OwnerLooksCarried = weaponC4StrongCarrySignal;
        }
    }

    
    
    

    
    
    if (weaponC4PosValid && weaponC4OwnerAlive && weaponC4OwnerDist2D > 96.0f) {
        weaponC4OwnerCarrySticky = false;
        weaponC4OwnerAttachGrace = false;
        weaponC4OwnerAttachedByHandle = false;
        weaponC4StrongCarrySignal =
            weaponC4OwnerSelectedC4 ||
            weaponC4OwnerSlowInventoryBomb ||
            weaponC4RecentOwnerAttach;
        weaponC4OwnerLooksCarried = weaponC4StrongCarrySignal;
        s_cachedBombCarryOwnerSlot = -1;
        s_cachedBombCarryOwnerUs = 0;
        s_cachedBombAttachedOwnerSlot = -1;
        s_cachedBombAttachedOwnerUs = 0;
    }

    
    
    
    if (bombDroppedByRules) {
        weaponC4OwnerCarrySticky = false;
        weaponC4OwnerAttachGrace = false;
        weaponC4OwnerAttachedByHandle = false;
        weaponC4StrongCarrySignal =
            esp::data::HasStrictCurrentCarryEvidence(
                weaponC4OwnerAlive,
                weaponC4OwnerSelectedC4,
                weaponC4OwnerFreshInventoryBomb,
                false) ||
            (weaponC4OwnerAlive && weaponC4RecentOwnerAttach);
        weaponC4OwnerLooksCarried = weaponC4StrongCarrySignal;
        s_cachedBombCarryOwnerSlot = -1;
        s_cachedBombCarryOwnerUs = 0;
        s_cachedBombAttachedOwnerSlot = -1;
        s_cachedBombAttachedOwnerUs = 0;
    }

    // m_bCanBePickedUp and recycled weapon entities can lag one transition
    // behind a real pickup. A current active-weapon, clean inventory, or
    // owner-attach sample is stronger evidence, except on the actual drop edge.
    if (esp::data::ShouldFreshCarryOverrideDetachedWeaponC4(
            weaponC4DetachedCurrent,
            weaponC4RecentDropTickEdge,
            bombDroppedByRules,
            weaponC4OwnerAlive,
            weaponC4OwnerSelectedC4,
            freshInventoryC4CarrierEvidence,
            weaponC4RecentOwnerAttach,
            weaponC4OwnerAttachedByHandle)) {
        weaponC4DetachedCurrent = false;
    }

    if (weaponC4DetachedCurrent) {
        weaponC4OwnerCarrySticky = false;
        weaponC4OwnerAttachGrace = false;
        weaponC4OwnerAttachedByHandle = false;
        weaponC4StrongCarrySignal = false;
        weaponC4OwnerLooksCarried = false;
        weaponC4OwnerSelectedC4 = false;
        weaponC4OwnerSlowInventoryBomb = false;
        inventoryC4CarrierEvidence = false;
        strictInventoryC4CarrierEvidence = false;
        inventoryC4CarrierSlot = -1;
        s_cachedBombCarryOwnerSlot = -1;
        s_cachedBombCarryOwnerUs = 0;
        s_cachedBombAttachedOwnerSlot = -1;
        s_cachedBombAttachedOwnerUs = 0;
        s_lastObservedBombOwnerSlot = -1;
        s_recentOwnerAttachEntity = 0;
        s_recentOwnerAttachUntilUs = 0;
        s_freshInventoryC4CarrierSlot = -1;
        s_freshInventoryC4Entity = 0;
        s_freshInventoryC4CarrierUs = 0;
        for (int resolvedIdx = 0;
             resolvedIdx < playerResolvedSlotCount;
             ++resolvedIdx) {
            const int i = playerResolvedSlots[resolvedIdx];
            if (i >= 0 && i < 64)
                bombCarrierBySlot[i] = false;
        }
    }

    const bool treatWeaponC4OwnerAsCarrier =
        weaponC4OwnerValid &&
        weaponC4OwnerPlayerIndex >= 0 &&
        weaponC4OwnerLooksCarried;
    const bool weaponC4CurrentCarryEvidence =
        weaponC4OwnerSelectedC4 ||
        weaponC4OwnerSlowInventoryBomb ||
        weaponC4RecentOwnerAttach ||
        weaponC4OwnerAttachGrace;
    if (treatWeaponC4OwnerAsCarrier && weaponC4CurrentCarryEvidence) {
        s_cachedBombCarryOwnerSlot = weaponC4OwnerPlayerIndex;
        s_cachedBombCarryOwnerUs = nowUs;
    } else if (esp::data::ShouldExpireCachedBombCarryOwner(
                   s_cachedBombCarryOwnerUs,
                   nowUs)) {
        s_cachedBombCarryOwnerSlot = -1;
        s_cachedBombCarryOwnerUs = 0;
    }
    if (treatWeaponC4OwnerAsCarrier)
        bombCarrierBySlot[weaponC4OwnerPlayerIndex] = true;

    bool anyBombCarrierNow = false;
    for (int resolvedIdx = 0; resolvedIdx < playerResolvedSlotCount; ++resolvedIdx) {
        const int i = playerResolvedSlots[resolvedIdx];
        if (bombCarrierBySlot[i]) {
            anyBombCarrierNow = true;
            break;
        }
    }
    constexpr int kBombCarrierTeamT = 2;
    auto isAliveBombCarryCandidate = [&](int idx) -> bool {
        if (idx < 0 || idx >= 64)
            return false;
        if (healths[idx] <= 0 || lifeStates[idx] != 0 || teams[idx] != kBombCarrierTeamT)
            return false;
        if (!std::isfinite(positions[idx].x) ||
            !std::isfinite(positions[idx].y) ||
            !std::isfinite(positions[idx].z)) {
            return false;
        }
        return
            bombCarrierBySlot[idx] ||
            inventoryHasBombBySlot[idx] ||
            weaponIds[idx] == kWeaponC4Id ||
            idx == weaponC4OwnerPlayerIndex ||
            (idx == s_cachedBombCarryOwnerSlot &&
             s_cachedBombCarryOwnerUs > 0 &&
             (nowUs - s_cachedBombCarryOwnerUs) <= kBombOwnerCarryStickyUs) ||
            (idx == s_cachedBombAttachedOwnerSlot &&
             s_cachedBombAttachedOwnerUs > 0 &&
             (nowUs - s_cachedBombAttachedOwnerUs) <= kBombOwnerAttachGraceUs);
    };
    auto bombCarryCandidateDistance2DSq = [&](const Vector3& bombPos, int idx) -> float {
        if (!std::isfinite(bombPos.x) ||
            !std::isfinite(bombPos.y) ||
            !std::isfinite(bombPos.z) ||
            !isAliveBombCarryCandidate(idx)) {
            return FLT_MAX;
        }
        const float dx = bombPos.x - positions[idx].x;
        const float dy = bombPos.y - positions[idx].y;
        const float dz = std::fabs(bombPos.z - positions[idx].z);
        const float distance2DSq = dx * dx + dy * dy;
        return distance2DSq <= (32.0f * 32.0f) && dz <= 64.0f
            ? distance2DSq
            : FLT_MAX;
    };

    
    
    
    
    
    
    
    
    if (weaponC4PosValid && !bombDroppedByRules) {
        int bombPositionCarrierSlot = -1;
        float bestDist2DSq = FLT_MAX;
        for (int resolvedIdx = 0; resolvedIdx < playerResolvedSlotCount; ++resolvedIdx) {
            const int i = playerResolvedSlots[resolvedIdx];
            const float dist2DSq = bombCarryCandidateDistance2DSq(weaponC4WorldPos, i);
            if (dist2DSq < bestDist2DSq) {
                bestDist2DSq = dist2DSq;
                bombPositionCarrierSlot = i;
            }
        }
        if (bombPositionCarrierSlot >= 0) {
            weaponC4SpatialCarryVeto = true;
            weaponC4SpatialCarrySlot = bombPositionCarrierSlot;
            weaponC4LooksDroppedNearOwner = false;
        }
    }
