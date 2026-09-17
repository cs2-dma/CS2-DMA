        auto isTrackedBombWorldEntitySlot = [&](int idx) {
            return s_worldEntityItemIds[idx] == kWeaponC4Id;
        };
        auto isTrackedDroppedWorldEntitySlot = [&](int idx) {
            const uint16_t itemId = s_worldEntityItemIds[idx];
            return itemId != 0 &&
                   itemId != kWeaponC4Id &&
                   WeaponNameFromItemId(itemId) != nullptr &&
                   !isUtilityWorldItemId(itemId) &&
                   !IsKnifeItemId(itemId) &&
                   s_worldEntityClassKinds[idx] ==
                       static_cast<uint8_t>(esp::data::WorldEntityClass::DroppedWeapon);
        };
        auto isTrackedUtilityWorldEntitySlot = [&](int idx) {
            const uint16_t itemId = s_worldEntityItemIds[idx];
            const uint32_t subclassId = s_worldEntitySubclassIds[idx];
            return isUtilityWorldItemId(itemId) ||
                   esp::data::IsWorldUtilityClass(
                       static_cast<esp::data::WorldEntityClass>(
                           s_worldEntityClassKinds[idx])) ||
                   hasTrackedSubclass(s_worldSmokeSubclassIds, subclassId) ||
                   hasTrackedSubclass(s_worldDecoySubclassIds, subclassId) ||
                   hasTrackedSubclass(s_worldHeSubclassIds, subclassId) ||
                   hasTrackedSubclass(s_worldInfernoSubclassIds, subclassId) ||
                   hasTrackedSubclass(s_worldMolotovSubclassIds, subclassId) ||
                   hasTrackedWorldUtilityState(idx);
        };
        auto shouldTrackWorldSlotForDueDomains = [&](int idx) {
            if (!isTrackedWorldEntitySlot(idx))
                return false;
            if (worldDomainSlowDiscoveryDue)
                return true;
            if (worldDomainBombRescueDue && isTrackedBombWorldEntitySlot(idx))
                return true;
            if (worldDomainDroppedItemsDue && isTrackedDroppedWorldEntitySlot(idx))
                return true;
            if (worldDomainActiveUtilityDue && isTrackedUtilityWorldEntitySlot(idx))
                return true;
            return false;
        };

        if (worldDomainBombRescueDue && s_worldBombCandidateSlotCount != 0u) {
            for (int bombSlotPos = 0; bombSlotPos < static_cast<int>(s_worldBombCandidateSlotCount); ++bombSlotPos)
                pushWorldCandidate(s_worldBombCandidateSlots[bombSlotPos]);
        }
        const bool discoveryCandidatesDue =
            worldDomainSlowDiscoveryDue ||
            ((worldDomainDroppedItemsDue || worldDomainActiveUtilityDue) &&
             s_worldTrackedIndexCount == 0);
        const bool fullWorldDiscovery = discoveryCandidatesDue && (worldDiscoveryShardCount <= 1u);
        for (int trackedIdxPos = 0; trackedIdxPos < s_worldTrackedIndexCount; ++trackedIdxPos) {
            const int trackedIdx = s_worldTrackedIndices[trackedIdxPos];
            if (shouldTrackWorldSlotForDueDomains(trackedIdx))
                pushWorldCandidate(trackedIdx);
        }
        if (discoveryCandidatesDue) {
            if (fullWorldDiscovery) {
                for (int idx = esp::data::kFirstWorldEntitySlot; idx <= worldLimit; ++idx)
                    pushWorldCandidate(idx);
            } else {
                for (int idx = esp::data::kFirstWorldEntitySlot + static_cast<int>(activeWorldDiscoveryShard); idx <= worldLimit; idx += static_cast<int>(worldDiscoveryShardCount))
                    pushWorldCandidate(idx);
            }
        }
