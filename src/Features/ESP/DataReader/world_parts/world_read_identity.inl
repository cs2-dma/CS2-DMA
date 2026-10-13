        int newEntityCount = 0;
        int dynamicRefreshCount = 0;
        if (worldScanOk) {
            
            
            
            
            for (int candidateIdx = 0; candidateIdx < worldCandidateCount; ++candidateIdx) {
                const int idx = s_worldCandidateIndices[candidateIdx];
                const int blockIdx = idx >> 9;
                const bool blockReadComplete =
                    blockIdx >= 0 &&
                    blockIdx < blockCount &&
                    esp::data::IsWorldFieldReadComplete(
                        worldBlockReadBytes[blockIdx],
                        sizeof(uintptr_t));
                const bool entityReadComplete =
                    blockReadComplete &&
                    app::memory_address::IsCompleteGamePointerSample(
                        worldEntities[idx], worldEntityReadBytes[idx]);
                worldEntityReadComplete[idx] = entityReadComplete ? 1u : 0u;
                if (!entityReadComplete) {
                    if (worldDomainBombRescueDue &&
                        s_worldEntityItemIds[idx] == kWeaponC4Id)
                        worldScanC4ReadIncomplete = true;
                    worldEntities[idx] = s_worldEntityRefs[idx];
                    continue;
                }
                const uintptr_t ent = worldEntities[idx];
                if (!ent)
                    continue;

                const bool entityUnchanged = (ent == s_worldEntityRefs[idx]);
                const auto cachedNodeClass = static_cast<esp::data::WorldEntityClass>(s_worldEntityClassKinds[idx]);
                const bool acceptsNullNode = cachedNodeClass == esp::data::WorldEntityClass::Inferno ||
                    cachedNodeClass == esp::data::WorldEntityClass::SmokeProjectile;
                if (entityUnchanged && (s_cachedWorldSceneNodes[idx] || acceptsNullNode)) {
                    const bool cachedC4 =
                        s_worldEntityItemIds[idx] == kWeaponC4Id &&
                        worldDomainBombRescueDue;
                    bool sceneNodeComplete = true;
                    if (worldSceneNodeRefreshRequested[idx] != 0u) {
                        sceneNodeComplete =
                            app::memory_address::IsCompleteGamePointerSample(
                                worldSceneNodes[idx], worldSceneNodeReadBytes[idx]) &&
                            (worldSceneNodes[idx] != 0 || acceptsNullNode);
                        if (!sceneNodeComplete) {
                            if (cachedC4)
                                worldScanC4ReadIncomplete = true;
                            worldSceneNodes[idx] = s_cachedWorldSceneNodes[idx];
                        } else if (worldSceneNodes[idx] != s_cachedWorldSceneNodes[idx]) {
                            worldSceneNodeChanged[idx] = 1u;
                            ++dynamicRefreshCount;
                        }
                    } else {
                        worldSceneNodes[idx] = s_cachedWorldSceneNodes[idx];
                    }
                    const bool ownerRefreshRequested =
                        needsWorldOwnerRefreshForItemId(s_worldEntityItemIds[idx]) &&
                        ofs.C_BaseEntity_m_hOwnerEntity > 0;
                    const bool ownerComplete =
                        !ownerRefreshRequested ||
                        esp::data::IsWorldFieldReadComplete(
                            worldOwnerReadBytes[idx],
                            sizeof(uint32_t));
                    if (!ownerComplete) {
                        if (cachedC4)
                            worldScanC4ReadIncomplete = true;
                        worldOwnerHandles[idx] = s_cachedWorldOwnerHandles[idx];
                    }
                    const bool refreshedSubclass =
                        s_identityRefreshMask[idx] != 0u &&
                        esp::data::IsWorldFieldReadComplete(
                            worldSubclassReadBytes[idx],
                            sizeof(uint32_t));
                    const bool refreshedItemDefinition =
                        s_identityRefreshMask[idx] != 0u &&
                        esp::data::IsWorldFieldReadComplete(
                            worldItemDefReadBytes[idx],
                            sizeof(uint16_t));
                    if (!refreshedSubclass)
                        worldSubclassIds[idx] = s_worldEntitySubclassIds[idx];
                    if (!refreshedItemDefinition)
                        worldItemDefs[idx] = s_worldEntityItemIds[idx];
                    worldClassKinds[idx] = s_worldEntityClassKinds[idx];
                    worldBasicDetailsComplete[idx] =
                        esp::data::AreWorldBasicDetailsComplete(
                            sceneNodeComplete,
                            true)
                            ? 1u
                            : 0u;
                } else {
                    
                    ++newEntityCount;
                    // Cached dependent reads were queued for the previous entity.
                    // Reset values AND completion counts before reading a replacement,
                    // or an incomplete new read can inherit the old entity's success.
                    const DWORD entityBytes = worldEntityReadBytes[idx];
                    resetWorldScratchSlot(idx);
                    worldEntities[idx] = ent;
                    worldEntityReadBytes[idx] = entityBytes;
                    worldEntityReadComplete[idx] = 1u;
                    worldDetailsReadQueued[idx] = 1u;
                    if (entityUnchanged)
                        worldClassKinds[idx] = s_worldEntityClassKinds[idx];
                    s_positionReadAttemptedMask[idx] = 0;
                    s_identityRefreshMask[idx] = 0;



                    mem.AddScatterReadRequest(
                        handle,
                        ent + ofs.C_BaseEntity_m_pGameSceneNode,
                        &worldSceneNodes[idx],
                        sizeof(uintptr_t),
                        &worldSceneNodeReadBytes[idx]);
                    if (wantsWorldOwnerHandles && ofs.C_BaseEntity_m_hOwnerEntity > 0) {
                        mem.AddScatterReadRequest(
                            handle,
                            ent + ofs.C_BaseEntity_m_hOwnerEntity,
                            &worldOwnerHandles[idx],
                            sizeof(uint32_t),
                            &worldOwnerReadBytes[idx]);
                    }
                    if (!bombOnlyWorldMode && ofs.C_BaseEntity_m_nSubclassID > 0) {
                        mem.AddScatterReadRequest(
                            handle,
                            ent + ofs.C_BaseEntity_m_nSubclassID,
                            &worldSubclassIds[idx],
                            sizeof(uint32_t),
                            &worldSubclassReadBytes[idx]);
                    }
                    if (wantsWorldIdentityData && ofs.CEntityInstance_m_pEntity > 0) {
                        mem.AddScatterReadRequest(
                            handle,
                            ent + ofs.CEntityInstance_m_pEntity,
                            &worldIdentities[idx],
                            sizeof(uintptr_t),
                            &worldIdentityReadBytes[idx]);
                    }
                    // Item definition is the independent numeric identity path.
                    // Keep it for utility scans: designerName requires three DMA
                    // hops and can transiently fail when a new entity is created.
                    const bool wantsItemDefinition =
                        esp::data::ShouldReadWorldItemDefinition(
                            wantsWorldUtilityData,
                            worldDomainDroppedItemsDue,
                            worldDomainBombRescueDue);
                    if (wantsItemDefinition &&
                        ofs.C_EconEntity_m_AttributeManager > 0 &&
                        ofs.C_AttributeContainer_m_Item > 0 &&
                        ofs.C_EconItemView_m_iItemDefinitionIndex > 0) {
                        const uintptr_t itemDefAddress =
                            ent + ofs.C_EconEntity_m_AttributeManager + ofs.C_AttributeContainer_m_Item + ofs.C_EconItemView_m_iItemDefinitionIndex;
                        mem.AddScatterReadRequest(
                            handle,
                            itemDefAddress,
                            &worldItemDefs[idx],
                            sizeof(uint16_t),
                            &worldItemDefReadBytes[idx]);
                    }
                }
            }
            
            if (newEntityCount > 0 || cachedIdentityRefreshCount > 0) {
                if (newEntityCount > 0)
                    executeOptionalScatterRead();
                for (int candidateIdx = 0; candidateIdx < worldCandidateCount; ++candidateIdx) {
                    const int idx = s_worldCandidateIndices[candidateIdx];
                    if (!worldEntityReadComplete[idx] ||
                        !worldEntities[idx] ||
                        worldDetailsReadQueued[idx] == 0u) {
                        continue;
                    }
                    const bool sceneNodeComplete =
                        esp::data::IsWorldFieldReadComplete(
                            worldSceneNodeReadBytes[idx],
                            sizeof(uintptr_t)) &&
                        isLikelyGamePointer(worldSceneNodes[idx]);
                    const bool subclassComplete =
                        bombOnlyWorldMode ||
                        ofs.C_BaseEntity_m_nSubclassID <= 0 ||
                        esp::data::IsWorldFieldReadComplete(
                            worldSubclassReadBytes[idx],
                            sizeof(uint32_t));
                    const bool itemDefComplete =
                        esp::data::IsWorldFieldReadComplete(
                            worldItemDefReadBytes[idx],
                            sizeof(uint16_t));
                    const bool sameEntity = worldEntities[idx] == s_worldEntityRefs[idx];
                    if (!subclassComplete && sameEntity)
                        worldSubclassIds[idx] = s_worldEntitySubclassIds[idx];
                    if (!itemDefComplete)
                        worldItemDefs[idx] = sameEntity ? s_worldEntityItemIds[idx] : 0u;
                    if (!esp::data::IsWorldFieldReadComplete(
                            worldOwnerReadBytes[idx],
                            sizeof(uint32_t))) {
                        worldOwnerHandles[idx] = 0;
                    }
                    worldBasicDetailsComplete[idx] =
                        esp::data::AreWorldBasicDetailsComplete(
                            sceneNodeComplete,
                            subclassComplete)
                            ? 1u
                            : 0u;
                    if (worldBasicDetailsComplete[idx])
                        ++dynamicRefreshCount;
                }

                int identityCount = 0;
                if (wantsWorldIdentityData && ofs.CEntityIdentity_m_designerName > 0) {
                    for (int candidateIdx = 0; candidateIdx < worldCandidateCount; ++candidateIdx) {
                        const int idx = s_worldCandidateIndices[candidateIdx];
                        if (!worldEntityReadComplete[idx] ||
                            !worldEntities[idx] ||
                            !esp::data::IsWorldFieldReadComplete(
                                worldIdentityReadBytes[idx],
                                sizeof(uintptr_t)) ||
                            !isLikelyGamePointer(worldIdentities[idx])) {
                            continue;
                        }
                        const bool entityChanged =
                            worldEntities[idx] != s_worldEntityRefs[idx];
                        if (!entityChanged && worldDetailsReadQueued[idx] == 0u &&
                            s_identityRefreshMask[idx] == 0u &&
                            worldClassKinds[idx] !=
                                static_cast<uint8_t>(esp::data::WorldEntityClass::Unknown)) {
                            continue;
                        }
                        mem.AddScatterReadRequest(
                            handle,
                            worldIdentities[idx] + ofs.CEntityIdentity_m_designerName,
                            &worldDesignerNamePtrs[idx],
                            sizeof(uintptr_t),
                            &worldDesignerNamePtrReadBytes[idx]);
                        ++identityCount;
                    }
                }
                if (identityCount > 0)
                    executeOptionalScatterRead();

                int designerNameCount = 0;
                for (int candidateIdx = 0; candidateIdx < worldCandidateCount; ++candidateIdx) {
                    const int idx = s_worldCandidateIndices[candidateIdx];
                    if (!esp::data::IsWorldDesignerNamePointerSample(
                            worldDesignerNamePtrs[idx], worldDesignerNamePtrReadBytes[idx])) {
                        continue;
                    }
                    mem.AddScatterReadRequest(
                        handle,
                        worldDesignerNamePtrs[idx],
                        worldDesignerNames[idx],
                        sizeof(worldDesignerNames[idx]) - 1u,
                        &worldDesignerNameReadBytes[idx]);
                    ++designerNameCount;
                }
                if (designerNameCount > 0)
                    executeOptionalScatterRead();

                for (int candidateIdx = 0; candidateIdx < worldCandidateCount; ++candidateIdx) {
                    const int idx = s_worldCandidateIndices[candidateIdx];
                    const auto designerName = esp::data::ReadWorldDesignerName(
                        worldDesignerNames[idx], sizeof(worldDesignerNames[idx]),
                        worldDesignerNameReadBytes[idx]);
                    if (designerName.empty()) {
                        continue;
                    }
                    const auto entityClass = esp::data::ClassifyWorldDesignerName(
                        designerName);
                    worldClassKinds[idx] = static_cast<uint8_t>(entityClass);
                    rememberEntityClass(worldSubclassIds[idx], entityClass);
                    const uint16_t inferredItemId = esp::weapons::WeaponItemIdFromDesignerName(designerName);
                    worldItemDefs[idx] = esp::data::CanonicalWorldItemId(worldItemDefs[idx], entityClass, inferredItemId);
                }
            }
        }
