        struct WorldReadScratch {
            uintptr_t worldBlocks[kMaxTrackedWorldBlocks];
            uintptr_t worldEntities[kMaxTrackedWorldEntities + 1];
            uintptr_t worldSceneNodes[kMaxTrackedWorldEntities + 1];
            uintptr_t worldIdentities[kMaxTrackedWorldEntities + 1];
            uintptr_t worldDesignerNamePtrs[kMaxTrackedWorldEntities + 1];
            char worldDesignerNames[kMaxTrackedWorldEntities + 1][48];
            uint32_t worldOwnerHandles[kMaxTrackedWorldEntities + 1];
            uint32_t worldDropTicks[kMaxTrackedWorldEntities + 1];
            uint32_t worldSubclassIds[kMaxTrackedWorldEntities + 1];
            uint16_t worldItemDefs[kMaxTrackedWorldEntities + 1];
            int worldSmokeTick[kMaxTrackedWorldEntities + 1];
            uint8_t worldSmokeActive[kMaxTrackedWorldEntities + 1];
            uint8_t worldSmokeVolumeDataReceived[kMaxTrackedWorldEntities + 1];
            uint8_t worldSmokeEffectSpawned[kMaxTrackedWorldEntities + 1];
            int worldInfernoTick[kMaxTrackedWorldEntities + 1];
            float worldInfernoLife[kMaxTrackedWorldEntities + 1];
            Vector3 worldInfernoOrigin[kMaxTrackedWorldEntities + 1];
            DWORD worldInfernoOriginReadBytes[kMaxTrackedWorldEntities + 1];
            int worldInfernoFireCount[kMaxTrackedWorldEntities + 1];
            uint8_t worldInfernoInPostEffect[kMaxTrackedWorldEntities + 1];
            int worldDecoyTick[kMaxTrackedWorldEntities + 1];
            int worldDecoyClientTick[kMaxTrackedWorldEntities + 1];
            int worldExplodeTick[kMaxTrackedWorldEntities + 1];
            Vector3 worldVelocities[kMaxTrackedWorldEntities + 1];
            Vector3 worldPositions[kMaxTrackedWorldEntities + 1];
            Vector3 worldTransformPositions[kMaxTrackedWorldEntities + 1];
            uint8_t worldDormantFlags[kMaxTrackedWorldEntities + 1];
            bool worldUnknownUtilityProbe[kMaxTrackedWorldEntities + 1];
            DWORD worldBlockReadBytes[kMaxTrackedWorldBlocks];
            DWORD worldEntityReadBytes[kMaxTrackedWorldEntities + 1];
            DWORD worldSceneNodeReadBytes[kMaxTrackedWorldEntities + 1];
            DWORD worldIdentityReadBytes[kMaxTrackedWorldEntities + 1];
            DWORD worldDesignerNamePtrReadBytes[kMaxTrackedWorldEntities + 1];
            DWORD worldDesignerNameReadBytes[kMaxTrackedWorldEntities + 1];
            DWORD worldOwnerReadBytes[kMaxTrackedWorldEntities + 1];
            DWORD worldDropTickReadBytes[kMaxTrackedWorldEntities + 1];
            DWORD worldSubclassReadBytes[kMaxTrackedWorldEntities + 1];
            DWORD worldItemDefReadBytes[kMaxTrackedWorldEntities + 1];
            DWORD worldPositionReadBytes[kMaxTrackedWorldEntities + 1];
            DWORD worldTransformPositionReadBytes[kMaxTrackedWorldEntities + 1];
            DWORD worldDormantReadBytes[kMaxTrackedWorldEntities + 1];
            DWORD worldSmokeTickReadBytes[kMaxTrackedWorldEntities + 1];
            DWORD worldSmokeActiveReadBytes[kMaxTrackedWorldEntities + 1];
            DWORD worldSmokeVolumeReadBytes[kMaxTrackedWorldEntities + 1];
            DWORD worldSmokeSpawnedReadBytes[kMaxTrackedWorldEntities + 1];
            DWORD worldInfernoTickReadBytes[kMaxTrackedWorldEntities + 1];
            DWORD worldInfernoLifeReadBytes[kMaxTrackedWorldEntities + 1];
            DWORD worldInfernoFireCountReadBytes[kMaxTrackedWorldEntities + 1];
            DWORD worldInfernoPostEffectReadBytes[kMaxTrackedWorldEntities + 1];
            DWORD worldDecoyTickReadBytes[kMaxTrackedWorldEntities + 1];
            DWORD worldDecoyClientTickReadBytes[kMaxTrackedWorldEntities + 1];
            DWORD worldExplodeTickReadBytes[kMaxTrackedWorldEntities + 1];
            DWORD worldVelocityReadBytes[kMaxTrackedWorldEntities + 1];
            uint8_t worldEntityReadComplete[kMaxTrackedWorldEntities + 1];
            uint8_t worldBasicDetailsComplete[kMaxTrackedWorldEntities + 1];
            uint8_t worldSceneNodeChanged[kMaxTrackedWorldEntities + 1];
            uint8_t worldSceneNodeRefreshRequested[kMaxTrackedWorldEntities + 1];
            uint8_t worldC4DynamicComplete[kMaxTrackedWorldEntities + 1];
            uint8_t worldClassKinds[kMaxTrackedWorldEntities + 1];
        };
        static thread_local WorldReadScratch s_worldReadScratch = {};
        auto& worldBlocks = s_worldReadScratch.worldBlocks;
        auto& worldEntities = s_worldReadScratch.worldEntities;
        auto& worldSceneNodes = s_worldReadScratch.worldSceneNodes;
        auto& worldIdentities = s_worldReadScratch.worldIdentities;
        auto& worldDesignerNamePtrs = s_worldReadScratch.worldDesignerNamePtrs;
        auto& worldDesignerNames = s_worldReadScratch.worldDesignerNames;
        auto& worldOwnerHandles = s_worldReadScratch.worldOwnerHandles;
        auto& worldDropTicks = s_worldReadScratch.worldDropTicks;
        auto& worldSubclassIds = s_worldReadScratch.worldSubclassIds;
        auto& worldItemDefs = s_worldReadScratch.worldItemDefs;
        auto& worldSmokeTick = s_worldReadScratch.worldSmokeTick;
        auto& worldSmokeActive = s_worldReadScratch.worldSmokeActive;
        auto& worldSmokeVolumeDataReceived = s_worldReadScratch.worldSmokeVolumeDataReceived;
        auto& worldSmokeEffectSpawned = s_worldReadScratch.worldSmokeEffectSpawned;
        auto& worldInfernoTick = s_worldReadScratch.worldInfernoTick;
        auto& worldInfernoLife = s_worldReadScratch.worldInfernoLife;
        auto& worldInfernoOrigin = s_worldReadScratch.worldInfernoOrigin;
        auto& worldInfernoOriginReadBytes = s_worldReadScratch.worldInfernoOriginReadBytes;
        auto& worldInfernoFireCount = s_worldReadScratch.worldInfernoFireCount;
        auto& worldInfernoInPostEffect = s_worldReadScratch.worldInfernoInPostEffect;
        auto& worldDecoyTick = s_worldReadScratch.worldDecoyTick;
        auto& worldDecoyClientTick = s_worldReadScratch.worldDecoyClientTick;
        auto& worldExplodeTick = s_worldReadScratch.worldExplodeTick;
        auto& worldVelocities = s_worldReadScratch.worldVelocities;
        auto& worldPositions = s_worldReadScratch.worldPositions;
        auto& worldTransformPositions = s_worldReadScratch.worldTransformPositions;
        auto& worldDormantFlags = s_worldReadScratch.worldDormantFlags;
        auto& worldUnknownUtilityProbe = s_worldReadScratch.worldUnknownUtilityProbe;
        auto& worldBlockReadBytes = s_worldReadScratch.worldBlockReadBytes;
        auto& worldEntityReadBytes = s_worldReadScratch.worldEntityReadBytes;
        auto& worldSceneNodeReadBytes = s_worldReadScratch.worldSceneNodeReadBytes;
        auto& worldIdentityReadBytes = s_worldReadScratch.worldIdentityReadBytes;
        auto& worldDesignerNamePtrReadBytes = s_worldReadScratch.worldDesignerNamePtrReadBytes;
        auto& worldDesignerNameReadBytes = s_worldReadScratch.worldDesignerNameReadBytes;
        auto& worldOwnerReadBytes = s_worldReadScratch.worldOwnerReadBytes;
        auto& worldDropTickReadBytes = s_worldReadScratch.worldDropTickReadBytes;
        auto& worldSubclassReadBytes = s_worldReadScratch.worldSubclassReadBytes;
        auto& worldItemDefReadBytes = s_worldReadScratch.worldItemDefReadBytes;
        auto& worldPositionReadBytes = s_worldReadScratch.worldPositionReadBytes;
        auto& worldTransformPositionReadBytes = s_worldReadScratch.worldTransformPositionReadBytes;
        auto& worldDormantReadBytes = s_worldReadScratch.worldDormantReadBytes;
        auto& worldSmokeTickReadBytes = s_worldReadScratch.worldSmokeTickReadBytes;
        auto& worldSmokeActiveReadBytes = s_worldReadScratch.worldSmokeActiveReadBytes;
        auto& worldSmokeVolumeReadBytes = s_worldReadScratch.worldSmokeVolumeReadBytes;
        auto& worldSmokeSpawnedReadBytes = s_worldReadScratch.worldSmokeSpawnedReadBytes;
        auto& worldInfernoTickReadBytes = s_worldReadScratch.worldInfernoTickReadBytes;
        auto& worldInfernoLifeReadBytes = s_worldReadScratch.worldInfernoLifeReadBytes;
        auto& worldInfernoFireCountReadBytes = s_worldReadScratch.worldInfernoFireCountReadBytes;
        auto& worldInfernoPostEffectReadBytes = s_worldReadScratch.worldInfernoPostEffectReadBytes;
        auto& worldDecoyTickReadBytes = s_worldReadScratch.worldDecoyTickReadBytes;
        auto& worldDecoyClientTickReadBytes = s_worldReadScratch.worldDecoyClientTickReadBytes;
        auto& worldExplodeTickReadBytes = s_worldReadScratch.worldExplodeTickReadBytes;
        auto& worldVelocityReadBytes = s_worldReadScratch.worldVelocityReadBytes;
        auto& worldEntityReadComplete = s_worldReadScratch.worldEntityReadComplete;
        auto& worldBasicDetailsComplete = s_worldReadScratch.worldBasicDetailsComplete;
        auto& worldSceneNodeChanged = s_worldReadScratch.worldSceneNodeChanged;
        auto& worldSceneNodeRefreshRequested = s_worldReadScratch.worldSceneNodeRefreshRequested;
        auto& worldC4DynamicComplete = s_worldReadScratch.worldC4DynamicComplete;
        auto& worldClassKinds = s_worldReadScratch.worldClassKinds;
        std::memset(worldDormantFlags, 0, sizeof(worldDormantFlags));
        const int worldLimit = std::clamp(highestEntityIndex, 64, kMaxTrackedWorldEntities);
        const int blockCount = std::min((worldLimit >> 9) + 1, kMaxTrackedWorldBlocks);
        static thread_local int s_worldCandidateIndices[kMaxTrackedWorldEntities + 1];
        static thread_local uint8_t s_worldCandidateMask[kMaxTrackedWorldEntities + 1];
        static thread_local int s_worldProcessIndices[kMaxTrackedWorldEntities + 1];
        static thread_local uint8_t s_worldEntityChangedFlags[kMaxTrackedWorldEntities + 1];
        std::memset(s_worldCandidateMask, 0, sizeof(s_worldCandidateMask));
        int worldCandidateCount = 0;
        auto pushWorldCandidate = [&](int idx) {
            if (idx < esp::data::kFirstWorldEntitySlot || idx > worldLimit || s_worldCandidateMask[idx] != 0)
                return;
            s_worldCandidateMask[idx] = 1u;
            s_worldCandidateIndices[worldCandidateCount++] = idx;
        };
        auto hasTrackedSubclass = [](const uint32_t (&tracked)[kTrackedWorldSubclassSlots], uint32_t subclassId) -> bool {
            if (subclassId == 0u)
                return false;
            for (uint32_t trackedId : tracked) {
                if (trackedId == subclassId)
                    return true;
            }
            return false;
        };
        auto rememberTrackedSubclass = [](uint32_t (&tracked)[kTrackedWorldSubclassSlots], uint32_t subclassId) {
            if (subclassId == 0u)
                return;
            for (uint32_t& trackedId : tracked) {
                if (trackedId == subclassId)
                    return;
                if (trackedId == 0u) {
                    trackedId = subclassId;
                    return;
                }
            }
            tracked[0] = subclassId;
        };
        auto normalizeWorldItemId = [](uint16_t itemId) -> uint16_t {
            if (itemId == 0 || itemId >= 1200)
                return 0;
            if (WeaponNameFromItemId(itemId) == nullptr)
                return 0;
            return itemId;
        };
        auto isUtilityWorldItemId = [](uint16_t itemId) -> bool {
            return itemId >= 43 && itemId <= 48;
        };
        auto hasTrackedWorldUtilityState = [&](int idx) {
            return
                s_worldUtilityHasHistory[idx] ||
                s_worldSmokeLatched[idx] ||
                s_worldInfernoLatched[idx] ||
                s_worldDecoyLatched[idx] ||
                s_worldExplosiveLatched[idx] ||
                s_worldSmokeEvidenceCount[idx] != 0 ||
                s_worldInfernoEvidenceCount[idx] != 0 ||
                s_worldDecoyEvidenceCount[idx] != 0 ||
                s_worldExplosiveEvidenceCount[idx] != 0;
        };
        auto isTrackedWorldEntitySlot = [&](int idx) {
            const uint16_t itemId = s_worldEntityItemIds[idx];
            const uint32_t subclassId = s_worldEntitySubclassIds[idx];
            const auto entityClass = static_cast<esp::data::WorldEntityClass>(
                s_worldEntityClassKinds[idx]);
            const bool isBombItem = itemId == kWeaponC4Id;
            const bool isUtilityItem = isUtilityWorldItemId(itemId);
            const bool isDroppedItem =
                itemId != 0u &&
                !isBombItem &&
                !isUtilityItem &&
                !IsKnifeItemId(itemId) &&
                WeaponNameFromItemId(itemId) != nullptr &&
                entityClass == esp::data::WorldEntityClass::DroppedWeapon;
            const bool knownUtilitySubclass =
                hasTrackedSubclass(s_worldSmokeSubclassIds, subclassId) ||
                hasTrackedSubclass(s_worldDecoySubclassIds, subclassId) ||
                hasTrackedSubclass(s_worldHeSubclassIds, subclassId) ||
                hasTrackedSubclass(s_worldInfernoSubclassIds, subclassId) ||
                hasTrackedSubclass(s_worldMolotovSubclassIds, subclassId);
            return esp::data::ShouldRetainWorldEntity(
                wantsBombConsumers,
                wantsDroppedItemMarkers,
                wantsWorldUtilityData,
                isBombItem,
                isDroppedItem,
                isUtilityItem,
                entityClass,
                knownUtilitySubclass,
                hasTrackedWorldUtilityState(idx));
        };
        auto addTrackedWorldIndex = [&](int idx) {
            if (idx < esp::data::kFirstWorldEntitySlot || idx > kMaxTrackedWorldEntities || s_worldTrackedIndexPos[idx] != 0)
                return;
            if (s_worldTrackedIndexCount >= kMaxTrackedWorldEntities)
                return;
            s_worldTrackedIndices[s_worldTrackedIndexCount] = idx;
            s_worldTrackedIndexPos[idx] = static_cast<uint16_t>(s_worldTrackedIndexCount + 1);
            ++s_worldTrackedIndexCount;
        };
        auto removeTrackedWorldIndex = [&](int idx) {
            if (idx < esp::data::kFirstWorldEntitySlot || idx > kMaxTrackedWorldEntities)
                return;
            const uint16_t posPlusOne = s_worldTrackedIndexPos[idx];
            if (posPlusOne == 0)
                return;
            const int pos = static_cast<int>(posPlusOne - 1u);
            const int lastPos = s_worldTrackedIndexCount - 1;
            const int lastIdx = s_worldTrackedIndices[lastPos];
            s_worldTrackedIndices[pos] = lastIdx;
            s_worldTrackedIndices[lastPos] = 0;
            s_worldTrackedIndexPos[idx] = 0;
            --s_worldTrackedIndexCount;
            if (pos != lastPos && lastIdx >= esp::data::kFirstWorldEntitySlot && lastIdx <= kMaxTrackedWorldEntities)
                s_worldTrackedIndexPos[lastIdx] = static_cast<uint16_t>(pos + 1);
        };
        auto refreshTrackedWorldIndex = [&](int idx) {
            if (isTrackedWorldEntitySlot(idx))
                addTrackedWorldIndex(idx);
            else
                removeTrackedWorldIndex(idx);
        };
        auto clearWorldBombCandidateSlots = [&]() {
            if (s_worldBombCandidateSlotCount == 0)
                return;
            std::memset(s_worldBombCandidateSlots, 0, sizeof(s_worldBombCandidateSlots));
            s_worldBombCandidateSlotCount = 0;
        };
        auto rememberWorldBombCandidateSlot = [&](int idx) {
            if (idx < esp::data::kFirstWorldEntitySlot || idx > kMaxTrackedWorldEntities)
                return;
            constexpr int kMaxWorldBombCandidateSlots =
                static_cast<int>(sizeof(s_worldBombCandidateSlots) / sizeof(s_worldBombCandidateSlots[0]));
            for (int i = 0; i < static_cast<int>(s_worldBombCandidateSlotCount); ++i) {
                if (s_worldBombCandidateSlots[i] == idx)
                    return;
            }
            if (s_worldBombCandidateSlotCount < kMaxWorldBombCandidateSlots) {
                s_worldBombCandidateSlots[s_worldBombCandidateSlotCount++] = idx;
                return;
            }
            std::memmove(
                s_worldBombCandidateSlots,
                s_worldBombCandidateSlots + 1,
                sizeof(s_worldBombCandidateSlots[0]) * static_cast<size_t>(kMaxWorldBombCandidateSlots - 1));
            s_worldBombCandidateSlots[kMaxWorldBombCandidateSlots - 1] = idx;
        };
        std::memset(worldBlocks, 0, sizeof(uintptr_t) * static_cast<size_t>(blockCount));
        std::memset(worldBlockReadBytes, 0, sizeof(DWORD) * static_cast<size_t>(blockCount));
        auto resetWorldScratchSlot = [&](int idx) {
            worldEntities[idx] = 0;
            worldSceneNodes[idx] = 0;
            worldIdentities[idx] = 0;
            worldDesignerNamePtrs[idx] = 0;
            std::memset(worldDesignerNames[idx], 0, sizeof(worldDesignerNames[idx]));
            worldOwnerHandles[idx] = 0;
            worldDropTicks[idx] = 0;
            worldSubclassIds[idx] = 0;
            worldItemDefs[idx] = 0;
            worldSmokeTick[idx] = 0;
            worldSmokeActive[idx] = 0;
            worldSmokeVolumeDataReceived[idx] = 0;
            worldSmokeEffectSpawned[idx] = 0;
            worldInfernoTick[idx] = 0;
            worldInfernoLife[idx] = 0.0f;
            worldInfernoOrigin[idx] = {};
            worldInfernoOriginReadBytes[idx] = 0;
            worldInfernoFireCount[idx] = 0;
            worldInfernoInPostEffect[idx] = 0;
            worldDecoyTick[idx] = 0;
            worldDecoyClientTick[idx] = 0;
            worldExplodeTick[idx] = 0;
            worldVelocities[idx] = {};
            worldPositions[idx] = {};
            worldTransformPositions[idx] = {};
            worldEntityReadBytes[idx] = 0;
            worldSceneNodeReadBytes[idx] = 0;
            worldIdentityReadBytes[idx] = 0;
            worldDesignerNamePtrReadBytes[idx] = 0;
            worldDesignerNameReadBytes[idx] = 0;
            worldOwnerReadBytes[idx] = 0;
            worldDropTickReadBytes[idx] = 0;
            worldSubclassReadBytes[idx] = 0;
            worldItemDefReadBytes[idx] = 0;
            worldPositionReadBytes[idx] = 0;
            worldTransformPositionReadBytes[idx] = 0;
            worldDormantReadBytes[idx] = 0;
            worldSmokeTickReadBytes[idx] = 0;
            worldSmokeActiveReadBytes[idx] = 0;
            worldSmokeVolumeReadBytes[idx] = 0;
            worldSmokeSpawnedReadBytes[idx] = 0;
            worldInfernoTickReadBytes[idx] = 0;
            worldInfernoLifeReadBytes[idx] = 0;
            worldInfernoFireCountReadBytes[idx] = 0;
            worldInfernoPostEffectReadBytes[idx] = 0;
            worldDecoyTickReadBytes[idx] = 0;
            worldDecoyClientTickReadBytes[idx] = 0;
            worldExplodeTickReadBytes[idx] = 0;
            worldVelocityReadBytes[idx] = 0;
            worldEntityReadComplete[idx] = 0;
            worldBasicDetailsComplete[idx] = 0;
            worldSceneNodeChanged[idx] = 0;
            worldSceneNodeRefreshRequested[idx] = 0;
            worldC4DynamicComplete[idx] = 0;
            worldClassKinds[idx] = 0;
        };
        static uintptr_t s_cachedWorldBlocks[kMaxTrackedWorldBlocks] = {};
        static int s_cachedWorldBlockCount = 0;
        static uint64_t s_worldBlockCacheResetSerial = 0;
        const uint64_t worldBlockSceneResetSerial = s_sceneResetSerial.load(std::memory_order_relaxed);
        if (s_worldBlockCacheResetSerial != worldBlockSceneResetSerial) {
            s_worldBlockCacheResetSerial = worldBlockSceneResetSerial;
            memset(s_cachedWorldBlocks, 0, sizeof(s_cachedWorldBlocks));
            s_cachedWorldBlockCount = 0;
        }

        auto needsWorldOwnerRefreshForItemId = [&](uint16_t itemId) -> bool {
            if (itemId == 0)
                return false;
            if (itemId == kWeaponC4Id)
                return true;
            return WeaponNameFromItemId(itemId) != nullptr &&
                   !isUtilityWorldItemId(itemId) &&
                   !IsKnifeItemId(itemId);
        };

        #include "world_parts/world_domain_candidates.inl"
        for (int candidateIdx = 0; candidateIdx < worldCandidateCount; ++candidateIdx)
            resetWorldScratchSlot(s_worldCandidateIndices[candidateIdx]);
