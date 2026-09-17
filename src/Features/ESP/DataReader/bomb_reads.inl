    uint8_t weaponC4ResolutionMode = 0;
    uint32_t weaponC4ListOrdinal = 0;

    #include "bomb_parts/bomb_cache_reset.inl"

    auto isValidBombBounds = [](const Vector3& mins, const Vector3& maxs) -> bool {
        if (!std::isfinite(mins.x) || !std::isfinite(mins.y) || !std::isfinite(mins.z) ||
            !std::isfinite(maxs.x) || !std::isfinite(maxs.y) || !std::isfinite(maxs.z))
            return false;

        const float spanX = maxs.x - mins.x;
        const float spanY = maxs.y - mins.y;
        const float spanZ = maxs.z - mins.z;
        
        
        return spanX > 0.1f && spanY > 0.1f && spanZ > 0.1f &&
               spanX < 40.0f && spanY < 40.0f && spanZ < 40.0f;
    };

    auto tryResolveSceneNode = [&](uintptr_t ent, uintptr_t* outSceneNode) -> bool {
        if (!ent || ofs.C_BaseEntity_m_pGameSceneNode <= 0)
            return false;
        uintptr_t rawSceneNode = 0;
        if (!readValue(ent + ofs.C_BaseEntity_m_pGameSceneNode, &rawSceneNode, sizeof(rawSceneNode)))
            return false;
        rawSceneNode = sanitizePointer(rawSceneNode);
        if (!rawSceneNode)
            return false;
        if (outSceneNode)
            *outSceneNode = rawSceneNode;
        return true;
    };

    int bombPositionPlayerSlots[64] = {};
    int bombPositionPlayerCount = 0;
    float bombPositionMinPlayerZ = FLT_MAX;
    float bombPositionMaxPlayerZ = -FLT_MAX;
    for (int resolvedIdx = 0; resolvedIdx < playerResolvedSlotCount; ++resolvedIdx) {
        const int idx = playerResolvedSlots[resolvedIdx];
        if (!coreReadFresh[idx] ||
            !coreReadAlive[idx] ||
            healths[idx] <= 0 ||
            lifeStates[idx] != 0 ||
            !isValidWorldPos(positions[idx])) {
            continue;
        }
        if (bombPositionPlayerCount >= 64)
            break;
        bombPositionPlayerSlots[bombPositionPlayerCount++] = idx;
        bombPositionMinPlayerZ =
            (std::min)(bombPositionMinPlayerZ, positions[idx].z);
        bombPositionMaxPlayerZ =
            (std::max)(bombPositionMaxPlayerZ, positions[idx].z);
    }

    auto isDroppedC4PositionPlausible = [&](const Vector3& pos) -> bool {
        if (!isValidWorldPos(pos))
            return false;

        if (minimapBoundsValid) {
            const float minX = (std::min)(minimapMins.x, minimapMaxs.x) - 1024.0f;
            const float maxX = (std::max)(minimapMins.x, minimapMaxs.x) + 1024.0f;
            const float minY = (std::min)(minimapMins.y, minimapMaxs.y) - 1024.0f;
            const float maxY = (std::max)(minimapMins.y, minimapMaxs.y) + 1024.0f;
            if (pos.x < minX || pos.x > maxX || pos.y < minY || pos.y > maxY)
                return false;
        }

        // A validated C4 may be on a different floor from every living player.
        // Player-relative height is only a heuristic for weak candidates below,
        // never a reason to discard an authoritative entity/rules sample.
        return true;
    };

    auto isDroppedC4WeakPositionPlausible = [&](const Vector3& pos) -> bool {
        if (!isDroppedC4PositionPlausible(pos))
            return false;

        float nearestDist2 = FLT_MAX;
        float nearestDz = FLT_MAX;
        for (int sampleIdx = 0; sampleIdx < bombPositionPlayerCount; ++sampleIdx) {
            const int i = bombPositionPlayerSlots[sampleIdx];
            const float dx = pos.x - positions[i].x;
            const float dy = pos.y - positions[i].y;
            const float dist2 = dx * dx + dy * dy;
            if (dist2 < nearestDist2) {
                nearestDist2 = dist2;
                nearestDz = std::fabs(pos.z - positions[i].z);
            }
        }

        if (bombPositionPlayerCount == 0)
            return true;
        if (pos.z < bombPositionMinPlayerZ - 160.0f ||
            pos.z > bombPositionMaxPlayerZ + 160.0f) {
            return false;
        }
        if (nearestDist2 <= (384.0f * 384.0f) && nearestDz > 128.0f)
            return false;
        return true;
    };

    auto looksLikePlantedC4Entity = [&](uintptr_t ent) -> bool {
        if (!ent)
            return false;
        if (ofs.C_PlantedC4_m_bHasExploded > 0) {
            uint8_t hasExploded = 0;
            if (readValue(ent + ofs.C_PlantedC4_m_bHasExploded, &hasExploded, sizeof(hasExploded))) {
                if (hasExploded != 0)
                    return false;
            }
        }
        if (ofs.C_PlantedC4_m_bBombDefused > 0) {
            uint8_t bombDefusedValue = 0;
            if (readValue(ent + ofs.C_PlantedC4_m_bBombDefused, &bombDefusedValue, sizeof(bombDefusedValue))) {
                if (bombDefusedValue != 0)
                    return false;
            }
        }
        if (ofs.C_PlantedC4_m_bC4Activated > 0) {
            uint8_t activated = 0;
            if (readValue(ent + ofs.C_PlantedC4_m_bC4Activated, &activated, sizeof(activated))) {
                if (activated != 0)
                    return true;
            }
        }
        if (ofs.C_PlantedC4_m_bBombTicking > 0) {
            uint8_t ticking = 0;
            if (readValue(ent + ofs.C_PlantedC4_m_bBombTicking, &ticking, sizeof(ticking))) {
                if (ticking != 0)
                    return true;
            }
        }
        if (ofs.C_PlantedC4_m_flC4Blow > 0) {
            float blowTime = 0.0f;
            if (readValue(ent + ofs.C_PlantedC4_m_flC4Blow, &blowTime, sizeof(blowTime))) {
                if (std::isfinite(blowTime) && blowTime > 0.0f)
                    return true;
            }
        }
        uintptr_t sceneNode = 0;
        if (tryResolveSceneNode(ent, &sceneNode))
            return true;
        return false;
    };

    auto resolvePlantedC4Entity = [&](uintptr_t candidate) -> uintptr_t {
        candidate = sanitizePointer(candidate);
        if (!candidate)
            return 0;
        if (looksLikePlantedC4Entity(candidate))
            return candidate;
        uintptr_t deref = 0;
        if (readPointer(candidate, &deref) && looksLikePlantedC4Entity(deref))
            return deref;
        return 0;
    };

    auto hasC4ItemDefinition = [&](uintptr_t entity) -> bool {
        entity = sanitizePointer(entity);
        if (!entity ||
            ofs.C_EconEntity_m_AttributeManager <= 0 ||
            ofs.C_AttributeContainer_m_Item <= 0 ||
            ofs.C_EconItemView_m_iItemDefinitionIndex <= 0) {
            return false;
        }

        uint16_t itemDefinition = 0;
        const uintptr_t itemDefinitionAddress =
            entity +
            static_cast<uintptr_t>(ofs.C_EconEntity_m_AttributeManager) +
            static_cast<uintptr_t>(ofs.C_AttributeContainer_m_Item) +
            static_cast<uintptr_t>(ofs.C_EconItemView_m_iItemDefinitionIndex);
        return readValue(
                   itemDefinitionAddress,
                   &itemDefinition,
                   sizeof(itemDefinition)) &&
               itemDefinition == kWeaponC4Id;
    };

    auto entityHasNoOwner = [&](uintptr_t entity) -> bool {
        entity = sanitizePointer(entity);
        if (!entity || ofs.C_BaseEntity_m_hOwnerEntity <= 0)
            return false;
        uint32_t owner = 0;
        if (!readValue(entity + ofs.C_BaseEntity_m_hOwnerEntity, &owner, sizeof(owner)))
            return false;
        return owner == 0u || owner == 0xFFFFFFFFu;
    };

    // dwWeaponC4 is often a CUtlVector<C_C4*>. Prefer unowned when dropped;
    // game often keeps a stale owner handle for a while; still accept any C4.
    auto pickC4FromWeaponList = [&](uintptr_t rootCandidate, bool requireNoOwner) -> uintptr_t {
        auto tryEntity = [&](uintptr_t ent) -> uintptr_t {
            ent = sanitizePointer(ent);
            if (!ent || !hasC4ItemDefinition(ent))
                return 0;
            if (requireNoOwner && !entityHasNoOwner(ent))
                return 0;
            return ent;
        };

        auto scanUtlVectorBatched = [&](uintptr_t vectorBase) -> uintptr_t {
            vectorBase = sanitizePointer(vectorBase);
            if (!vectorBase)
                return 0;
            uintptr_t memory = 0;
            int32_t size = 0;
            if (!readValue(vectorBase, &memory, sizeof(memory)) ||
                !readValue(vectorBase + 0x10u, &size, sizeof(size)))
                return 0;
            memory = sanitizePointer(memory);
            if (!memory || size <= 0 || size > 32)
                return 0;

            uintptr_t candidateEnts[32] = {};
            const int readCount = std::min<int>(size, 32);
            if (!readValue(memory, candidateEnts, static_cast<size_t>(readCount) * sizeof(uintptr_t)))
                return 0;

            uint16_t candidateItemDefs[32] = {};
            uint32_t candidateOwners[32] = {};
            uint32_t candidateDropTicks[32] = {};
            DWORD candidateItemDefBytesRead[32] = {};
            DWORD candidateOwnerBytesRead[32] = {};
            DWORD candidateDropTickBytesRead[32] = {};
            bool candidateValid[32] = {};

            bool queuedCandidateReads = false;
            for (int i = 0; i < readCount; ++i) {
                uintptr_t ent = sanitizePointer(candidateEnts[i]);
                if (!ent)
                    continue;
                candidateEnts[i] = ent;
                candidateValid[i] = true;

                if (ofs.C_EconEntity_m_AttributeManager > 0 &&
                    ofs.C_AttributeContainer_m_Item > 0 &&
                    ofs.C_EconItemView_m_iItemDefinitionIndex > 0) {
                    const uintptr_t itemDefAddr =
                        ent +
                        static_cast<uintptr_t>(ofs.C_EconEntity_m_AttributeManager) +
                        static_cast<uintptr_t>(ofs.C_AttributeContainer_m_Item) +
                        static_cast<uintptr_t>(ofs.C_EconItemView_m_iItemDefinitionIndex);
                    mem.AddScatterReadRequest(
                        handle,
                        itemDefAddr,
                        &candidateItemDefs[i],
                        sizeof(uint16_t),
                        &candidateItemDefBytesRead[i]);
                    queuedCandidateReads = true;
                }
                if (ofs.C_BaseEntity_m_hOwnerEntity > 0) {
                    const uintptr_t ownerAddr =
                        ent + static_cast<uintptr_t>(ofs.C_BaseEntity_m_hOwnerEntity);
                    mem.AddScatterReadRequest(
                        handle,
                        ownerAddr,
                        &candidateOwners[i],
                        sizeof(uint32_t),
                        &candidateOwnerBytesRead[i]);
                    queuedCandidateReads = true;
                }
                if (ofs.C_CSWeaponBase_m_nDropTick > 0) {
                    mem.AddScatterReadRequest(
                        handle,
                        ent + static_cast<uintptr_t>(ofs.C_CSWeaponBase_m_nDropTick),
                        &candidateDropTicks[i],
                        sizeof(uint32_t),
                        &candidateDropTickBytesRead[i]);
                    queuedCandidateReads = true;
                }
            }
            if (!queuedCandidateReads || !executeOptionalScatterRead())
                return 0;

            bool candidateEligible[32] = {};
            for (int i = 0; i < readCount; ++i) {
                if (!candidateValid[i])
                    continue;
                const bool itemDefinitionComplete =
                    esp::data::IsBombFieldReadComplete(
                        true,
                        candidateItemDefBytesRead[i],
                        sizeof(candidateItemDefs[i]));
                const bool ownerComplete =
                    esp::data::IsBombFieldReadComplete(
                        ofs.C_BaseEntity_m_hOwnerEntity > 0,
                        candidateOwnerBytesRead[i],
                        sizeof(candidateOwners[i]));
                const bool dropTickComplete =
                    esp::data::IsBombFieldReadComplete(
                        ofs.C_CSWeaponBase_m_nDropTick > 0,
                        candidateDropTickBytesRead[i],
                        sizeof(candidateDropTicks[i]));
                if (!itemDefinitionComplete || !dropTickComplete)
                    continue;
                if (candidateItemDefs[i] != kWeaponC4Id)
                    continue;
                const uint32_t owner = candidateOwners[i];
                const bool unowned = (owner == 0u || owner == 0xFFFFFFFFu);
                if (requireNoOwner &&
                    (!ownerComplete ||
                     ofs.C_BaseEntity_m_hOwnerEntity <= 0 ||
                     !unowned))
                    continue;
                candidateEligible[i] = true;
            }

            const int selectedIndex =
                esp::data::SelectNewestC4DropCandidateIndex(
                    candidateDropTicks,
                    candidateEligible,
                    readCount);
            if (selectedIndex < 0)
                return 0;

            const uint32_t selectedOwner = candidateOwners[selectedIndex];
            const bool selectedUnowned =
                selectedOwner == 0u || selectedOwner == 0xFFFFFFFFu;
            if (bombDroppedRulesRoseThisTick &&
                narrowDebug.Enabled(esp::diagnostics::kNarrowDebugC4)) {
                DmaLogPrintf(
                    "[DEBUG] C4 drop-edge probe idx=%d ent=0x%llx def=%u owner=0x%x unowned=%d drop_tick=%u",
                    selectedIndex,
                    static_cast<unsigned long long>(candidateEnts[selectedIndex]),
                    candidateItemDefs[selectedIndex],
                    selectedOwner,
                    selectedUnowned ? 1 : 0,
                    candidateDropTicks[selectedIndex]);
            }
            weaponC4ListOrdinal = static_cast<uint32_t>(selectedIndex + 1);
            weaponC4DropTick = candidateDropTicks[selectedIndex];
            return candidateEnts[selectedIndex];
        };

        if (ofs.dwWeaponC4 > 0) {
            if (uintptr_t e = scanUtlVectorBatched(g::clientBase + static_cast<uintptr_t>(ofs.dwWeaponC4)))
                return e;
        }
        if (uintptr_t e = scanUtlVectorBatched(rootCandidate))
            return e;

        if (uintptr_t e = tryEntity(rootCandidate))
            return e;

        uintptr_t indirect = 0;
        if (readPointer(rootCandidate, &indirect)) {
            if (uintptr_t e = tryEntity(indirect))
                return e;
        }
        return 0;
    };

    uintptr_t preferredCarriedC4Entity = 0;
    auto resolveWeaponC4Entity = [&](uintptr_t rootCandidate) -> uintptr_t {
        rootCandidate = sanitizePointer(rootCandidate);

        if (preferredCarriedC4Entity &&
            hasC4ItemDefinition(preferredCarriedC4Entity)) {
            weaponC4ResolutionMode = 7;
            return preferredCarriedC4Entity;
        }

        // World C4 first when dropped (any owner state; rules are authoritative).
        if (bombDroppedByRules &&
            worldScanFoundC4 &&
            worldScanC4Entity) {
            weaponC4ResolutionMode = 4;
            return worldScanC4Entity;
        }

        if (bombDroppedByRules) {
            // Round transitions leave the new live C4 attached to the previous
            // owner briefly, while old entries are already unowned. Recency
            // must win before owner state or the old round is selected.
            if (uintptr_t listed = pickC4FromWeaponList(rootCandidate, false)) {
                weaponC4ResolutionMode = 6;
                return listed;
            }
            if (uintptr_t listed = pickC4FromWeaponList(rootCandidate, true)) {
                weaponC4ResolutionMode = 6;
                return listed;
            }
        }

        if (!rootCandidate)
            return 0;

        if (!bombDroppedByRules &&
            rootCandidate == s_cachedWeaponRootCandidate &&
            s_mergedWeaponEntity &&
            hasC4ItemDefinition(s_mergedWeaponEntity)) {
            weaponC4ResolutionMode = 3;
            return s_mergedWeaponEntity;
        }

        const bool rootIsC4 = hasC4ItemDefinition(rootCandidate);
        if (rootIsC4) {
            weaponC4ResolutionMode = 1;
            return esp::data::SelectValidatedWeaponC4Entity(
                rootCandidate,
                true,
                0,
                false,
                0);
        }

        uintptr_t indirectCandidate = 0;
        bool indirectIsC4 = false;
        if (readPointer(rootCandidate, &indirectCandidate)) {
            indirectCandidate = sanitizePointer(indirectCandidate);
            indirectIsC4 = hasC4ItemDefinition(indirectCandidate);
            if (indirectIsC4) {
                weaponC4ResolutionMode = 2;
                return esp::data::SelectValidatedWeaponC4Entity(
                    rootCandidate,
                    false,
                    indirectCandidate,
                    true,
                    0);
            }
        }

        const uintptr_t worldCandidate =
            worldScanFoundC4 ? worldScanC4Entity : 0;
        const uintptr_t selected =
            esp::data::SelectValidatedWeaponC4Entity(
                rootCandidate,
                false,
                indirectCandidate,
                false,
                worldCandidate);
        if (selected) {
            weaponC4ResolutionMode = 4;
            return selected;
        }

        return 0;
    };

    bool inventoryC4CarrierEvidence = false;
    bool strictInventoryC4CarrierEvidence = false;
    int inventoryC4CarrierSlot = -1;
    uintptr_t activeC4EntityCandidate = 0;
    int exactInventoryC4CarrierCount = 0;
    int exactInventoryC4CarrierSlot = -1;
    uintptr_t exactInventoryC4Entity = 0;
    int activeC4CarrierCount = 0;
    for (int resolvedIdx = 0; resolvedIdx < playerResolvedSlotCount; ++resolvedIdx) {
        const int i = playerResolvedSlots[resolvedIdx];
        if (!pawns[i] ||
            healths[i] <= 0 ||
            lifeStates[i] != 0 ||
            teams[i] != 2) {
            continue;
        }
        const uint16_t liveWeaponId = (weaponIds[i] < 20000u) ? weaponIds[i] : 0u;
        bool explicitInventoryC4 = false;
        uintptr_t explicitInventoryC4Entity = 0;
        const int inventorySlotCount = getInventorySlotCount(i);
        for (int slot = 0; slot < inventorySlotCount; ++slot) {
            if (inventoryWeaponIds[i][slot] != kWeaponC4Id)
                continue;
            explicitInventoryC4 = true;
            if (isLikelyGamePointer(inventoryWeapons[i][slot]))
                explicitInventoryC4Entity = inventoryWeapons[i][slot];
            break;
        }
        if (!inventoryHasBombBySlot[i] &&
            !explicitInventoryC4 &&
            liveWeaponId != kWeaponC4Id) {
            continue;
        }
        bombCarrierBySlot[i] = true;
        inventoryC4CarrierEvidence = true;
        if (explicitInventoryC4) {
            if (inventoryC4CarrierSlot < 0) {
                inventoryC4CarrierSlot = i;
            }
            if (_inventoryFullTick &&
                fullInventoryPlayerCoverageComplete[i] &&
                isLikelyGamePointer(explicitInventoryC4Entity)) {
                ++exactInventoryC4CarrierCount;
                if (exactInventoryC4CarrierCount == 1) {
                    exactInventoryC4CarrierSlot = i;
                    exactInventoryC4Entity = explicitInventoryC4Entity;
                } else {
                    exactInventoryC4CarrierSlot = -1;
                    exactInventoryC4Entity = 0;
                }
            }
        }
        if (_inventoryActiveTick &&
            activeWeaponHandleReadFresh[i] &&
            weaponHandleChainFailures == 0 &&
            weaponEntryFailures == 0 &&
            weaponEntityFailures == 0 &&
            weaponMetaFailures == 0 &&
            liveWeaponId == kWeaponC4Id &&
            isLikelyGamePointer(activeWeapons[i])) {
            ++activeC4CarrierCount;
            if (activeC4CarrierCount == 1)
                activeC4EntityCandidate = activeWeapons[i];
            else
                activeC4EntityCandidate = 0;
        }
    }
    const bool fullInventoryReadSucceeded =
        _inventoryFullTick &&
        !fullInventoryReadFailed;
    if (fullInventoryReadSucceeded) {
        if (exactInventoryC4CarrierCount == 1 &&
            exactInventoryC4CarrierSlot >= 0 &&
            isLikelyGamePointer(exactInventoryC4Entity) &&
            esp::data::IsBombInventorySamplePastDropEdge(
                bombDroppedByRules,
                s_bombDroppedRulesRiseUs,
                nowUs)) {
            s_freshInventoryC4CarrierSlot = exactInventoryC4CarrierSlot;
            s_freshInventoryC4Entity = exactInventoryC4Entity;
            s_freshInventoryC4CarrierUs = nowUs;
        } else if (fullInventoryC4CoverageComplete ||
                   exactInventoryC4CarrierCount > 1) {
            s_freshInventoryC4CarrierSlot = -1;
            s_freshInventoryC4Entity = 0;
            s_freshInventoryC4CarrierUs = 0;
        }
    }
    const bool freshInventoryCarrierStillAlive =
        s_freshInventoryC4CarrierSlot >= 0 &&
        s_freshInventoryC4CarrierSlot < 64 &&
        pawns[s_freshInventoryC4CarrierSlot] != 0 &&
        healths[s_freshInventoryC4CarrierSlot] > 0 &&
        lifeStates[s_freshInventoryC4CarrierSlot] == 0 &&
        teams[s_freshInventoryC4CarrierSlot] == 2;
    const bool freshInventoryC4CarrierEvidence =
        freshInventoryCarrierStillAlive &&
        isLikelyGamePointer(s_freshInventoryC4Entity) &&
        esp::data::IsFreshInventoryC4CarrierEvidence(
            s_freshInventoryC4CarrierSlot,
            s_freshInventoryC4CarrierUs,
            nowUs);
    strictInventoryC4CarrierEvidence = freshInventoryC4CarrierEvidence;
    if (freshInventoryC4CarrierEvidence) {
        inventoryC4CarrierSlot = s_freshInventoryC4CarrierSlot;
    }
    if (activeC4EntityCandidate) {
        preferredCarriedC4Entity = activeC4EntityCandidate;
    } else if (freshInventoryC4CarrierEvidence &&
               s_freshInventoryC4Entity) {
        preferredCarriedC4Entity = s_freshInventoryC4Entity;
    }

    bool plantedDefuseProbeHint = false;
    if (bombPlantedByRules) {
        for (int resolvedIdx = 0; resolvedIdx < playerResolvedSlotCount; ++resolvedIdx) {
            const int i = playerResolvedSlots[resolvedIdx];
            if (esp::data::IsDefusingPawnCandidate(
                    defusingFlags[i] == 1u,
                    pawns[i] != 0,
                    healths[i],
                    lifeStates[i],
                    teams[i])) {
                plantedDefuseProbeHint = true;
                break;
            }
        }
    }

    #include "bomb_parts/bomb_entity_acquisition.inl"

    #include "bomb_parts/bomb_owner_signals.inl"

    
    
    

    
    #include "bomb_parts/bomb_state_resolve.inl"
