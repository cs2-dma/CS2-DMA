    
    
    
    
    static uintptr_t s_cachedSceneNodes[64] = {};
    static uintptr_t s_cachedBoneArrays[64] = {};
    static uintptr_t s_cachedBonePawns[64] = {};
    static uint32_t s_cachedBonePawnHandles[64] = {};
    struct CachedHitboxDefinition
    {
        Vector3 mins = {};
        Vector3 maxs = {};
        float radius = 0.0f;
        int16_t bone = -1;
        uint8_t index = 0;
        uint8_t hitgroup = 0;
    };
    static uintptr_t s_cachedHitboxSceneNodes[64] = {};
    static uintptr_t s_cachedHitboxModelHandles[64] = {};
    static CachedHitboxDefinition
        s_cachedHitboxDefinitions[64][esp::kMaximumPlayerHitboxes] = {};
    static uint8_t s_cachedHitboxDefinitionCounts[64] = {};
    static uint64_t s_lastHitboxDefinitionQueryUs[64] = {};
    static uint64_t s_lastBoneSlotReadUs[64] = {};
    static uint64_t s_lastBonePointerValidationUs[64] = {};
    static uint64_t s_boneCacheResetSerial = 0;
    static int s_boneReadCursor = 0;
    static uint8_t s_sceneNodeZeroStreak[64] = {};
    static uint8_t s_boneArrayZeroStreak[64] = {};
    static uint32_t s_perSlotBoneStaleStreak[64] = {};
    static uint8_t s_perSlotBoneStaleEscalation[64] = {};
    static uint64_t s_lastPerSlotBoneLocalResetUs[64] = {};
    static bool s_poseRejected[64] = {};
    static uint64_t s_lastPoseRejectLogUs[64] = {};
    static uint64_t s_lastPerSlotBoneRefreshUs = 0;
    static thread_local esp::data::BoneReadBatch s_boneReadBatches[64] = {};
    if (wantsEspSkeleton) {
        const uint64_t boneSubsystemNowUs = TickNowUs();
        auto clearBoneCacheSlot = [&](int i, bool clearModel = false) {
            if (i < 0 || i >= 64)
                return;
            s_cachedSceneNodes[i] = 0;
            s_cachedBoneArrays[i] = 0;
            s_lastBonePointerValidationUs[i] = 0;
            // Keep this batch's anchor through commit even when requesting
            // a pointer retry; otherwise an invalid pose could be rechecked
            // against an unrelated old origin and accidentally accepted.
            s_sceneNodeZeroStreak[i] = 0;
            s_boneArrayZeroStreak[i] = 0;
            // A pose retry is not a model change. Preserve static metadata and
            // its failure cooldown instead of starting N serial query chains.
            if (clearModel) {
                s_cachedHitboxSceneNodes[i] = 0;
                s_cachedHitboxModelHandles[i] = 0;
                s_cachedHitboxDefinitionCounts[i] = 0;
                s_lastHitboxDefinitionQueryUs[i] = 0;
                memset(s_cachedHitboxDefinitions[i], 0, sizeof(s_cachedHitboxDefinitions[i]));
            }
        };
        auto clearResolvedBoneSlot = [&](int i) {
            hasBoneData[i] = false;
            boneSampleTimeUs[i] = 0;
            sceneNodes[i] = 0;
            for (int b = 0; b < esp::kPlayerStoredBoneCount; ++b) {
                allBones[i][b] = {};
            }
            hasHitboxData[i] = false;
            hitboxCounts[i] = 0;
            hitboxSampleTimeUs[i] = 0;
            memset(allHitboxes[i], 0, sizeof(allHitboxes[i]));
        };
        auto copyCommittedBoneSlot = [&](int i, const esp::PlayerData& committed) -> bool {
            if (i < 0 || i >= 64 ||
                !committed.valid ||
                !committed.hasBones ||
                committed.pawn == 0 ||
                committed.pawn != pawns[i] || committed.pawnHandle != pawnHandles[i] ||
                !esp::data::IsReusableBoneSample(committed.bonesUpdatedAtUs, boneSubsystemNowUs, 850000u)) {
                return false;
            }
            hasBoneData[i] = true;
            boneSampleTimeUs[i] = committed.bonesUpdatedAtUs;
            if (committed.boneAnchorValid)
                poseAnchors[i] = {committed.pawn, committed.bonesUpdatedAtUs, committed.boneAnchorPosition};
            sceneNodes[i] = s_cachedSceneNodes[i];
            for (int b = 0; b < esp::kPlayerStoredBoneCount; ++b)
                allBones[i][b] = committed.bones[b];
            hasHitboxData[i] = committed.hasHitboxes;
            hitboxCounts[i] = committed.hitboxCount;
            hitboxSampleTimeUs[i] = committed.hitboxesUpdatedAtUs;
            if (committed.hasHitboxes) {
                memcpy(
                    allHitboxes[i],
                    committed.hitboxes,
                    sizeof(committed.hitboxes));
            } else {
                memset(allHitboxes[i], 0, sizeof(allHitboxes[i]));
            }
            return true;
        };
        auto restoreCommittedBoneSlot = [&](int i) -> bool {
            if (copyCommittedBoneSlot(i, s_players[i]))
                return true;
            return copyCommittedBoneSlot(i, s_prevPlayers[i]);
        };
        auto hasReusableCommittedBoneSlot = [&](int i) -> bool {
            if (i < 0 || i >= 64 || !pawns[i])
                return false;
            const esp::PlayerData& current = s_players[i];
            if (current.valid && current.hasBones && current.pawn == pawns[i] &&
                current.pawnHandle == pawnHandles[i])
                return true;
            const esp::PlayerData& previous = s_prevPlayers[i];
            return previous.valid && previous.hasBones && previous.pawn == pawns[i] &&
                previous.pawnHandle == pawnHandles[i];
        };

        {
            const uint64_t boneResetSerial = s_sceneResetSerial.load(std::memory_order_relaxed);
            if (s_boneCacheResetSerial != boneResetSerial) {
                s_boneCacheResetSerial = boneResetSerial;
                s_boneReadCursor = 0;
                memset(s_cachedSceneNodes, 0, sizeof(s_cachedSceneNodes));
                memset(s_cachedBoneArrays, 0, sizeof(s_cachedBoneArrays));
                memset(s_cachedBonePawns, 0, sizeof(s_cachedBonePawns));
                memset(s_cachedBonePawnHandles, 0, sizeof(s_cachedBonePawnHandles));
                memset(s_cachedHitboxSceneNodes, 0, sizeof(s_cachedHitboxSceneNodes));
                memset(s_cachedHitboxModelHandles, 0, sizeof(s_cachedHitboxModelHandles));
                memset(s_cachedHitboxDefinitions, 0, sizeof(s_cachedHitboxDefinitions));
                memset(s_cachedHitboxDefinitionCounts, 0, sizeof(s_cachedHitboxDefinitionCounts));
                memset(s_lastHitboxDefinitionQueryUs, 0, sizeof(s_lastHitboxDefinitionQueryUs));
                memset(s_lastBoneSlotReadUs, 0, sizeof(s_lastBoneSlotReadUs));
                memset(
                    s_lastBonePointerValidationUs,
                    0,
                    sizeof(s_lastBonePointerValidationUs));
                memset(s_sceneNodeZeroStreak, 0, sizeof(s_sceneNodeZeroStreak));
                memset(s_boneArrayZeroStreak, 0, sizeof(s_boneArrayZeroStreak));
                memset(s_perSlotBoneStaleStreak, 0, sizeof(s_perSlotBoneStaleStreak));
                memset(s_perSlotBoneStaleEscalation, 0, sizeof(s_perSlotBoneStaleEscalation));
                memset(s_lastPerSlotBoneLocalResetUs, 0, sizeof(s_lastPerSlotBoneLocalResetUs));
                memset(s_poseRejected, 0, sizeof(s_poseRejected));
                memset(s_lastPoseRejectLogUs, 0, sizeof(s_lastPoseRejectLogUs));
                for (int slot = 0; slot < 64; ++slot)
                    poseAnchors[slot] = {};
                s_lastPerSlotBoneRefreshUs = 0;
            }
        }

        for (int i = 0; i < 64; ++i) {
            if (pawns[i] == s_cachedBonePawns[i] && pawnHandles[i] == s_cachedBonePawnHandles[i])
                continue;
            s_cachedBonePawns[i] = pawns[i];
            s_cachedBonePawnHandles[i] = pawnHandles[i];
            s_poseRejected[i] = false;
            clearBoneCacheSlot(i, true);
            poseAnchors[i] = {};
            s_lastBoneSlotReadUs[i] = 0;
            s_perSlotBoneStaleStreak[i] = 0;
            s_perSlotBoneStaleEscalation[i] = 0;
            s_lastPerSlotBoneLocalResetUs[i] = 0;
        }

        const auto hitgroupFromHitbox = [](int hitbox) -> uint8_t {
            switch (hitbox) {
            case 0:
            case 1: return 1;
            case 2:
            case 3:
            case 6: return 3;
            case 4:
            case 5: return 2;
            case 7:
            case 9:
            case 11: return 4;
            case 8:
            case 10:
            case 12: return 5;
            case 13:
            case 15:
            case 17: return 6;
            case 14:
            case 16:
            case 18: return 7;
            default: return 2;
            }
        };
        auto readHitboxValue = [&](uintptr_t address, void* output, size_t size) {
            return address != 0 && output != nullptr && size != 0 &&
                mem.Read(address, output, size);
        };
        int modelQueriesRemaining = 1;
        auto queryHitboxDefinitions = [&](int slot, uintptr_t sceneNode, uintptr_t modelHandle) -> bool {
            if (slot < 0 || slot >= 64 || !isLikelyGamePointer(sceneNode) ||
                !isLikelyGamePointer(modelHandle))
                return false;
            if (s_cachedHitboxSceneNodes[slot] == sceneNode &&
                s_cachedHitboxModelHandles[slot] == modelHandle &&
                s_cachedHitboxDefinitionCounts[slot] > 0) {
                return true;
            }
            if (s_lastHitboxDefinitionQueryUs[slot] != 0 &&
                boneSubsystemNowUs >= s_lastHitboxDefinitionQueryUs[slot] &&
                boneSubsystemNowUs - s_lastHitboxDefinitionQueryUs[slot] < 250000u) {
                return false;
            }
            if (modelQueriesRemaining == 0)
                return false;
            --modelQueriesRemaining;
            s_lastHitboxDefinitionQueryUs[slot] = boneSubsystemNowUs;

            uintptr_t model = 0;
            uintptr_t renderMeshHolder = 0;
            uintptr_t renderMeshes = 0;
            uintptr_t hitboxData = 0;
            uintptr_t hitboxArray = 0;
            if (!readHitboxValue(modelHandle, &model, sizeof(model)) ||
                !isLikelyGamePointer(model) ||
                !readHitboxValue(model + 0x78u, &renderMeshHolder, sizeof(renderMeshHolder)) ||
                !isLikelyGamePointer(renderMeshHolder) ||
                !readHitboxValue(renderMeshHolder, &renderMeshes, sizeof(renderMeshes)) ||
                !isLikelyGamePointer(renderMeshes) ||
                !readHitboxValue(renderMeshes + 0x168u, &hitboxData, sizeof(hitboxData)) ||
                !isLikelyGamePointer(hitboxData)) {
                return false;
            }

            int count = 0;
            if (!readHitboxValue(hitboxData + 0x28u, &count, sizeof(count)) ||
                count <= 0 || count > esp::kMaximumPlayerHitboxes ||
                !readHitboxValue(hitboxData + 0x30u, &hitboxArray, sizeof(hitboxArray)) ||
                !isLikelyGamePointer(hitboxArray)) {
                return false;
            }

            int remapCount = 0;
            uintptr_t remapTable = 0;
            uintptr_t meshA = 0;
            uintptr_t meshB = 0;
            if (!readHitboxValue(model + 0x220u, &remapCount, sizeof(remapCount)) ||
                !readHitboxValue(model + 0x228u, &remapTable, sizeof(remapTable)) ||
                !readHitboxValue(model + 0x240u, &meshA, sizeof(meshA)) ||
                !readHitboxValue(model + 0x2F0u, &meshB, sizeof(meshB)) ||
                remapCount <= 0 || remapCount > 4096 ||
                !isLikelyGamePointer(remapTable) ||
                !isLikelyGamePointer(meshA) || !isLikelyGamePointer(meshB)) {
                return false;
            }

            const int remapToRead = std::min(remapCount, 512);
            std::array<int16_t, 512> remap = {};
            uint16_t offsetA = 0;
            uint16_t offsetB = 0;
            if (!readHitboxValue(
                    remapTable,
                    remap.data(),
                    static_cast<size_t>(remapToRead) * sizeof(int16_t)) ||
                !readHitboxValue(meshA, &offsetA, sizeof(offsetA)) ||
                !readHitboxValue(meshB, &offsetB, sizeof(offsetB))) {
                return false;
            }

            constexpr size_t kHitboxStride = 0x70u;
            std::array<std::byte,
                esp::kMaximumPlayerHitboxes * kHitboxStride> raw = {};
            if (!readHitboxValue(
                    hitboxArray,
                    raw.data(),
                    static_cast<size_t>(count) * kHitboxStride)) {
                return false;
            }

            CachedHitboxDefinition resolved[esp::kMaximumPlayerHitboxes] = {};
            uint8_t resolvedCount = 0;
            for (int index = 0; index < count; ++index) {
                const std::byte* entry =
                    raw.data() + static_cast<size_t>(index) * kHitboxStride;
                uint16_t remapIndex = 0;
                Vector3 mins = {};
                Vector3 maxs = {};
                float radius = 0.0f;
                memcpy(&mins, entry + 0x18u, sizeof(mins));
                memcpy(&maxs, entry + 0x24u, sizeof(maxs));
                memcpy(&radius, entry + 0x30u, sizeof(radius));
                memcpy(&remapIndex, entry + 0x48u, sizeof(remapIndex));
                const size_t remapSlot = static_cast<size_t>(remapIndex) +
                    static_cast<size_t>(offsetA) + static_cast<size_t>(offsetB);
                if (remapSlot >= static_cast<size_t>(remapToRead))
                    continue;
                const int bone = remap[remapSlot];
                if (bone < esp::data::kBoneReadFirst ||
                    bone > esp::data::kBoneReadLast ||
                    !IsFiniteVec(mins) || !IsFiniteVec(maxs) ||
                    !std::isfinite(radius) || radius < 0.0f || radius > 100.0f ||
                    std::fabs(mins.x) > 256.0f || std::fabs(mins.y) > 256.0f ||
                    std::fabs(mins.z) > 256.0f || std::fabs(maxs.x) > 256.0f ||
                    std::fabs(maxs.y) > 256.0f || std::fabs(maxs.z) > 256.0f) {
                    continue;
                }
                CachedHitboxDefinition& definition = resolved[resolvedCount++];
                definition.mins = mins;
                definition.maxs = maxs;
                definition.radius = radius;
                definition.bone = static_cast<int16_t>(bone);
                definition.index = static_cast<uint8_t>(index);
                definition.hitgroup = hitgroupFromHitbox(index);
            }
            if (resolvedCount == 0)
                return false;
            s_cachedHitboxSceneNodes[slot] = sceneNode;
            s_cachedHitboxModelHandles[slot] = modelHandle;
            s_cachedHitboxDefinitionCounts[slot] = resolvedCount;
            memcpy(
                s_cachedHitboxDefinitions[slot],
                resolved,
                sizeof(resolved));
            return true;
        };
        auto unpackHitboxCapsules = [&](int slot) -> bool {
            if (slot < 0 || slot >= 64 ||
                s_cachedHitboxDefinitionCounts[slot] == 0) {
                return false;
            }
            memset(allHitboxes[slot], 0, sizeof(allHitboxes[slot]));
            uint8_t count = 0;
            for (uint8_t index = 0;
                 index < s_cachedHitboxDefinitionCounts[slot] &&
                 count < esp::kMaximumPlayerHitboxes;
                 ++index) {
                const CachedHitboxDefinition& definition =
                    s_cachedHitboxDefinitions[slot][index];
                esp::data::BoneTransform transform = {};
                if (!esp::data::UnpackBoneTransform(
                        s_boneReadBatches[slot],
                        definition.bone,
                        transform)) {
                    continue;
                }
                const Vector3 centerLocal =
                    (definition.mins + definition.maxs) * 0.5f;
                const Vector3 halfExtent =
                    (definition.maxs - definition.mins) * 0.5f;
                const float halfX = std::fabs(halfExtent.x);
                const float halfY = std::fabs(halfExtent.y);
                const float halfZ = std::fabs(halfExtent.z);
                const float longest = std::max({halfX, halfY, halfZ});
                Vector3 axisLocal = {};
                if (halfX >= halfY && halfX >= halfZ)
                    axisLocal.x = longest;
                else if (halfY >= halfZ)
                    axisLocal.y = longest;
                else
                    axisLocal.z = longest;
                const Vector3 center = transform.position +
                    esp::data::RotateByQuaternion(
                        transform.rotation,
                        centerLocal);
                const Vector3 axisWorld =
                    esp::data::RotateByQuaternion(
                        transform.rotation,
                        axisLocal);
                const Vector3 start = center - axisWorld;
                const Vector3 end = center + axisWorld;
                if (!isValidWorldPos(start) || !isValidWorldPos(end) ||
                    !isValidWorldPos(center)) {
                    continue;
                }
                esp::HitboxCapsule& capsule = allHitboxes[slot][count++];
                capsule.start = start;
                capsule.end = end;
                capsule.center = center;
                capsule.radius = definition.radius;
                capsule.bone = definition.bone;
                capsule.index = definition.index;
                capsule.hitgroup = definition.hitgroup;
                capsule.valid = true;
            }
            hitboxCounts[slot] = count;
            hasHitboxData[slot] = count > 0;
            hitboxSampleTimeUs[slot] = count > 0 ? boneSampleTimeUs[slot] : 0;
            return count > 0;
        };

        for (int i = 0; i < 64; ++i) {
            // Core commit precedes this lane: s_players already contains THIS
            // frame. Comparing it to healths/positions could never see a respawn.
            if (esp::data::HasBoneCoreDiscontinuity(s_prevPlayers[i], pawns[i],
                    pawnHandles[i], healths[i], lifeStates[i], positions[i])) {
                clearBoneCacheSlot(i);
                poseAnchors[i] = {};
                s_perSlotBoneStaleStreak[i] = 0;
                s_perSlotBoneStaleEscalation[i] = 0;
                s_lastPerSlotBoneLocalResetUs[i] = 0;
                s_lastBoneSlotReadUs[i] = 0;
                clearResolvedBoneSlot(i);
            }
        }

        int eligibleBoneSlots[64] = {};
        int eligibleBoneSlotCount = 0;
        const int boneFilterLocalTeam = s_localTeam;
        const bool boneFilterTeamConfirmed =
            localTeamLiveResolved ||
            localControllerTeam == boneFilterLocalTeam;
        const bool filterTeammatesForBones =
            !wantsEspShowTeammates &&
            (boneFilterLocalTeam == 2 || boneFilterLocalTeam == 3) &&
            boneFilterTeamConfirmed &&
            !localTeamLikelySwitched;
        auto isBoneSlotLive = [&](int slot) {
            const bool liveCore =
                pawns[slot] != 0 &&
                healths[slot] > 0 &&
                lifeStates[slot] == 0;
            const bool cachedCore =
                s_players[slot].valid &&
                s_players[slot].pawn == pawns[slot] &&
                s_players[slot].health > 0;
            return esp::data::IsBoneSlotLive(liveCore, cachedCore);
        };
        for (int resolvedIdx = 0; resolvedIdx < playerResolvedSlotCount; ++resolvedIdx) {
            const int i = playerResolvedSlots[resolvedIdx];
            const bool localBoneSlot =
                (s_localPawn != 0 && pawns[i] == s_localPawn) ||
                (s_localPlayerIndex >= 0 &&
                 s_localPlayerIndex < 64 &&
                 i == s_localPlayerIndex);
            if (localBoneSlot)
                continue;
            if (!isBoneSlotLive(i)) {
                s_perSlotBoneStaleStreak[i] = 0;
                s_perSlotBoneStaleEscalation[i] = 0;
                continue;
            }
            const bool staleTeamDuringSwitch =
                localTeamLikelySwitched &&
                !liveTeamReads[i];
            const bool liveConfirmedTeammate =
                liveTeamReads[i] &&
                teams[i] == boneFilterLocalTeam;
            const bool committedWouldRenderAsEnemy =
                s_players[i].valid &&
                s_players[i].pawn == pawns[i] &&
                (boneFilterLocalTeam != 2 && boneFilterLocalTeam != 3 ||
                 s_players[i].team != boneFilterLocalTeam);
            if (filterTeammatesForBones &&
                liveConfirmedTeammate &&
                !staleTeamDuringSwitch &&
                !committedWouldRenderAsEnemy) {
                continue;
            }
            eligibleBoneSlots[eligibleBoneSlotCount++] = i;
        }
        const uint64_t boneNowUs = TickNowUs();
        int boneScanSlots[64] = {};
        int boneScanSlotCount = 0;
        const int boneSlotLimit =
            allowScheduledBoneReads
                ? esp::data::SelectBoneReadBatchSlotLimit(eligibleBoneSlotCount)
                : 0;
        int nextBoneCursor = s_boneReadCursor;
        for (int offset = 0;
             offset < eligibleBoneSlotCount &&
             boneScanSlotCount < boneSlotLimit;
             ++offset) {
            const int eligibleIndex =
                (s_boneReadCursor + offset) % eligibleBoneSlotCount;
            const int i = eligibleBoneSlots[eligibleIndex];
            if (!esp::data::IsBoneSlotReadDue(
                    s_lastBoneSlotReadUs[i],
                    boneNowUs,
                    esp::intervals::kBoneReadsUs)) {
                continue;
            }
            boneScanSlots[boneScanSlotCount++] = i;
            nextBoneCursor = (eligibleIndex + 1) % eligibleBoneSlotCount;
        }
        if (boneScanSlotCount > 0)
            s_boneReadCursor = nextBoneCursor;
        _boneReadsActiveTick = boneScanSlotCount > 0;

        if (eligibleBoneSlotCount == 0) {
            s_stageBonePoseSlots.store(0, std::memory_order_relaxed);
            s_stageBonePointerValidationSlots.store(0, std::memory_order_relaxed);
            s_stageBonePoseRanges.store(0, std::memory_order_relaxed);
            s_stageBonePoseBytes.store(0, std::memory_order_relaxed);
            for (int resolvedIdx = 0; resolvedIdx < playerResolvedSlotCount; ++resolvedIdx) {
                const int i = playerResolvedSlots[resolvedIdx];
                // A single incomplete core sample must not erase a coherent
                // pose. Keep the committed pose for the same pawn and clear it
                // only after the slot is no longer live or its identity changed.
                if (isBoneSlotLive(i) && restoreCommittedBoneSlot(i))
                    continue;
                clearResolvedBoneSlot(i);
            }
            MarkSubsystemHealthy(RuntimeSubsystem::Bones, boneSubsystemNowUs);
        } else if (boneScanSlotCount == 0) {
            s_stageBonePoseSlots.store(0, std::memory_order_relaxed);
            s_stageBonePointerValidationSlots.store(0, std::memory_order_relaxed);
            s_stageBonePoseRanges.store(0, std::memory_order_relaxed);
            s_stageBonePoseBytes.store(0, std::memory_order_relaxed);
            for (int eligibleIdx = 0;
                 eligibleIdx < eligibleBoneSlotCount;
                 ++eligibleIdx) {
                const int i = eligibleBoneSlots[eligibleIdx];
                if (!restoreCommittedBoneSlot(i))
                    clearResolvedBoneSlot(i);
            }
            // Skipping a scheduled lane cannot cure a rejected pose.
            bool rejected = false;
            for (int idx = 0; idx < eligibleBoneSlotCount; ++idx)
                rejected = rejected || s_poseRejected[eligibleBoneSlots[idx]];
            if (rejected)
                MarkSubsystemDegraded(RuntimeSubsystem::Bones, boneSubsystemNowUs);
        } else {
            uintptr_t boneArrays[64] = {};
            DWORD sceneNodeBytesRead[64] = {};
            DWORD boneArrayBytesRead[64] = {};
            DWORD poseBytesRead[64] = {};
            Vector3 poseAnchorPositions[64] = {};
            DWORD poseAnchorBytesRead[64] = {};
            uintptr_t poseModelHandles[64] = {};
            DWORD poseModelBytesRead[64] = {};
            bool bonePointerValidationDue[64] = {};
            bool bonePointerValidationSucceeded[64] = {};
            int sceneNodeFailures = 0;
            int boneArrayFailures = 0;
            int boneReadFailures = 0;
            int bonePoseSlotCount = 0;
            int bonePoseRangeCount = 0;
            int bonePointerValidationSlotCount = 0;
            size_t bonePoseBytes = 0;
            bool bonePoseAttempted[64] = {};
            bool prefetchedBonePose[64] = {};
            bool queuedBonePointerReads = false;
            auto queueBonePositionRanges = [&](int slot, uintptr_t boneArray, uintptr_t poseSceneNode) {
                if (slot < 0 || slot >= 64 || !boneArray)
                    return false;
                if (!bonePoseAttempted[slot]) {
                    bonePoseAttempted[slot] = true;
                    ++bonePoseSlotCount;
                }
                esp::data::BoneReadBatch& batch = s_boneReadBatches[slot];
                batch = {};
                poseBytesRead[slot] = 0;
                poseAnchorBytesRead[slot] = 0;
                poseModelBytesRead[slot] = 0;
                poseModelHandles[slot] = 0;
                if (isLikelyGamePointer(poseSceneNode)) {
                    mem.AddScatterReadRequest(handle, poseSceneNode + 0x200u,
                        &poseModelHandles[slot], sizeof(uintptr_t), &poseModelBytesRead[slot]);
                }
                if (isLikelyGamePointer(poseSceneNode) && ofs.CGameSceneNode_m_vecAbsOrigin > 0) {
                    mem.AddScatterReadRequest(handle,
                        poseSceneNode + ofs.CGameSceneNode_m_vecAbsOrigin,
                        &poseAnchorPositions[slot], sizeof(Vector3), &poseAnchorBytesRead[slot]);
                }
                mem.AddScatterReadRequest(
                    handle,
                    boneArray +
                        static_cast<uintptr_t>(
                            esp::data::kBoneReadFirst) *
                            esp::data::kBoneTransformStrideBytes,
                    batch.transforms.data(),
                    batch.transforms.size(),
                    &poseBytesRead[slot]);
                ++bonePoseRangeCount;
                bonePoseBytes += esp::data::kBoneBatchBytesPerPlayer;
                return true;
            };
            auto unpackBonePositionRanges = [&](int slot) {
                esp::data::UnpackStoredBones(
                    s_boneReadBatches[slot],
                    allBones[slot]);
            };

            int stablePointerValidationBudget =
                esp::data::kBonePointerValidationBudgetPerTick;
            for (int scanIdx = 0; scanIdx < boneScanSlotCount; ++scanIdx) {
                const int i = boneScanSlots[scanIdx];
                sceneNodes[i] = s_cachedSceneNodes[i];
                boneArrays[i] = s_cachedBoneArrays[i];
                const bool cacheReady =
                    isLikelyGamePointer(s_cachedSceneNodes[i]) &&
                    isLikelyGamePointer(s_cachedBoneArrays[i]);
                const bool validationDue =
                    esp::data::IsBonePointerValidationDue(
                        s_lastBonePointerValidationUs[i],
                        boneNowUs,
                        cacheReady);
                bonePointerValidationDue[i] =
                    esp::data::SelectBonePointerValidation(
                        validationDue,
                        cacheReady,
                        stablePointerValidationBudget);
                if (!bonePointerValidationDue[i])
                    continue;
                sceneNodes[i] = 0;
                boneArrays[i] = 0;
                if (!pawns[i])
                    continue;
                ++bonePointerValidationSlotCount;
                sceneNodeBytesRead[i] = 0;
                mem.AddScatterReadRequest(
                    handle,
                    pawns[i] + ofs.C_BaseEntity_m_pGameSceneNode,
                    &sceneNodes[i],
                    sizeof(uintptr_t),
                    &sceneNodeBytesRead[i]);
                queuedBonePointerReads = true;
            }
            for (int scanIdx = 0; scanIdx < boneScanSlotCount; ++scanIdx) {
                const int i = boneScanSlots[scanIdx];
                if (!bonePointerValidationDue[i] ||
                    !s_cachedSceneNodes[i])
                    continue;
                boneArrayBytesRead[i] = 0;
                mem.AddScatterReadRequest(
                    handle,
                    s_cachedSceneNodes[i] + ofs.CSkeletonInstance_m_modelState + 0x80,
                    &boneArrays[i],
                    sizeof(uintptr_t),
                    &boneArrayBytesRead[i]);
                queuedBonePointerReads = true;
            }

            // Stable pointer validation and pose reads can share one physical
            // scatter. A changed pointer invalidates only that slot's
            // prefetched pose and is retried below with the new array.
            if (queuedBonePointerReads) {
                for (int scanIdx = 0; scanIdx < boneScanSlotCount; ++scanIdx) {
                    const int i = boneScanSlots[scanIdx];
                    if (!isLikelyGamePointer(s_cachedBoneArrays[i]))
                        continue;
                    if (queueBonePositionRanges(i, s_cachedBoneArrays[i], s_cachedSceneNodes[i]))
                        prefetchedBonePose[i] = true;
                }
            }

            bool pointerScatterSucceeded = true;
            if (queuedBonePointerReads && !mem.ExecuteReadScatter(handle)) {
                pointerScatterSucceeded = false;
                sceneNodeFailures = 1;
                boneArrayFailures = 1;
                for (int scanIdx = 0; scanIdx < boneScanSlotCount; ++scanIdx) {
                    const int i = boneScanSlots[scanIdx];
                    if (!bonePointerValidationDue[i])
                        continue;
                    sceneNodes[i] = s_cachedSceneNodes[i];
                    boneArrays[i] = s_cachedBoneArrays[i];
                    prefetchedBonePose[i] = false;
                }
                logUpdateDataIssue(
                    "scatter_16_pointer",
                    "optional_failed_bone_pointer_validation_using_cached");
            }
            if (pointerScatterSucceeded) {
                for (int scanIdx = 0;
                     scanIdx < boneScanSlotCount;
                     ++scanIdx) {
                    const int i = boneScanSlots[scanIdx];
                    if (bonePointerValidationDue[i]) {
                        const bool sceneNodeReadComplete =
                            esp::data::IsBonePointerReadComplete(
                                sceneNodeBytesRead[i]);
                        const bool cachedBoneArrayReadComplete =
                            !s_cachedSceneNodes[i] ||
                            esp::data::IsBonePointerReadComplete(
                                boneArrayBytesRead[i]);
                        if (!sceneNodeReadComplete) {
                            sceneNodes[i] = 0;
                            ++sceneNodeFailures;
                        }
                        if (!cachedBoneArrayReadComplete) {
                            boneArrays[i] = 0;
                            ++boneArrayFailures;
                        }
                        bonePointerValidationSucceeded[i] =
                            sceneNodeReadComplete &&
                            cachedBoneArrayReadComplete &&
                            isLikelyGamePointer(sceneNodes[i]) &&
                            isLikelyGamePointer(boneArrays[i]);
                    }
                }
            }

            for (int scanIdx = 0; scanIdx < boneScanSlotCount; ++scanIdx) {
                const int i = boneScanSlots[scanIdx];
                if (!isLikelyGamePointer(sceneNodes[i]))
                    sceneNodes[i] = 0;
                if (!sceneNodes[i] && s_cachedSceneNodes[i] && hasReusableCommittedBoneSlot(i)) {
                    if (s_sceneNodeZeroStreak[i] < 0xFFu)
                        ++s_sceneNodeZeroStreak[i];
                    
                    
                    
                    
                    
                    
                    
                    const uint64_t lastBulkEvictUsForBones =
                        s_lastBulkEvictionUs.load(std::memory_order_relaxed);
                    const uint64_t boneRecoveryNowUs = TickNowUs();
                    const bool boneBulkRecovery =
                        esp::data::IsWithinBulkRecoveryStaleWindow(
                            lastBulkEvictUsForBones,
                            boneRecoveryNowUs);
                    const uint8_t sceneNodeHoldStreak =
                        esp::data::SelectSceneNodeHoldStreak(boneBulkRecovery);
                    if (esp::data::ShouldReuseCachedPointerOnZero(
                            s_sceneNodeZeroStreak[i],
                            sceneNodeHoldStreak))
                        sceneNodes[i] = s_cachedSceneNodes[i];
                } else if (sceneNodes[i]) {
                    s_sceneNodeZeroStreak[i] = 0;
                }
            }

            bool sceneNodeDirty[64] = {};
            bool sceneNodesChanged = false;
            for (int scanIdx = 0; scanIdx < boneScanSlotCount; ++scanIdx) {
                const int i = boneScanSlots[scanIdx];
                if (sceneNodes[i] != s_cachedSceneNodes[i]) {
                    sceneNodeDirty[i] = true;
                    sceneNodesChanged = true;
                }
            }
            if (sceneNodesChanged) {
                for (int scanIdx = 0; scanIdx < boneScanSlotCount; ++scanIdx) {
                    const int i = boneScanSlots[scanIdx];
                    if (sceneNodeDirty[i])
                        boneArrays[i] = 0;
                }
                bool queuedSceneNodeRefresh = false;
                for (int scanIdx = 0; scanIdx < boneScanSlotCount; ++scanIdx) {
                    const int i = boneScanSlots[scanIdx];
                    if (!sceneNodeDirty[i] || !sceneNodes[i])
                        continue;
                    boneArrayBytesRead[i] = 0;
                    mem.AddScatterReadRequest(
                        handle,
                        sceneNodes[i] + ofs.CSkeletonInstance_m_modelState + 0x80,
                        &boneArrays[i],
                        sizeof(uintptr_t),
                        &boneArrayBytesRead[i]);
                    queuedSceneNodeRefresh = true;
                }
                if (queuedSceneNodeRefresh && !mem.ExecuteReadScatter(handle)) {
                    boneArrayFailures = 1;
                    for (int scanIdx = 0; scanIdx < boneScanSlotCount; ++scanIdx) {
                        const int i = boneScanSlots[scanIdx];
                        if (sceneNodeDirty[i]) {
                            boneArrays[i] = 0;
                            bonePointerValidationSucceeded[i] = false;
                        }
                    }
                    logUpdateDataIssue("scatter_16_refresh", "optional_failed_scene_node_bone_array_refresh");
                } else if (queuedSceneNodeRefresh) {
                    for (int scanIdx = 0; scanIdx < boneScanSlotCount; ++scanIdx) {
                        const int i = boneScanSlots[scanIdx];
                        if (sceneNodeDirty[i] &&
                            bonePointerValidationDue[i]) {
                            const bool boneArrayReadComplete =
                                esp::data::IsBonePointerReadComplete(
                                    boneArrayBytesRead[i]);
                            if (!boneArrayReadComplete) {
                                boneArrays[i] = 0;
                                ++boneArrayFailures;
                            }
                            bonePointerValidationSucceeded[i] =
                                boneArrayReadComplete &&
                                isLikelyGamePointer(sceneNodes[i]) &&
                                isLikelyGamePointer(boneArrays[i]);
                        }
                    }
                }
            }

            for (int scanIdx = 0; scanIdx < boneScanSlotCount; ++scanIdx) {
                const int i = boneScanSlots[scanIdx];
                if (!isLikelyGamePointer(boneArrays[i]))
                    boneArrays[i] = 0;
                if (!boneArrays[i] &&
                    s_cachedBoneArrays[i] &&
                    !sceneNodeDirty[i] &&
                    hasReusableCommittedBoneSlot(i)) {
                    if (s_boneArrayZeroStreak[i] < 0xFFu)
                        ++s_boneArrayZeroStreak[i];
                    const uint64_t baBulkUs = s_lastBulkEvictionUs.load(std::memory_order_relaxed);
                    const uint64_t baNowUs = TickNowUs();
                    const bool baBulkRecovery =
                        esp::data::IsWithinBulkRecoveryStaleWindow(baBulkUs, baNowUs);
                    const uint8_t boneArrayHoldStreak =
                        esp::data::SelectBoneArrayHoldStreak(baBulkRecovery);
                    if (esp::data::ShouldReuseCachedPointerOnZero(
                            s_boneArrayZeroStreak[i],
                            boneArrayHoldStreak))
                        boneArrays[i] = s_cachedBoneArrays[i];
                } else if (boneArrays[i]) {
                    s_boneArrayZeroStreak[i] = 0;
                }
            }

            bool queuedBonePoseReads = false;
            for (int scanIdx = 0; scanIdx < boneScanSlotCount; ++scanIdx) {
                const int i = boneScanSlots[scanIdx];
                if (!boneArrays[i])
                    continue;
                const bool prefetchedPoseStillValid =
                    prefetchedBonePose[i] &&
                    pointerScatterSucceeded &&
                    !sceneNodeDirty[i] &&
                    boneArrays[i] == s_cachedBoneArrays[i] &&
                    esp::data::IsBonePoseReadComplete(poseBytesRead[i]);
                if (prefetchedPoseStillValid)
                    continue;
                prefetchedBonePose[i] = false;
                if (queueBonePositionRanges(i, boneArrays[i], sceneNodes[i]))
                    queuedBonePoseReads = true;
            }
            bool finalPoseScatterSucceeded = true;
            if (queuedBonePoseReads) {
                if (!mem.ExecuteReadScatter(handle)) {
                    finalPoseScatterSucceeded = false;
                    boneReadFailures = 1;
                    for (int scanIdx = 0;
                         scanIdx < boneScanSlotCount;
                         ++scanIdx) {
                        const int i = boneScanSlots[scanIdx];
                        if (!prefetchedBonePose[i] &&
                            !restoreCommittedBoneSlot(i))
                            clearResolvedBoneSlot(i);
                    }
                    logUpdateDataIssue(
                        "scatter_17_pose",
                        "optional_failed_bone_positions_using_committed");
                }
            }
            for (int scanIdx = 0;
                 scanIdx < boneScanSlotCount;
                 ++scanIdx) {
                const int i = boneScanSlots[scanIdx];
                const bool poseReadComplete =
                    boneArrays[i] &&
                    bonePointerValidationSucceeded[i] &&
                    (prefetchedBonePose[i] ||
                     finalPoseScatterSucceeded) &&
                    esp::data::IsBonePoseReadComplete(poseBytesRead[i]);
                if (poseReadComplete) {
                    hasBoneData[i] = true;
                    boneSampleTimeUs[i] = boneNowUs;
                    poseAnchors[i] = {};
                    if (poseAnchorBytesRead[i] == sizeof(Vector3))
                        poseAnchors[i] = {pawns[i], boneNowUs, poseAnchorPositions[i]};
                    unpackBonePositionRanges(i);
                    hasHitboxData[i] = false;
                    hitboxCounts[i] = 0;
                    hitboxSampleTimeUs[i] = 0;
                    memset(allHitboxes[i], 0, sizeof(allHitboxes[i]));
                    if (poseModelBytesRead[i] == sizeof(uintptr_t) &&
                        esp::data::HasMatchingBonePoseAnchor(poseAnchors[i], pawns[i], boneNowUs) &&
                        queryHitboxDefinitions(i, sceneNodes[i], poseModelHandles[i]))
                        unpackHitboxCapsules(i);
                } else {
                    if (boneArrays[i])
                        ++boneReadFailures;
                    if (!restoreCommittedBoneSlot(i))
                        clearResolvedBoneSlot(i);
                }
            }

            s_stageBonePoseSlots.store(
                static_cast<uint32_t>(bonePoseSlotCount),
                std::memory_order_relaxed);
            s_stageBonePointerValidationSlots.store(
                static_cast<uint32_t>(bonePointerValidationSlotCount),
                std::memory_order_relaxed);
            s_stageBonePoseRanges.store(
                static_cast<uint32_t>(bonePoseRangeCount),
                std::memory_order_relaxed);
            s_stageBonePoseBytes.store(
                static_cast<uint32_t>(bonePoseBytes),
                std::memory_order_relaxed);

            
            
            
            
            bool boneReadReturnedZeros[64] = {};
            bool poseValidationFailed[64] = {};
            for (int scanIdx = 0; scanIdx < boneScanSlotCount; ++scanIdx) {
                const int i = boneScanSlots[scanIdx];
                if (!hasBoneData[i]) {
                    s_poseRejected[i] = true;
                    boneReadReturnedZeros[i] = true;
                    ++boneReadFailures;
                    continue;
                }
                bool anyNonZero = false;
                for (int b = 0; b < esp::kPlayerStoredBoneCount; ++b) {
                    const Vector3& v = allBones[i][b];
                    if (std::fabs(v.x) > 0.5f || std::fabs(v.y) > 0.5f || std::fabs(v.z) > 0.5f) {
                        anyNonZero = true;
                        break;
                    }
                }
                // Keep the raw batch/hitboxes until model remapping has been
                // tried. Clearing zero fixed indices here erased valid remap
                // evidence for models with a different core bone layout.
                const bool matchingAnchor = esp::data::HasMatchingBonePoseAnchor(
                    poseAnchors[i], pawns[i], boneSampleTimeUs[i]);
                auto plausibility = esp::data::EvaluateAnchoredBonePose(
                    allBones[i], poseAnchors[i], pawns[i], boneSampleTimeUs[i]);
                if (!plausibility.plausible && hasHitboxData[i] &&
                    boneSampleTimeUs[i] == boneNowUs && hitboxSampleTimeUs[i] == boneNowUs) {
                    Vector3 remapped[esp::kPlayerStoredBoneCount];
                    memcpy(remapped, allBones[i], sizeof(remapped));
                    if (esp::data::RemapStoredCoreBones(s_boneReadBatches[i],
                            allHitboxes[i], hitboxCounts[i], remapped)) {
                        const auto remappedPlausibility = esp::data::EvaluateAnchoredBonePose(
                            remapped, poseAnchors[i], pawns[i], boneSampleTimeUs[i]);
                        if (remappedPlausibility.plausible) {
                            memcpy(allBones[i], remapped, sizeof(remapped));
                            plausibility = remappedPlausibility;
                        }
                    }
                }
                s_poseRejected[i] = !hasBoneData[i] || !plausibility.plausible;
                boneReadReturnedZeros[i] = s_poseRejected[i];
                if (s_poseRejected[i]) {
                    poseValidationFailed[i] = anyNonZero;
                    ++boneReadFailures;
                    // Non-zero garbage used to bypass local recovery forever.
                    boneReadReturnedZeros[i] = true;
                    if (narrowDebug.Enabled(esp::diagnostics::kNarrowDebugBones) &&
                        (s_lastPoseRejectLogUs[i] == 0 || boneNowUs - s_lastPoseRejectLogUs[i] >= 5000000u)) {
                        s_lastPoseRejectLogUs[i] = boneNowUs;
                        const auto& pelvis = allBones[i][esp::PlayerStoredBoneIndex(esp::PELVIS)];
                        const auto& head = allBones[i][esp::PlayerStoredBoneIndex(esp::HEAD)];
                        DmaLogPrintf("[WARN] Bone pose rejected slot=%d reason=%s scene=0x%llX array=0x%llX old=(%.1f,%.1f,%.1f) anchor=%d:(%.1f,%.1f,%.1f) pelvis=(%.1f,%.1f,%.1f) head=(%.1f,%.1f,%.1f)",
                            i, esp::data::BonePlausibilityRejectReasonName(plausibility.rejectReason),
                            static_cast<unsigned long long>(sceneNodes[i]), static_cast<unsigned long long>(boneArrays[i]),
                            positions[i].x, positions[i].y, positions[i].z, matchingAnchor ? 1 : 0,
                            poseAnchorPositions[i].x, poseAnchorPositions[i].y, poseAnchorPositions[i].z,
                            pelvis.x, pelvis.y, pelvis.z, head.x, head.y, head.z);
                    }
                }
            }

            for (int scanIdx = 0; scanIdx < boneScanSlotCount; ++scanIdx) {
                const int i = boneScanSlots[scanIdx];
                if (!hasBoneData[i]) {
                    s_poseRejected[i] = true;
                    restoreCommittedBoneSlot(i);
                }
            }

            for (int scanIdx = 0; scanIdx < boneScanSlotCount; ++scanIdx) {
                const int i = boneScanSlots[scanIdx];
                s_cachedSceneNodes[i] = sceneNodes[i];
                s_cachedBoneArrays[i] = boneArrays[i];
                s_lastBoneSlotReadUs[i] = boneNowUs;
                if (bonePointerValidationDue[i] &&
                    bonePointerValidationSucceeded[i] &&
                    isLikelyGamePointer(sceneNodes[i]) &&
                    isLikelyGamePointer(boneArrays[i])) {
                    s_lastBonePointerValidationUs[i] = boneNowUs;
                }
            }

            bool perSlotStaleDetected = false;
            int perSlotProbeCandidateCount = 0;
            for (int scanIdx = 0; scanIdx < boneScanSlotCount; ++scanIdx) {
                const int i = boneScanSlots[scanIdx];
                if (!isBoneSlotLive(i)) {
                    s_perSlotBoneStaleStreak[i] = 0;
                    s_perSlotBoneStaleEscalation[i] = 0;
                    continue;
                }
                
                
                
                const bool slotLooksStale =
                    esp::data::IsBoneSlotStale(
                        sceneNodes[i],
                        boneArrays[i],
                        boneReadReturnedZeros[i]);
                if (slotLooksStale) {
                    if (s_perSlotBoneStaleStreak[i] < 0xFFFFFFFFu)
                        ++s_perSlotBoneStaleStreak[i];
                    if (s_perSlotBoneStaleStreak[i] >= esp::data::kPerSlotBoneLocalResetFrames)
                        perSlotStaleDetected = true;
                } else {
                    s_perSlotBoneStaleStreak[i] = 0;
                    s_perSlotBoneStaleEscalation[i] = 0;
                    s_lastPerSlotBoneLocalResetUs[i] = 0;
                }
            }
            if (perSlotStaleDetected) {
                for (int i = 0; i < 64; ++i) {
                    if (s_perSlotBoneStaleStreak[i] < esp::data::kPerSlotBoneLocalResetFrames)
                        continue;

                    const bool localResetCooldownElapsed =
                        esp::data::IsPerSlotBoneLocalResetDue(
                            s_perSlotBoneStaleStreak[i],
                            s_lastPerSlotBoneLocalResetUs[i],
                            boneNowUs);
                    if (!localResetCooldownElapsed)
                        continue;

                    s_lastPerSlotBoneLocalResetUs[i] = boneNowUs;
                    // Invalidate only the stale slot. The next bone lane becomes
                    // urgent and reacquires scene-node/bone-array pointers while
                    // the committed pose provides a short visual hold.
                    clearBoneCacheSlot(i);
                    s_perSlotBoneStaleStreak[i] = 0;
                    if (s_perSlotBoneStaleEscalation[i] < 0xFFu)
                        ++s_perSlotBoneStaleEscalation[i];
                    // Model/pose validation failures do not justify recurring
                    // global DMA refreshes. Retry only this slot's pointers.
                    if (!poseValidationFailed[i] &&
                        esp::data::ShouldEscalatePerSlotBoneProbe(s_perSlotBoneStaleEscalation[i]))
                        ++perSlotProbeCandidateCount;
                }

                const int globalProbeThreshold =
                    eligibleBoneSlotCount <= 1 ? 1 : 2;
                if (perSlotProbeCandidateCount >= globalProbeThreshold) {
                    const bool perSlotProbeCooldownElapsed =
                        esp::data::IsPerSlotBoneProbeDue(
                            s_lastPerSlotBoneRefreshUs,
                            boneNowUs);
                    if (perSlotProbeCooldownElapsed) {
                        s_lastPerSlotBoneRefreshUs = boneNowUs;
                        refreshDmaCaches(
                            "per_slot_bone_stale_probe",
                            DmaRefreshTier::Probe,
                            false,
                            DmaRefreshTrigger::BoneSlotStale);
                    }
                }
            }

            if (narrowDebug.Enabled(esp::diagnostics::kNarrowDebugBones)) {
                static uint32_t s_bonesDebugCounter = 0;
                const bool emitBonesDebug =
                    sceneNodeFailures > 0 ||
                    boneArrayFailures > 0 ||
                    boneReadFailures > 0 ||
                    narrowDebug.Tick(esp::diagnostics::kNarrowDebugBones, s_bonesDebugCounter, 40u);
                if (emitBonesDebug) {
                    int sceneNodeCount = 0;
                    int boneArrayCount = 0;
                    int readyCount = 0;
                    for (int scanIdx = 0; scanIdx < boneScanSlotCount; ++scanIdx) {
                        const int i = boneScanSlots[scanIdx];
                        if (sceneNodes[i])
                            ++sceneNodeCount;
                        if (boneArrays[i])
                            ++boneArrayCount;
                        if (hasBoneData[i])
                            ++readyCount;
                    }
                    DmaLogPrintf(
                        "[DEBUG] Bones: fails(scene/array/read)=%d/%d/%d skeleton=%d sceneNodes=%d boneArrays=%d ready=%d needed=%d",
                        sceneNodeFailures,
                        boneArrayFailures,
                        boneReadFailures,
                        wantsEspSkeleton ? 1 : 0,
                        sceneNodeCount,
                        boneArrayCount,
                        readyCount,
                        esp::kPlayerStoredBoneCount);
                }
            }

            bool anyRejectedPose = false;
            for (int idx = 0; idx < eligibleBoneSlotCount; ++idx)
                anyRejectedPose = anyRejectedPose || s_poseRejected[eligibleBoneSlots[idx]];
            if (sceneNodeFailures > 0 || boneArrayFailures > 0 || boneReadFailures > 0 || anyRejectedPose) {
                const bool anyCachedBones = [&]() -> bool {
                    for (int scanIdx = 0; scanIdx < boneScanSlotCount; ++scanIdx) {
                        const int i = boneScanSlots[scanIdx];
                        if (s_cachedBoneArrays[i] != 0 || hasReusableCommittedBoneSlot(i))
                            return true;
                    }
                    return false;
                }();
                if (anyCachedBones)
                    MarkSubsystemDegraded(RuntimeSubsystem::Bones, boneSubsystemNowUs);
                else
                    MarkSubsystemFailed(RuntimeSubsystem::Bones, boneSubsystemNowUs);
            } else {
                MarkSubsystemHealthy(RuntimeSubsystem::Bones, boneSubsystemNowUs);
            }
        }
    } else if (narrowDebug.Enabled(esp::diagnostics::kNarrowDebugBones)) {
        s_stageBonePoseSlots.store(0, std::memory_order_relaxed);
        s_stageBonePointerValidationSlots.store(0, std::memory_order_relaxed);
        s_stageBonePoseRanges.store(0, std::memory_order_relaxed);
        s_stageBonePoseBytes.store(0, std::memory_order_relaxed);
        static uint32_t s_bonesDisabledDebugCounter = 0;
        if (narrowDebug.Tick(esp::diagnostics::kNarrowDebugBones, s_bonesDisabledDebugCounter, 120u)) {
            DmaLogPrintf("[DEBUG] Bones: skipped skeleton=0");
        }
        SetSubsystemUnknown(RuntimeSubsystem::Bones);
    } else {
        s_stageBonePoseSlots.store(0, std::memory_order_relaxed);
        s_stageBonePointerValidationSlots.store(0, std::memory_order_relaxed);
        s_stageBonePoseRanges.store(0, std::memory_order_relaxed);
        s_stageBonePoseBytes.store(0, std::memory_order_relaxed);
        SetSubsystemUnknown(RuntimeSubsystem::Bones);
    }
