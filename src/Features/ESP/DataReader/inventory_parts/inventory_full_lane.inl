    if (fullInventoryLaneDue) {
        DWORD inventoryCountBytesRead[64] = {};
        DWORD inventoryArrayBytesRead[64] = {};
        DWORD inventoryHandleBytesRead[64][kMaxInventoryWeapons] = {};
        const bool fullInventoryNeedsAllPlayers =
            webRadarDemandActive || wantsNoKnifeInventoryData;
        const int fullInventoryCandidateCount = inventoryPlayerSlotCount;
        int includedFullInventoryPlayers = 0;
        for (int slotIndex = 0;
             slotIndex < fullInventoryCandidateCount;
             ++slotIndex) {
            const int playerSlot = inventoryPlayerSlots[slotIndex];
            if (!esp::data::ShouldIncludeFullInventoryPlayer(
                    fullInventoryNeedsAllPlayers,
                    teams[playerSlot])) {
                continue;
            }
            inventoryPlayerSlots[includedFullInventoryPlayers++] =
                playerSlot;
        }
        inventoryPlayerSlotCount = includedFullInventoryPlayers;
        auto recomputeInventoryBombOwnership = [&]() {
            memset(inventoryHasBombBySlot, 0, sizeof(inventoryHasBombBySlot));
            for (int inventorySlotIdx = 0; inventorySlotIdx < inventoryPlayerSlotCount; ++inventorySlotIdx) {
                const int i = inventoryPlayerSlots[inventorySlotIdx];
                const int inventorySlotCount = getInventorySlotCount(i);
                for (int slot = 0; slot < inventorySlotCount; ++slot) {
                    if (inventoryWeaponIds[i][slot] == kWeaponC4Id ||
                        inventoryWeapons[i][slot] == weaponC4Entity) {
                        inventoryHasBombBySlot[i] = true;
                        break;
                    }
                }
            }
        };
        auto queueInventoryWeaponMetadata = [&](
            const auto& weaponEntities,
            bool onlyMissing) {
            bool queued = false;
            for (int inventorySlotIdx = 0; inventorySlotIdx < inventoryPlayerSlotCount; ++inventorySlotIdx) {
                const int i = inventoryPlayerSlots[inventorySlotIdx];
                const int inventorySlotCount = getInventorySlotCount(i);
                for (int slot = 0; slot < inventorySlotCount; ++slot) {
                    const uintptr_t weaponEntity = weaponEntities[i][slot];
                    if (!weaponEntity)
                        continue;
                    if (onlyMissing &&
                        inventoryWeaponIds[i][slot] > 0 &&
                        inventoryWeaponIds[i][slot] < 20000u) {
                        continue;
                    }
                    const uintptr_t itemDefAddress =
                        weaponEntity +
                        ofs.C_EconEntity_m_AttributeManager +
                        ofs.C_AttributeContainer_m_Item +
                        ofs.C_EconItemView_m_iItemDefinitionIndex;
                    mem.AddScatterReadRequest(
                        handle,
                        itemDefAddress,
                        &inventoryWeaponIds[i][slot],
                        sizeof(uint16_t));
                    queued = true;
                }
            }
            return queued;
        };
        auto hasMissingInventoryMetadata = [&]() {
            for (int inventorySlotIdx = 0;
                 inventorySlotIdx < inventoryPlayerSlotCount;
                 ++inventorySlotIdx) {
                const int i = inventoryPlayerSlots[inventorySlotIdx];
                const int inventorySlotCount = getInventorySlotCount(i);
                for (int slot = 0; slot < inventorySlotCount; ++slot) {
                    if (inventoryWeapons[i][slot] &&
                        (inventoryWeaponIds[i][slot] == 0 ||
                         inventoryWeaponIds[i][slot] >= 20000u)) {
                        return true;
                    }
                }
            }
            return false;
        };
        bool queuedInventoryHandleReads = false;
        for (int inventorySlotIdx = 0; inventorySlotIdx < inventoryPlayerSlotCount; ++inventorySlotIdx) {
            const int i = inventoryPlayerSlots[inventorySlotIdx];
            if (!weaponServices[i] || ofs.CPlayer_WeaponServices_m_hMyWeapons <= 0)
                continue;
            mem.AddScatterReadRequest(
                handle,
                weaponServices[i] + ofs.CPlayer_WeaponServices_m_hMyWeapons,
                &inventoryWeaponCounts[i],
                sizeof(int),
                &inventoryCountBytesRead[i]);
            mem.AddScatterReadRequest(
                handle,
                weaponServices[i] + ofs.CPlayer_WeaponServices_m_hMyWeapons + 0x8,
                &inventoryWeaponHandleArrays[i],
                sizeof(uintptr_t),
                &inventoryArrayBytesRead[i]);
            queuedInventoryHandleReads = true;
        }
        for (int inventorySlotIdx = 0; inventorySlotIdx < inventoryPlayerSlotCount; ++inventorySlotIdx) {
            const int i = inventoryPlayerSlots[inventorySlotIdx];
            if (!s_cachedInvHandleArrays[i] || s_cachedInvCounts[i] <= 0)
                continue;
            const int cachedSlotCount = std::clamp(s_cachedInvCounts[i], 0, kMaxInventoryWeapons);
            for (int slot = 0; slot < cachedSlotCount; ++slot) {
                mem.AddScatterReadRequest(
                    handle,
                    s_cachedInvHandleArrays[i] + static_cast<uintptr_t>(slot) * sizeof(uint32_t),
                    &inventoryWeaponHandles[i][slot],
                    sizeof(uint32_t),
                    &inventoryHandleBytesRead[i][slot]);
            }
            queuedInventoryHandleReads = true;
        }
        if (queuedInventoryHandleReads) {
            if (!mem.ExecuteReadScatter(handle)) {
                fullInventoryReadFailed = true;
                inventoryHandleFailures = 1;
                memcpy(inventoryWeaponCounts, s_cachedInvCounts, sizeof(inventoryWeaponCounts));
                memcpy(inventoryWeaponHandleArrays, s_cachedInvHandleArrays, sizeof(inventoryWeaponHandleArrays));
                memcpy(inventoryWeaponHandles, s_cachedInventoryWeaponHandlesResolved, sizeof(inventoryWeaponHandles));
                memcpy(inventoryWeaponEntries, s_cachedInventoryWeaponEntries, sizeof(inventoryWeaponEntries));
                memcpy(inventoryWeapons, s_cachedInventoryWeaponsResolved, sizeof(inventoryWeapons));
                memcpy(inventoryWeaponIds, s_cachedInventoryWeaponIdsResolved, sizeof(inventoryWeaponIds));
                memcpy(inventoryHasBombBySlot, s_cachedInventoryHasBombResolved, sizeof(inventoryHasBombBySlot));
                logUpdateDataIssue("scatter_12_inv", "inventory_handles_failed_using_cached");
            } else {
                for (uintptr_t& handleArray : inventoryWeaponHandleArrays) {
                    if (!isLikelyGamePointer(handleArray))
                        handleArray = 0;
                }
                for (int inventorySlotIdx = 0; inventorySlotIdx < inventoryPlayerSlotCount; ++inventorySlotIdx) {
                    const int i = inventoryPlayerSlots[inventorySlotIdx];
                    if (!inventoryWeaponHandleArrays[i] ||
                        inventoryWeaponCounts[i] < 0 ||
                        inventoryWeaponCounts[i] > kMaxInventoryWeapons) {
                        inventoryWeaponCounts[i] = 0;
                        inventoryWeaponHandleArrays[i] = 0;
                        memset(inventoryWeaponHandles[i], 0, sizeof(inventoryWeaponHandles[i]));
                        continue;
                    }
                    for (int slot = 0; slot < kMaxInventoryWeapons; ++slot) {
                        if (!isValidEntityHandle(inventoryWeaponHandles[i][slot]))
                            inventoryWeaponHandles[i][slot] = 0;
                    }
                }

                bool arraysChanged = false;
                for (int inventorySlotIdx = 0; inventorySlotIdx < inventoryPlayerSlotCount; ++inventorySlotIdx) {
                    const int i = inventoryPlayerSlots[inventorySlotIdx];
                    if (inventoryWeaponHandleArrays[i] != s_cachedInvHandleArrays[i] ||
                        inventoryWeaponCounts[i] != s_cachedInvCounts[i]) {
                        arraysChanged = true;
                        break;
                    }
                }

                if (arraysChanged) {
                    memcpy(inventoryWeaponHandles, s_cachedInventoryWeaponHandlesResolved, sizeof(inventoryWeaponHandles));
                    bool queuedFreshHandleReads = false;
                    for (int inventorySlotIdx = 0; inventorySlotIdx < inventoryPlayerSlotCount; ++inventorySlotIdx) {
                        const int i = inventoryPlayerSlots[inventorySlotIdx];
                        const int inventorySlotCount = getInventorySlotCount(i);
                        if (!inventoryWeaponHandleArrays[i] || inventorySlotCount <= 0)
                            continue;
                        for (int slot = 0; slot < inventorySlotCount; ++slot) {
                            inventoryHandleBytesRead[i][slot] = 0;
                            mem.AddScatterReadRequest(
                                handle,
                                inventoryWeaponHandleArrays[i] + static_cast<uintptr_t>(slot) * sizeof(uint32_t),
                                &inventoryWeaponHandles[i][slot],
                                sizeof(uint32_t),
                                &inventoryHandleBytesRead[i][slot]);
                            queuedFreshHandleReads = true;
                        }
                    }
                    if (queuedFreshHandleReads && !mem.ExecuteReadScatter(handle)) {
                        fullInventoryReadFailed = true;
                        inventoryHandleFailures = 1;
                        memcpy(inventoryWeaponHandles, s_cachedInventoryWeaponHandlesResolved, sizeof(inventoryWeaponHandles));
                        memcpy(inventoryWeaponEntries, s_cachedInventoryWeaponEntries, sizeof(inventoryWeaponEntries));
                        memcpy(inventoryWeapons, s_cachedInventoryWeaponsResolved, sizeof(inventoryWeapons));
                        memcpy(inventoryWeaponIds, s_cachedInventoryWeaponIdsResolved, sizeof(inventoryWeaponIds));
                        memcpy(inventoryHasBombBySlot, s_cachedInventoryHasBombResolved, sizeof(inventoryHasBombBySlot));
                        logUpdateDataIssue("scatter_12_inv_refresh", "inventory_handle_refresh_failed_using_cached");
                    }
                }

                bool inventoryHandlesChanged = false;
                bool inventoryWeaponHandleChanged[64][kMaxInventoryWeapons] = {};
                for (int inventorySlotIdx = 0; inventorySlotIdx < inventoryPlayerSlotCount; ++inventorySlotIdx) {
                    const int i = inventoryPlayerSlots[inventorySlotIdx];
                    const int slotCount = std::clamp(inventoryWeaponCounts[i], 0, kMaxInventoryWeapons);
                    for (int slot = 0; slot < slotCount; ++slot) {
                        const bool cachedChainIncomplete =
                            isValidEntityHandle(inventoryWeaponHandles[i][slot]) &&
                            (!s_cachedInventoryWeaponEntries[i][slot] ||
                             !s_cachedInventoryWeaponsResolved[i][slot]);
                        if (inventoryWeaponHandles[i][slot] != s_cachedInventoryWeaponHandlesResolved[i][slot] ||
                            cachedChainIncomplete) {
                            inventoryWeaponHandleChanged[i][slot] = true;
                            inventoryHandlesChanged = true;
                        }
                    }
                }
                for (int inventorySlotIdx = 0; inventorySlotIdx < inventoryPlayerSlotCount; ++inventorySlotIdx) {
                    const int i = inventoryPlayerSlots[inventorySlotIdx];
                    s_cachedInvHandleArrays[i] = inventoryWeaponHandleArrays[i];
                    s_cachedInvCounts[i] = inventoryWeaponCounts[i];
                }

                if (inventoryHandlesChanged) {
                for (int inventorySlotIdx = 0;
                     inventorySlotIdx < inventoryPlayerSlotCount;
                     ++inventorySlotIdx) {
                    const int i = inventoryPlayerSlots[inventorySlotIdx];
                    const int inventorySlotCount =
                        getInventorySlotCount(i);
                    for (int slot = 0; slot < inventorySlotCount; ++slot) {
                        if (!inventoryWeaponHandleChanged[i][slot])
                            continue;
                        inventoryWeaponEntries[i][slot] = 0;
                        inventoryWeapons[i][slot] = 0;
                        inventoryWeaponIds[i][slot] = 0;
                    }
                }
                bool queuedInventoryEntryReads = false;

                bool missingEntityBlocks[EntityBlockCache::kBlockCount] = {};
                for (int inventorySlotIdx = 0; inventorySlotIdx < inventoryPlayerSlotCount; ++inventorySlotIdx) {
                    const int i = inventoryPlayerSlots[inventorySlotIdx];
                    const int inventorySlotCount = getInventorySlotCount(i);
                    for (int slot = 0; slot < inventorySlotCount; ++slot) {
                        if (!inventoryWeaponHandleChanged[i][slot])
                            continue;
                        const uint32_t weaponHandle = inventoryWeaponHandles[i][slot];
                        if (!isValidEntityHandle(weaponHandle))
                            continue;
                        const uint32_t block = (weaponHandle & kEntityHandleMask) >> 9;
                        const uintptr_t cachedBlock = s_entityBlockCache.Get(block);
                        if (isLikelyGamePointer(cachedBlock)) {
                            inventoryWeaponEntries[i][slot] = cachedBlock;
                        } else if (block < EntityBlockCache::kBlockCount) {
                            missingEntityBlocks[block] = true;
                        }
                    }
                }
                for (uint32_t block = 0; block < EntityBlockCache::kBlockCount; ++block) {
                    if (!missingEntityBlocks[block])
                        continue;
                    s_entityBlockCache.Invalidate(block);
                    mem.AddScatterReadRequest(
                        handle,
                        entityList + 0x10 + 8 * block,
                        &s_entityBlockCache.blocks[block],
                        sizeof(uintptr_t));
                    queuedInventoryEntryReads = true;
                }

                for (int inventorySlotIdx = 0; inventorySlotIdx < inventoryPlayerSlotCount; ++inventorySlotIdx) {
                    const int i = inventoryPlayerSlots[inventorySlotIdx];
                    const int inventorySlotCount = getInventorySlotCount(i);
                    for (int slot = 0; slot < inventorySlotCount; ++slot) {
                        if (!inventoryWeaponHandleChanged[i][slot])
                            continue;
                        const uint32_t weaponHandle = inventoryWeaponHandles[i][slot];
                        if (!isValidEntityHandle(weaponHandle) ||
                            !inventoryWeaponEntries[i][slot]) {
                            continue;
                        }
                        const uint32_t handleSlot = weaponHandle & kEntitySlotMask;
                        mem.AddScatterReadRequest(
                            handle,
                            inventoryWeaponEntries[i][slot] +
                                s_activeEntitySlotSize * handleSlot,
                            &inventoryWeapons[i][slot],
                            sizeof(uintptr_t));
                        queuedInventoryEntryReads = true;
                    }
                }
                if (queueInventoryWeaponMetadata(
                        s_cachedInventoryWeaponsResolved,
                        true))
                    queuedInventoryEntryReads = true;
                if (queuedInventoryEntryReads) {
                    if (!mem.ExecuteReadScatter(handle)) {
                        fullInventoryReadFailed = true;
                        weaponEntryFailures = std::max(weaponEntryFailures, 1);
                        memcpy(inventoryWeaponEntries, s_cachedInventoryWeaponEntries, sizeof(inventoryWeaponEntries));
                        memcpy(inventoryWeapons, s_cachedInventoryWeaponsResolved, sizeof(inventoryWeapons));
                        memcpy(inventoryWeaponIds, s_cachedInventoryWeaponIdsResolved, sizeof(inventoryWeaponIds));
                        memcpy(inventoryHasBombBySlot, s_cachedInventoryHasBombResolved, sizeof(inventoryHasBombBySlot));
                        logUpdateDataIssue("scatter_13_inv", "inventory_entries_failed_using_cached");
                    } else {
                        for (uint32_t block = 0; block < EntityBlockCache::kBlockCount; ++block) {
                            if (missingEntityBlocks[block] &&
                                !isLikelyGamePointer(s_entityBlockCache.Get(block))) {
                                s_entityBlockCache.Invalidate(block);
                            }
                        }
                        for (int inventorySlotIdx = 0;
                             inventorySlotIdx < inventoryPlayerSlotCount;
                             ++inventorySlotIdx) {
                            const int i = inventoryPlayerSlots[inventorySlotIdx];
                            const int inventorySlotCount = getInventorySlotCount(i);
                            for (int slot = 0; slot < inventorySlotCount; ++slot) {
                                if (!inventoryWeaponHandleChanged[i][slot] ||
                                    inventoryWeaponEntries[i][slot]) {
                                    continue;
                                }
                                const uint32_t weaponHandle = inventoryWeaponHandles[i][slot];
                                if (!isValidEntityHandle(weaponHandle))
                                    continue;
                                inventoryWeaponEntries[i][slot] =
                                    s_entityBlockCache.Get(
                                        (weaponHandle & kEntityHandleMask) >> 9);
                            }
                        }

                        bool inventoryEntriesChanged = false;
                        for (int inventorySlotIdx = 0; inventorySlotIdx < inventoryPlayerSlotCount && !inventoryEntriesChanged; ++inventorySlotIdx) {
                            const int i = inventoryPlayerSlots[inventorySlotIdx];
                            const int inventorySlotCount = getInventorySlotCount(i);
                            for (int slot = 0; slot < inventorySlotCount; ++slot) {
                                if (inventoryWeaponEntries[i][slot] != s_cachedInventoryWeaponEntries[i][slot]) {
                                    inventoryEntriesChanged = true;
                                    break;
                                }
                            }
                        }

                        bool inventoryEntityRefreshFailed = false;
                        if (inventoryEntriesChanged) {
                            
                            memcpy(inventoryWeapons, s_cachedInventoryWeaponsResolved, sizeof(inventoryWeapons));
                            bool queuedInventoryEntityRefresh = false;
                            for (int inventorySlotIdx = 0; inventorySlotIdx < inventoryPlayerSlotCount; ++inventorySlotIdx) {
                                const int i = inventoryPlayerSlots[inventorySlotIdx];
                                const int inventorySlotCount = getInventorySlotCount(i);
                                for (int slot = 0; slot < inventorySlotCount; ++slot) {
                                    const uint32_t weaponHandle = inventoryWeaponHandles[i][slot];
                                    if (!isValidEntityHandle(weaponHandle) || !inventoryWeaponEntries[i][slot])
                                        continue;
                                    const uint32_t handleSlot = weaponHandle & kEntitySlotMask;
                                    mem.AddScatterReadRequest(
                                        handle,
                                        inventoryWeaponEntries[i][slot] + s_activeEntitySlotSize * handleSlot,
                                        &inventoryWeapons[i][slot],
                                        sizeof(uintptr_t));
                                    queuedInventoryEntityRefresh = true;
                                }
                            }
                            if (queuedInventoryEntityRefresh && !mem.ExecuteReadScatter(handle)) {
                                fullInventoryReadFailed = true;
                                inventoryEntityRefreshFailed = true;
                                weaponEntityFailures = std::max(weaponEntityFailures, 1);
                                memcpy(inventoryWeapons, s_cachedInventoryWeaponsResolved, sizeof(inventoryWeapons));
                                memcpy(inventoryWeaponIds, s_cachedInventoryWeaponIdsResolved, sizeof(inventoryWeaponIds));
                                memcpy(inventoryHasBombBySlot, s_cachedInventoryHasBombResolved, sizeof(inventoryHasBombBySlot));
                                logUpdateDataIssue("scatter_14_inv_refresh", "inventory_entity_refresh_failed_using_cached");
                            }
                        }
                        if (!inventoryEntityRefreshFailed) {
                        for (int inventorySlotIdx = 0; inventorySlotIdx < inventoryPlayerSlotCount; ++inventorySlotIdx) {
                            const int i = inventoryPlayerSlots[inventorySlotIdx];
                            const int inventorySlotCount = getInventorySlotCount(i);
                            for (int slot = 0; slot < inventorySlotCount; ++slot) {
                                if (inventoryWeaponEntries[i][slot] && !isLikelyGamePointer(inventoryWeaponEntries[i][slot]))
                                    inventoryWeaponEntries[i][slot] = 0;
                                if (inventoryWeapons[i][slot] && !isLikelyGamePointer(inventoryWeapons[i][slot])) {
                                    inventoryWeapons[i][slot] = 0;
                                    const uint32_t weaponHandle = inventoryWeaponHandles[i][slot];
                                    if (isValidEntityHandle(weaponHandle)) {
                                        s_entityBlockCache.Invalidate(
                                            (weaponHandle & kEntityHandleMask) >> 9);
                                    }
                                    inventoryWeaponEntries[i][slot] = 0;
                                }
                            }
                        }

                        
                        bool inventoryEntitiesChanged =
                            inventoryEntriesChanged ||
                            inventoryHandlesChanged;
                        for (int inventorySlotIdx = 0;
                             inventorySlotIdx < inventoryPlayerSlotCount;
                             ++inventorySlotIdx) {
                            const int i = inventoryPlayerSlots[inventorySlotIdx];
                            const int inventorySlotCount = getInventorySlotCount(i);
                            for (int slot = 0; slot < inventorySlotCount; ++slot) {
                                if (inventoryWeapons[i][slot] !=
                                    s_cachedInventoryWeaponsResolved[i][slot]) {
                                    inventoryEntitiesChanged = true;
                                }
                            }
                        }

                        if (inventoryEntitiesChanged) {
                            for (int inventorySlotIdx = 0;
                                 inventorySlotIdx < inventoryPlayerSlotCount;
                                 ++inventorySlotIdx) {
                                const int i = inventoryPlayerSlots[inventorySlotIdx];
                                const int inventorySlotCount = getInventorySlotCount(i);
                                for (int slot = 0; slot < inventorySlotCount; ++slot) {
                                    if (esp::data::ShouldInvalidateInventoryMetadata(
                                            s_cachedInventoryWeaponHandlesResolved[i][slot],
                                            inventoryWeaponHandles[i][slot],
                                            s_cachedInventoryWeaponsResolved[i][slot],
                                            inventoryWeapons[i][slot])) {
                                        inventoryWeaponIds[i][slot] = 0;
                                    }
                                }
                            }
                        }

                        for (int inventorySlotIdx = 0; inventorySlotIdx < inventoryPlayerSlotCount; ++inventorySlotIdx) {
                            const int i = inventoryPlayerSlots[inventorySlotIdx];
                            memcpy(s_cachedInventoryWeaponEntries[i], inventoryWeaponEntries[i], sizeof(s_cachedInventoryWeaponEntries[i]));
                            memcpy(s_cachedInventoryWeaponsResolved[i], inventoryWeapons[i], sizeof(s_cachedInventoryWeaponsResolved[i]));
                        }

                        
                        if (inventoryEntitiesChanged) {
                            const bool queuedInventoryMetaReads =
                                queueInventoryWeaponMetadata(
                                    inventoryWeapons,
                                    true);
                            if (queuedInventoryMetaReads && !mem.ExecuteReadScatter(handle)) {
                                fullInventoryReadFailed = true;
                                weaponMetaFailures = std::max(weaponMetaFailures, 1);
                                memcpy(inventoryWeaponIds, s_cachedInventoryWeaponIdsResolved, sizeof(inventoryWeaponIds));
                                for (int inventorySlotIdx = 0;
                                     inventorySlotIdx < inventoryPlayerSlotCount;
                                     ++inventorySlotIdx) {
                                    const int i = inventoryPlayerSlots[inventorySlotIdx];
                                    const int inventorySlotCount =
                                        getInventorySlotCount(i);
                                    for (int slot = 0;
                                         slot < inventorySlotCount;
                                         ++slot) {
                                        if (esp::data::ShouldInvalidateInventoryMetadata(
                                                s_cachedInventoryWeaponHandlesResolved[i][slot],
                                                inventoryWeaponHandles[i][slot],
                                                s_cachedInventoryWeaponsResolved[i][slot],
                                                inventoryWeapons[i][slot])) {
                                            inventoryWeaponIds[i][slot] = 0;
                                        }
                                    }
                                }
                                logUpdateDataIssue("scatter_15_inv", "inventory_meta_failed_using_cached");
                            }
                        }

                        recomputeInventoryBombOwnership();

                        for (int inventorySlotIdx = 0; inventorySlotIdx < inventoryPlayerSlotCount; ++inventorySlotIdx)
                            storeInventoryFullSlotToCache(inventoryPlayerSlots[inventorySlotIdx]);
                        if (inventoryEntitiesChanged)
                            s_lastInventoryMetaRefreshUs = inventoryNowUs;
                        }
                    }
                } else {
                    recomputeInventoryBombOwnership();
                    for (int inventorySlotIdx = 0;
                         inventorySlotIdx < inventoryPlayerSlotCount;
                         ++inventorySlotIdx) {
                        storeInventoryFullSlotToCache(
                            inventoryPlayerSlots[inventorySlotIdx]);
                    }
                }
                }

                const bool inventoryMetaRetryDue =
                    !inventoryHandlesChanged &&
                    esp::data::IsInventoryMetadataRetryDue(
                        wantsEspBombInfo ||
                            wantsRadarShowBomb ||
                            webRadarDemandActive,
                        hasMissingInventoryMetadata(),
                        s_lastInventoryMetaRefreshUs,
                        inventoryNowUs,
                        esp::intervals::kInventoryMetadataRetryUs);
                if (inventoryMetaRetryDue) {
                    const bool queuedMetaOnlyReads =
                        queueInventoryWeaponMetadata(
                            s_cachedInventoryWeaponsResolved,
                            true);
                    if (queuedMetaOnlyReads && mem.ExecuteReadScatter(handle)) {
                        recomputeInventoryBombOwnership();
                        for (int inventorySlotIdx = 0; inventorySlotIdx < inventoryPlayerSlotCount; ++inventorySlotIdx)
                            storeInventoryFullSlotToCache(inventoryPlayerSlots[inventorySlotIdx]);
                        s_lastInventoryMetaRefreshUs = inventoryNowUs;
                    }
                }
            }
        }
        if (!fullInventoryReadFailed) {
            fullInventoryC4CoverageComplete = inventoryPlayerSlotCount > 0;
            for (int inventorySlotIdx = 0;
                 inventorySlotIdx < inventoryPlayerSlotCount;
                 ++inventorySlotIdx) {
                const int i = inventoryPlayerSlots[inventorySlotIdx];
                if (!pawns[i] || healths[i] <= 0 || lifeStates[i] != 0 || teams[i] != 2)
                    continue;
                const int inventorySlotCount = getInventorySlotCount(i);
                bool playerCoverageComplete =
                    esp::data::IsInventoryPlayerCoverageComplete(
                        weaponServices[i] != 0,
                        inventoryCountBytesRead[i],
                        inventoryArrayBytesRead[i],
                        inventorySlotCount,
                        isLikelyGamePointer(inventoryWeaponHandleArrays[i]));
                for (int slot = 0;
                     playerCoverageComplete && slot < inventorySlotCount;
                     ++slot) {
                    const bool handleValid =
                        isValidEntityHandle(inventoryWeaponHandles[i][slot]);
                    if (!esp::data::IsInventoryWeaponCoverageComplete(
                            inventoryHandleBytesRead[i][slot],
                            handleValid,
                            isLikelyGamePointer(inventoryWeapons[i][slot]),
                            inventoryWeaponIds[i][slot])) {
                        playerCoverageComplete = false;
                    }
                }
                fullInventoryPlayerCoverageComplete[i] = playerCoverageComplete;
                if (!playerCoverageComplete)
                    fullInventoryC4CoverageComplete = false;
            }
        }
        s_lastFullInventoryLaneUs = inventoryNowUs;
    }
