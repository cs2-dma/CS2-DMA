struct WorldPipelineReplay {
    struct Reader : PlayerRepairReplay::Reader {
        std::map<uintptr_t, size_t> readCounts;
        uintptr_t partialAddress = 0;
        size_t partialBytes = 0;
        uintptr_t unreadablePage = 0;
        void AddScatterReadRequest(int handle, uintptr_t address, void* output, size_t size, DWORD* completed) {
            ++readCounts[address];
            PlayerRepairReplay::Reader::AddScatterReadRequest(handle, address, output, size, completed);
        }
        bool Execute() {
            ++batches;
            for (const auto& request : requests) {
                auto found = memory.upper_bound(request.address);
                if (found == memory.begin()) continue;
                --found;
                const size_t offset = request.address - found->first;
                if (offset > found->second.size() || request.size > found->second.size() - offset) continue;
                const size_t limit = request.address == shortAddress ? 1 :
                    request.address == partialAddress ? (std::min)(request.size, partialBytes) : request.size;
                size_t completed = 0;
                for (size_t i = 0; i < limit; ++i) {
                    if (unreadablePage && ((request.address + i) & ~uintptr_t{4095}) == unreadablePage) continue;
                    static_cast<uint8_t*>(request.output)[i] = found->second[offset + i];
                    ++completed;
                }
                *request.completed = static_cast<DWORD>(completed);
            }
            if (mutateAddress && (mutateAtBatch == 0 || batches == mutateAtBatch)) {
                Set(mutateAddress, mutateValue);
                mutateAddress = 0;
            }
            requests.clear();
            return true;
        }
    } mem;
    runtime_offsets::Values ofs;
    using WorldMarker = esp::WorldMarker;
    using WorldMarkerType = esp::WorldMarkerType;
    using BombState = esp::BombState;
    using RuntimeSubsystem = esp::RuntimeSubsystem;
    static constexpr int kMaxTrackedWorldEntities = 8191, kMaxTrackedWorldBlocks = 16;
    static constexpr int kTrackedWorldSubclassSlots = 8;
    static constexpr uint16_t kWeaponC4Id = 49;
    static constexpr uint32_t kEntitySlotMask = 0x1FF, kEntityHandleMask = 0x7FFF;
    uint32_t s_activeEntitySlotSize = 0x70;
    std::atomic<bool> s_engineInGame{true}, s_engineMenu{false};
    std::atomic<int> s_engineSignOnState{6};
    std::atomic<uint64_t> s_sceneResetSerial{1}, s_lastSceneResetUs{1};
    std::atomic<uint64_t> s_worldTrackedEntityCountStat{}, s_worldCandidateCountStat{};
    std::atomic<uint64_t> s_worldUtilityCandidateCountStat{}, s_worldClassifiedCandidateCountStat{};
    std::atomic<uint64_t> s_worldIdentityPendingCountStat{}, s_worldReadCountsStat{}, s_worldReadDemandStat{};
    std::atomic<uint64_t> s_lastWorldScanCommittedUs{}, s_worldMarkerCapacityDropsStat{};
    std::atomic<uint64_t> s_worldMarkerReadGapHoldsStat{}, s_worldPositionReadMissesStat{};
    uintptr_t s_worldEntityRefs[8192]{};
    uint32_t s_worldEntitySubclassIds[8192]{};
    uint16_t s_worldEntityItemIds[8192]{};
    uint8_t s_worldEntityClassKinds[8192]{};
    int s_worldTrackedIndices[8192]{};
    uint16_t s_worldTrackedIndexPos[8192]{};
    int s_worldTrackedIndexCount{};
    uint32_t s_worldSmokeSubclassIds[8]{};
    uint32_t s_worldMolotovSubclassIds[8]{};
    uint32_t s_worldDecoySubclassIds[8]{};
    uint32_t s_worldHeSubclassIds[8]{};
    uint32_t s_worldInfernoSubclassIds[8]{};
    bool s_worldSmokeLatched[8192]{};
    bool s_worldInfernoLatched[8192]{};
    bool s_worldDecoyLatched[8192]{};
    bool s_worldExplosiveLatched[8192]{};
    bool s_worldUtilityHasHistory[8192]{};
    uint8_t s_worldSmokeEvidenceCount[8192]{};
    uint8_t s_worldInfernoEvidenceCount[8192]{};
    uint8_t s_worldDecoyEvidenceCount[8192]{};
    uint8_t s_worldExplosiveEvidenceCount[8192]{};
    uint64_t s_worldSmokeStartUs[8192]{};
    uint64_t s_worldInfernoStartUs[8192]{};
    uint64_t s_worldDecoyStartUs[8192]{};
    uint64_t s_worldExplosiveStartUs[8192]{};
    uint64_t s_worldUtilityDeadlinesUs[8192][4]{};
    Vector3 s_worldPrevPos[8192]{};
    int s_worldPrevSmokeTick[8192]{};
    uint8_t s_worldPrevSmokeActive[8192]{};
    uint8_t s_worldPrevSmokeVolumeDataReceived[8192]{};
    uint8_t s_worldPrevSmokeEffectSpawned[8192]{};
    int s_worldPrevInfernoTick[8192]{};
    float s_worldPrevInfernoLife[8192]{};
    int s_worldPrevInfernoFireCount[8192]{};
    uint8_t s_worldPrevInfernoInPostEffect[8192]{};
    int s_worldPrevDecoyTick[8192]{};
    int s_worldPrevDecoyClientTick[8192]{};
    int s_worldPrevExplodeTick[8192]{};
    Vector3 s_worldPrevVelocity[8192]{};
    esp::data::UtilityFieldTimes s_worldUtilityFieldTimes[8192]{};
    uint64_t s_worldUtilityPositionSampleUs[8192]{};
    uint64_t s_worldUtilityStationarySinceUs[8192]{};
    uint8_t s_worldUtilityStationarySamples[8192]{};
    inline void ResetWorldUtilityTrackingSlot(int idx) noexcept
    {
        if (idx < 0 || idx > kMaxTrackedWorldEntities)
            return;
        s_worldSmokeLatched[idx] = false;
        s_worldInfernoLatched[idx] = false;
        s_worldDecoyLatched[idx] = false;
        s_worldExplosiveLatched[idx] = false;
        s_worldUtilityHasHistory[idx] = false;
        s_worldSmokeStartUs[idx] = 0;
        s_worldInfernoStartUs[idx] = 0;
        s_worldDecoyStartUs[idx] = 0;
        s_worldExplosiveStartUs[idx] = 0;
        std::fill(std::begin(s_worldUtilityDeadlinesUs[idx]), std::end(s_worldUtilityDeadlinesUs[idx]), 0u);
        s_worldSmokeEvidenceCount[idx] = 0;
        s_worldInfernoEvidenceCount[idx] = 0;
        s_worldDecoyEvidenceCount[idx] = 0;
        s_worldExplosiveEvidenceCount[idx] = 0;
        s_worldPrevPos[idx] = {};
        s_worldPrevSmokeTick[idx] = 0;
        s_worldPrevSmokeActive[idx] = 0;
        s_worldPrevSmokeVolumeDataReceived[idx] = 0;
        s_worldPrevSmokeEffectSpawned[idx] = 0;
        s_worldPrevInfernoTick[idx] = 0;
        s_worldPrevInfernoLife[idx] = 0.0f;
        s_worldPrevInfernoFireCount[idx] = 0;
        s_worldPrevInfernoInPostEffect[idx] = 0;
        s_worldPrevDecoyTick[idx] = 0;
        s_worldPrevDecoyClientTick[idx] = 0;
        s_worldPrevExplodeTick[idx] = 0;
        s_worldPrevVelocity[idx] = {};
        s_worldUtilityFieldTimes[idx] = {};
        s_worldUtilityPositionSampleUs[idx] = 0;
        s_worldUtilityStationarySinceUs[idx] = 0;
        s_worldUtilityStationarySamples[idx] = 0;
    }
    WorldMarker s_worldMarkers[256]{};
    int s_worldMarkerCount = 0;
    BombState s_bombState;
    uint64_t nowUs = 10000000, s_lastWorldScanUs = 0, s_lastStableGameTimeUs = 10000000;
    float currentGameTime = 102, intervalPerTick = 1.0f / 64, s_lastStableIntervalPerTick = 1.0f / 64;
    uint16_t s_localWeaponId = 0;
    bool currentGameTimeFresh = true, wantsEspItem = true, wantsEspWorld = true;
    bool freezeGameClock = false;
    bool wantsEspWorldProjectiles = true, webRadarDemandActive = false;
    bool wantsEspBombInfo = false, wantsRadarShowBomb = false;
    bool bombDroppedByRules = false, bombPlantedByRules = false, bombRoundChangedThisTick = false;
    bool weaponC4PosValid = false;
    uintptr_t entityList = 0x100000, weaponC4Entity = 0;
    Vector3 weaponC4WorldPos{};
    bool _playerHierarchyActiveTick = false, _playerAuxActiveTick = false;
    bool _inventoryActiveTick = false, _inventoryFullTick = false, _boneReadsActiveTick = false;
    int highestEntityIndex = 900, handle = 0, playerResolvedSlotCount = 0;
    int playerResolvedSlots[64]{}, healths[64]{}, teams[64]{};
    uint32_t pawnHandles[64]{};
    uint16_t weaponIds[64]{};
    uintptr_t controllers[64]{};
    uint8_t lifeStates[64]{};
    Vector3 positions[64]{};
    std::bitset<1200> espItemEnabledMaskSnapshot;
    struct Debug { bool Enabled(unsigned) const { return false; } } narrowDebug;
    template<class... Args> void DmaLogPrintf(const char*, Args...) {}
    void logUpdateDataIssue(const char*, const char*) {}
    void SetSubsystemUnknown(RuntimeSubsystem) {}
    void MarkSubsystemHealthy(RuntimeSubsystem, uint64_t) {}
    void MarkSubsystemDegraded(RuntimeSubsystem, uint64_t) {}
    int findPlayerIndexByEntityHandle(uint32_t) const { return -1; }
    bool executeOptionalScatterRead() { return mem.Execute(); }
    bool isLikelyGamePointer(uintptr_t pointer) const { return pointer >= 0x10000; }
    static bool IsFiniteVec(const Vector3& p) {
        return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
    }
    static bool isValidWorldPos(const Vector3& p) {
        return IsFiniteVec(p) && std::fabs(p.x) < 32768 && std::fabs(p.y) < 32768 &&
            std::fabs(p.z) < 16384 && std::fabs(p.x) + std::fabs(p.y) + std::fabs(p.z) > 1;
    }
    void ResetWorldUtilityTrackingState() {
        std::memset(&s_worldEntityRefs, 0, sizeof(s_worldEntityRefs));
        std::memset(&s_worldEntitySubclassIds, 0, sizeof(s_worldEntitySubclassIds));
        std::memset(&s_worldEntityItemIds, 0, sizeof(s_worldEntityItemIds));
        std::memset(&s_worldEntityClassKinds, 0, sizeof(s_worldEntityClassKinds));
        std::memset(&s_worldTrackedIndices, 0, sizeof(s_worldTrackedIndices));
        std::memset(&s_worldTrackedIndexPos, 0, sizeof(s_worldTrackedIndexPos));
        std::memset(&s_worldTrackedIndexCount, 0, sizeof(s_worldTrackedIndexCount));
        std::memset(&s_worldSmokeSubclassIds, 0, sizeof(s_worldSmokeSubclassIds));
        std::memset(&s_worldMolotovSubclassIds, 0, sizeof(s_worldMolotovSubclassIds));
        std::memset(&s_worldDecoySubclassIds, 0, sizeof(s_worldDecoySubclassIds));
        std::memset(&s_worldHeSubclassIds, 0, sizeof(s_worldHeSubclassIds));
        std::memset(&s_worldInfernoSubclassIds, 0, sizeof(s_worldInfernoSubclassIds));
        std::memset(&s_worldSmokeLatched, 0, sizeof(s_worldSmokeLatched));
        std::memset(&s_worldInfernoLatched, 0, sizeof(s_worldInfernoLatched));
        std::memset(&s_worldDecoyLatched, 0, sizeof(s_worldDecoyLatched));
        std::memset(&s_worldExplosiveLatched, 0, sizeof(s_worldExplosiveLatched));
        std::memset(&s_worldUtilityHasHistory, 0, sizeof(s_worldUtilityHasHistory));
        std::memset(&s_worldSmokeEvidenceCount, 0, sizeof(s_worldSmokeEvidenceCount));
        std::memset(&s_worldInfernoEvidenceCount, 0, sizeof(s_worldInfernoEvidenceCount));
        std::memset(&s_worldDecoyEvidenceCount, 0, sizeof(s_worldDecoyEvidenceCount));
        std::memset(&s_worldExplosiveEvidenceCount, 0, sizeof(s_worldExplosiveEvidenceCount));
        std::memset(&s_worldSmokeStartUs, 0, sizeof(s_worldSmokeStartUs));
        std::memset(&s_worldInfernoStartUs, 0, sizeof(s_worldInfernoStartUs));
        std::memset(&s_worldDecoyStartUs, 0, sizeof(s_worldDecoyStartUs));
        std::memset(&s_worldExplosiveStartUs, 0, sizeof(s_worldExplosiveStartUs));
        std::memset(&s_worldUtilityDeadlinesUs, 0, sizeof(s_worldUtilityDeadlinesUs));
        std::memset(&s_worldPrevPos, 0, sizeof(s_worldPrevPos));
        std::memset(&s_worldPrevSmokeTick, 0, sizeof(s_worldPrevSmokeTick));
        std::memset(&s_worldPrevSmokeActive, 0, sizeof(s_worldPrevSmokeActive));
        std::memset(&s_worldPrevSmokeVolumeDataReceived, 0, sizeof(s_worldPrevSmokeVolumeDataReceived));
        std::memset(&s_worldPrevSmokeEffectSpawned, 0, sizeof(s_worldPrevSmokeEffectSpawned));
        std::memset(&s_worldPrevInfernoTick, 0, sizeof(s_worldPrevInfernoTick));
        std::memset(&s_worldPrevInfernoLife, 0, sizeof(s_worldPrevInfernoLife));
        std::memset(&s_worldPrevInfernoFireCount, 0, sizeof(s_worldPrevInfernoFireCount));
        std::memset(&s_worldPrevInfernoInPostEffect, 0, sizeof(s_worldPrevInfernoInPostEffect));
        std::memset(&s_worldPrevDecoyTick, 0, sizeof(s_worldPrevDecoyTick));
        std::memset(&s_worldPrevDecoyClientTick, 0, sizeof(s_worldPrevDecoyClientTick));
        std::memset(&s_worldPrevExplodeTick, 0, sizeof(s_worldPrevExplodeTick));
        std::memset(&s_worldPrevVelocity, 0, sizeof(s_worldPrevVelocity));
        std::memset(&s_worldUtilityFieldTimes, 0, sizeof(s_worldUtilityFieldTimes));
        std::memset(&s_worldUtilityPositionSampleUs, 0, sizeof(s_worldUtilityPositionSampleUs));
        std::memset(&s_worldUtilityStationarySinceUs, 0, sizeof(s_worldUtilityStationarySinceUs));
        std::memset(&s_worldUtilityStationarySamples, 0, sizeof(s_worldUtilityStationarySamples));
    }
    static uintptr_t Entity(int slot) { return 0x400000u + static_cast<uintptr_t>(slot) * 0x10000; }
    static uintptr_t Node(int slot) { return Entity(slot) + 0x2000; }
    void AddEntity(int slot, const char* name, uint16_t item, uintptr_t namePointer = 0) {
        const uintptr_t entity = Entity(slot), identity = entity + 0x3000;
        if (!namePointer) namePointer = entity + 0x4003;
        mem.Set(0x200000 + (slot >> 9) * 0x10000 + (slot & 0x1FF) * s_activeEntitySlotSize, entity);
        mem.Set(entity + ofs.C_BaseEntity_m_pGameSceneNode, Node(slot));
        mem.Set(entity + ofs.C_BaseEntity_m_hOwnerEntity, UINT32_MAX);
        mem.Set(entity + ofs.C_BaseEntity_m_nSubclassID, static_cast<uint32_t>(slot));
        mem.Set(entity + ofs.CEntityInstance_m_pEntity, identity);
        mem.Set(entity + 0x70, item);
        mem.Set(identity + ofs.CEntityIdentity_m_designerName, namePointer);
        char buffer[48]{};
        strcpy_s(buffer, name);
        mem.Set(namePointer, buffer);
        mem.Set(Node(slot) + ofs.CGameSceneNode_m_vecAbsOrigin, Vector3{200, 300, 50});
        mem.Set(Node(slot) + 0x10, Vector3{200, 300, 50});
    }
    WorldPipelineReplay() {
        static uint64_t serial = 1000;
        s_sceneResetSerial = ++serial;
        espItemEnabledMaskSnapshot.set();
        ofs.C_BaseEntity_m_pGameSceneNode = 0x10;
        ofs.C_BaseEntity_m_hOwnerEntity = 0x20;
        ofs.C_BaseEntity_m_nSubclassID = 0x30;
        ofs.CEntityInstance_m_pEntity = 0x40;
        ofs.C_EconEntity_m_AttributeManager = 0x50;
        ofs.C_AttributeContainer_m_Item = 0x10;
        ofs.C_EconItemView_m_iItemDefinitionIndex = 0x10;
        ofs.CEntityIdentity_m_designerName = 0x20;
        ofs.CGameSceneNode_m_vecAbsOrigin = 0x80;
        ofs.C_SmokeGrenadeProjectile_m_nSmokeEffectTickBegin = 0x100;
        ofs.C_SmokeGrenadeProjectile_m_bDidSmokeEffect = 0x104;
        ofs.C_SmokeGrenadeProjectile_m_vSmokeDetonationPos = 0x108;
        ofs.C_Inferno_m_nFireEffectTickBegin = 0x600;
        ofs.C_Inferno_m_nFireLifetime = 0x604;
        ofs.C_Inferno_m_fireCount = 0x608;
        ofs.C_Inferno_m_bInPostEffectTime = 0x60C;
        ofs.C_Inferno_m_firePositions = 0x200;
        for (int b = 0; b < 2; ++b)
            mem.Set(entityList + 0x10 + b * 8, uintptr_t{0x200000u + b * 0x10000u});
        for (int slot = 64; slot <= highestEntityIndex; ++slot)
            mem.Set(0x200000 + (slot >> 9) * 0x10000 + (slot & 0x1FF) * s_activeEntitySlotSize, uintptr_t{0});
        AddEntity(65, "smokegrenade_projectile", 45);
        AddEntity(66, "inferno", 65535);
        AddEntity(67, "weapon_ak47", 7);
        AddEntity(68, "weapon_incgrenade", 48);
        AddEntity(69, "weapon_hkp2000", 0);
        mem.Set(Entity(65) + 0x100, int{6400});
        mem.Set(Entity(65) + 0x104, uint8_t{1});
        mem.Set(Entity(65) + 0x108, Vector3{400, 500, 50});
        mem.Set(Entity(66) + 0x600, int{6400});
        mem.Set(Entity(66) + 0x604, float{7});
        mem.Set(Entity(66) + 0x608, int{2});
        mem.Set(Entity(66) + 0x60C, uint8_t{0});
        Vector3 points[64]{};
        points[0] = {600, 700, 50};
        points[1] = {610, 700, 50};
        mem.Set(Entity(66) + 0x200, points);
    }
    void Tick(uint64_t step = 100000) {
        using namespace esp;
        using namespace esp::weapons;
        nowUs += step;
        if (!freezeGameClock) currentGameTime += static_cast<float>(step) / 1000000;
        s_lastStableGameTimeUs = nowUs;
        WorldMarker scannedMarkers[esp::data::kWorldMarkerScratchCapacity]{};
        int scannedMarkerCount = 0;
        #include "../../../src/Features/ESP/DataReader/world_reads.inl"
        #include "../../../src/Features/ESP/DataReader/commit_world.inl"
        (void)localWeaponIdResolved;
    }
    const WorldMarker* Find(WorldMarkerType type, uint16_t item) const {
        for (int i = 0; i < s_worldMarkerCount; ++i)
            if (s_worldMarkers[i].valid && s_worldMarkers[i].type == type && s_worldMarkers[i].weaponId == item)
                return &s_worldMarkers[i];
        return nullptr;
    }
};

void TestWorldPipelineReplay() {
    using Type = esp::WorldMarkerType;
    for (int scenario = 0; scenario < 9; ++scenario) {
        auto fixture = std::make_unique<WorldPipelineReplay>();
        auto& f = *fixture;
        if (scenario == 1) f.mem.shortAddress = f.Entity(66) + 0x200;
        if (scenario == 2) f.ofs.C_Inferno_m_firePositions = 0;
        if (scenario == 3) f.mem.Set(f.Entity(66) + 0x10, uintptr_t{0});
        if (scenario == 4) f.mem.Set(f.Entity(66) + 0x60C, uint8_t{1});
        if (scenario == 5 || scenario == 8) {
            f.wantsEspWorld = false;
            f.wantsEspWorldProjectiles = false;
        }
        if (scenario == 6) f.mem.Set(f.Node(67) + 0x80, Vector3{});
        if (scenario == 7) f.highestEntityIndex = 1600;
        if (scenario == 8) {
            f.mem.shortAddress = f.Entity(67) + 0x40;
            f.Tick();
            f.Tick();
            CHECK(!f.Find(Type::DroppedWeapon, 7));
            f.mem.shortAddress = 0;
        }
        for (int i = 0; i < 12; ++i) f.Tick();
        CHECK(f.Find(Type::DroppedWeapon, 7));
        CHECK(f.Find(Type::DroppedWeapon, 48));
        CHECK(f.Find(Type::DroppedWeapon, 32));
        CHECK((f.Find(Type::Smoke, 45) != nullptr) == (scenario != 5 && scenario != 8));
        const auto* fire = f.Find(Type::Inferno, 46);
        CHECK((fire != nullptr) == (scenario != 4 && scenario != 5 && scenario != 8));
        if (fire) {
            CHECK(fire->expiresUs > f.nowUs && fire->expiresUs < f.nowUs + 4000000);
            CHECK(fire->position.x == (scenario == 1 || scenario == 2 ? 200 : 600));
        }
        CHECK(!f.Find(Type::MolotovProjectile, 48));
        f.mem.Set(f.Entity(67) + 0x20, uint32_t{0x8080});
        for (int i = 0; i < 3; ++i) f.Tick();
        CHECK(!f.Find(Type::DroppedWeapon, 7));
        f.AddEntity(70, "weapon_ak47", 0, f.Entity(67) + 0x4003);
        for (int i = 0; i < 12; ++i) f.Tick();
        CHECK(f.Find(Type::DroppedWeapon, 7));
        f.mem.Set(f.Entity(66) + 0x60C, uint8_t{1});
        for (int i = 0; i < 3; ++i) f.Tick();
        CHECK(!f.Find(Type::Inferno, 46));
        f.mem.Set(0x200000 + 68 * f.s_activeEntitySlotSize, uintptr_t{0});
        for (int i = 0; i < 4; ++i) f.Tick();
        CHECK(!f.Find(Type::DroppedWeapon, 48));
        f.AddEntity(68, "weapon_awp", 9);
        for (int i = 0; i < 12; ++i) f.Tick();
        CHECK(f.Find(Type::DroppedWeapon, 9));
        CHECK(!f.Find(Type::DroppedWeapon, 48));
        f.s_engineInGame = false;
        f.Tick();
        CHECK(f.s_worldMarkerCount == 0);
    }
}

void TestWorldRealtimeContinuity() {
    using Type = esp::WorldMarkerType;
    for (int scenario = 0; scenario < 3; ++scenario) {
        auto fixture = std::make_unique<WorldPipelineReplay>();
        auto& f = *fixture;
        f.highestEntityIndex = scenario == 2 ? 1600 : 250;
        f.wantsEspWorldProjectiles = false;
        if (scenario == 1) {
            f.wantsEspBombInfo = true;
            f.bombDroppedByRules = true;
            f.AddEntity(200, "weapon_c4", 49);
        }
        for (int i = 0; i < 150; ++i) f.Tick(4000);
        int missingSmoke = 0, missingFire = 0, missingWeapon = 0;
        for (int i = 0; i < 350; ++i) {
            f._boneReadsActiveTick = scenario == 2 && i % 3 == 0;
            f.Tick(4000);
            missingSmoke += f.Find(Type::Smoke, 45) == nullptr;
            missingFire += f.Find(Type::Inferno, 46) == nullptr;
            missingWeapon += f.Find(Type::DroppedWeapon, 7) == nullptr;
        }
        if (missingSmoke || missingFire || missingWeapon)
            std::cerr << "World continuity scenario=" << scenario << " missing smoke/fire/item="
                << missingSmoke << '/' << missingFire << '/' << missingWeapon << '\n';
        CHECK(missingSmoke == 0);
        CHECK(missingFire == 0);
        CHECK(missingWeapon == 0);
    }
}

void TestWorldPartialAndReuseContinuity() {
    using Type = esp::WorldMarkerType;
    {
        auto fixture = std::make_unique<WorldPipelineReplay>();
        auto& f = *fixture;
        f.highestEntityIndex = 250;
        for (int i = 0; i < 150; ++i) f.Tick(4000);
        CHECK(f.Find(Type::DroppedWeapon, 7));
        f.mem.shortAddress = f.Entity(67) + f.ofs.C_BaseEntity_m_hOwnerEntity;
        int missing = 0;
        for (int i = 0; i < 75; ++i) {
            f.Tick(4000);
            missing += f.Find(Type::DroppedWeapon, 7) == nullptr;
        }
        CHECK(missing == 0);
        for (int i = 0; i < 125; ++i) f.Tick(4000);
        CHECK(!f.Find(Type::DroppedWeapon, 7));
        f.mem.shortAddress = 0;
        f.mem.Set(f.Entity(67) + f.ofs.C_BaseEntity_m_hOwnerEntity, uint32_t{0x8080});
        for (int i = 0; i < 20; ++i) f.Tick(4000);
        CHECK(!f.Find(Type::DroppedWeapon, 7));
    }
    {
        auto fixture = std::make_unique<WorldPipelineReplay>();
        auto& f = *fixture;
        f.highestEntityIndex = 250;
        for (int i = 0; i < 150; ++i) f.Tick(4000);
        CHECK(f.Find(Type::Smoke, 45) && f.Find(Type::Inferno, 46));
        f.mem.Set(f.Node(65) + f.ofs.CGameSceneNode_m_vecAbsOrigin, Vector3{});
        f.mem.Set(f.Entity(65) + f.ofs.C_SmokeGrenadeProjectile_m_vSmokeDetonationPos, Vector3{});
        f.mem.Set(f.Node(66) + f.ofs.CGameSceneNode_m_vecAbsOrigin, Vector3{});
        f.mem.Set(f.Node(66) + 0x10u, Vector3{});
        f.mem.shortAddress = f.Entity(66) + f.ofs.C_Inferno_m_firePositions;
        int missingSmoke = 0, missingFire = 0;
        for (int i = 0; i < 200; ++i) {
            f.Tick(4000);
            missingSmoke += f.Find(Type::Smoke, 45) == nullptr;
            missingFire += f.Find(Type::Inferno, 46) == nullptr;
        }
        CHECK(missingSmoke == 0);
        CHECK(missingFire == 0);
        const int restartTick = static_cast<int>(f.currentGameTime * 64);
        f.mem.Set(f.Entity(65) + f.ofs.C_SmokeGrenadeProjectile_m_nSmokeEffectTickBegin, restartTick);
        f.mem.Set(f.Entity(66) + f.ofs.C_Inferno_m_nFireEffectTickBegin, restartTick);
        for (int i = 0; i < 35; ++i) f.Tick(4000);
        CHECK(!f.Find(Type::Smoke, 45) && !f.Find(Type::Inferno, 46));
        f.mem.Set(f.Entity(65) + f.ofs.C_SmokeGrenadeProjectile_m_vSmokeDetonationPos,
            Vector3{1200, 1300, 50});
        f.mem.shortAddress = 0;
        Vector3 restartedPoints[64]{};
        restartedPoints[0] = {1400, 1500, 50};
        restartedPoints[1] = {1410, 1500, 50};
        f.mem.Set(f.Entity(66) + f.ofs.C_Inferno_m_firePositions, restartedPoints);
        for (int i = 0; i < 35; ++i) f.Tick(4000);
        const auto* restartedSmoke = f.Find(Type::Smoke, 45);
        const auto* restartedFire = f.Find(Type::Inferno, 46);
        CHECK(restartedSmoke && restartedSmoke->position.x == 1200);
        CHECK(restartedFire && restartedFire->position.x == 1400);
        f.mem.Set(0x200000 + 65 * f.s_activeEntitySlotSize, uintptr_t{0});
        f.mem.Set(f.Entity(66) + f.ofs.C_Inferno_m_bInPostEffectTime, uint8_t{1});
        for (int i = 0; i < 35; ++i) f.Tick(4000);
        CHECK(!f.Find(Type::Smoke, 45) && !f.Find(Type::Inferno, 46));
    }
    {
        auto fixture = std::make_unique<WorldPipelineReplay>();
        auto& f = *fixture;
        f.highestEntityIndex = 250;
        for (int i = 0; i < 150; ++i) f.Tick(4000);
        CHECK(f.Find(Type::DroppedWeapon, 48));
        f.AddEntity(68, "inferno", 65535);
        f.mem.Set(f.Entity(68) + 0x600, static_cast<int>(f.currentGameTime * 64));
        f.mem.Set(f.Entity(68) + 0x604, 7.0f);
        f.mem.Set(f.Entity(68) + 0x608, int{1});
        f.mem.Set(f.Entity(68) + 0x60C, uint8_t{0});
        Vector3 points[64]{};
        points[0] = {800, 900, 50};
        f.mem.Set(f.Entity(68) + 0x200, points);
        for (int i = 0; i < 300; ++i) f.Tick(4000);
        bool reusedFire = false;
        for (int i = 0; i < f.s_worldMarkerCount; ++i)
            reusedFire |= f.s_worldMarkers[i].type == Type::Inferno &&
                f.s_worldMarkers[i].sourceEntity == f.Entity(68);
        CHECK(reusedFire);
        CHECK(!f.Find(Type::DroppedWeapon, 48));
    }
    {
        auto fixture = std::make_unique<WorldPipelineReplay>();
        auto& f = *fixture;
        f.highestEntityIndex = 1600;
        for (int slot = 70; slot < 220; ++slot) f.AddEntity(slot, "weapon_ak47", 7);
        for (int i = 0; i < 400; ++i) f.Tick(4000);
        f.AddEntity(219, "weapon_awp", 9);
        for (int i = 0; i < 500; ++i) f.Tick(4000);
        bool lastRetyped = false;
        for (int i = 0; i < f.s_worldMarkerCount; ++i)
            lastRetyped |= f.s_worldMarkers[i].type == Type::DroppedWeapon &&
                f.s_worldMarkers[i].sourceEntity == f.Entity(219) && f.s_worldMarkers[i].weaponId == 9;
        if (!lastRetyped) {
            std::cerr << "World last slot item=" << f.s_worldEntityItemIds[219] << " class="
                << static_cast<int>(f.s_worldEntityClassKinds[219]) << " tracked="
                << f.s_worldTrackedIndexPos[219] << " markers=" << f.s_worldMarkerCount
                << " definition reads=" << f.mem.readCounts[f.Entity(219) + 0x70]
                << " name reads=" << f.mem.readCounts[f.Entity(219) + 0x4003] << '\n';
            for (int slot : {67, 80, 100, 150, 200, 219})
                std::cerr << "slot=" << slot << " definition=" << f.mem.readCounts[f.Entity(slot) + 0x70]
                    << " position=" << f.mem.readCounts[f.Node(slot) + 0x80] << '\n';
        }
        CHECK(lastRetyped);
    }
    {
        auto fixture = std::make_unique<WorldPipelineReplay>();
        auto& f = *fixture;
        f.highestEntityIndex = 250;
        f.mem.Set(f.Entity(66) + f.ofs.C_BaseEntity_m_pGameSceneNode, uintptr_t{0});
        for (int i = 0; i < 150; ++i) f.Tick(4000);
        CHECK(f.Find(Type::Inferno, 46));
        f.AddEntity(66, "smokegrenade_projectile", 45);
        f.mem.Set(f.Entity(66) + f.ofs.C_BaseEntity_m_pGameSceneNode, uintptr_t{0});
        f.mem.Set(f.Entity(66) + f.ofs.C_SmokeGrenadeProjectile_m_nSmokeEffectTickBegin,
            static_cast<int>(f.currentGameTime * 64));
        f.mem.Set(f.Entity(66) + f.ofs.C_SmokeGrenadeProjectile_m_bDidSmokeEffect, uint8_t{1});
        f.mem.Set(f.Entity(66) + f.ofs.C_SmokeGrenadeProjectile_m_vSmokeDetonationPos,
            Vector3{1600, 1700, 50});
        for (int i = 0; i < 75; ++i) f.Tick(4000);
        bool reusedSmoke = false;
        for (int i = 0; i < f.s_worldMarkerCount; ++i)
            reusedSmoke |= f.s_worldMarkers[i].type == Type::Smoke &&
                f.s_worldMarkers[i].sourceEntity == f.Entity(66);
        CHECK(reusedSmoke);
        CHECK(!f.Find(Type::Inferno, 46));
    }
}

void TestWorldRoundAndFireContinuity() {
    using Type = esp::WorldMarkerType;
    for (int scenario = 0; scenario < 4; ++scenario) {
        auto fixture = std::make_unique<WorldPipelineReplay>();
        auto& f = *fixture;
        f.highestEntityIndex = 900;
        if (scenario == 0) {
            f.mem.Set(f.Entity(66) + f.ofs.C_BaseEntity_m_pGameSceneNode, uintptr_t{0});
            f.mem.partialAddress = f.Entity(66) + f.ofs.C_Inferno_m_firePositions;
            f.mem.partialBytes = sizeof(Vector3);
        } else if (scenario == 1) {
            f.ofs.C_Inferno_m_firePositions = 0;
            f.mem.Set(f.Node(66) + f.ofs.CGameSceneNode_m_vecAbsOrigin, Vector3{});
        }
        for (int i = 0; i < 150; ++i) f.Tick(4000);
        CHECK(f.Find(Type::Inferno, 46));
        CHECK(f.Find(Type::DroppedWeapon, 7));
        if (scenario < 2) continue;
        f.AddEntity(550, "weapon_awp", 9);
        f.mem.Set(0x200000 + 67 * f.s_activeEntitySlotSize, uintptr_t{0});
        if (scenario == 2) {
            f.bombRoundChangedThisTick = true;
            f.Tick(4000);
            f.bombRoundChangedThisTick = false;
        } else {
            f.currentGameTime = 2.0f;
        }
        f.mem.Set(f.Entity(65) + f.ofs.C_SmokeGrenadeProjectile_m_nSmokeEffectTickBegin,
            static_cast<int>(f.currentGameTime * 64));
        f.mem.Set(f.Entity(66) + f.ofs.C_Inferno_m_nFireEffectTickBegin,
            static_cast<int>(f.currentGameTime * 64));
        for (int i = 0; i < 150; ++i) f.Tick(4000);
        CHECK(!f.Find(Type::DroppedWeapon, 7));
        CHECK(f.Find(Type::DroppedWeapon, 9));
        CHECK(f.Find(Type::DroppedWeapon, 48));
        CHECK(f.Find(Type::Smoke, 45));
        CHECK(f.Find(Type::Inferno, 46));
        for (int i = 0; i < 300; ++i) {
            f.Tick(4000);
            CHECK(f.Find(Type::DroppedWeapon, 9));
            CHECK(f.Find(Type::Inferno, 46));
        }
    }
}

void TestWorldStabilityGuards() {
    using Type = esp::WorldMarkerType;
    for (int scenario = 0; scenario < 9; ++scenario) {
        auto fixture = std::make_unique<WorldPipelineReplay>();
        auto& f = *fixture;
        f.highestEntityIndex = 250;
        if (scenario == 3) {
            f.ofs.C_Inferno_m_firePositions = 0x4FD8;
            Vector3 points[64]{};
            for (auto& point : points) point = {600, 700, 50};
            f.mem.Set(f.Entity(66) + 0x4FD8, points);
            f.mem.Set(f.Entity(66) + f.ofs.C_Inferno_m_fireCount, int{64});
            f.mem.Set(f.Entity(66) + f.ofs.C_BaseEntity_m_pGameSceneNode, uintptr_t{0});
        }
        for (int i = 0; i < 150; ++i) f.Tick(4000);
        const auto* item = f.Find(Type::DroppedWeapon, 7);
        const auto* fire = f.Find(Type::Inferno, 46);
        CHECK(item && fire);
        const uint64_t itemSample = item ? item->sourceSampleUs : 0;
        const uint64_t fireDeadline = fire ? fire->expiresUs : 0;
        if (scenario < 2) {
            const auto address = scenario == 0 ? 0x200000 + 67 * f.s_activeEntitySlotSize : f.entityList + 0x10;
            const auto original = scenario == 0 ? f.Entity(67) : uintptr_t{0x200000};
            f.mem.Set(address, uintptr_t{0x4A5B6B90C8603008ULL});
            for (int i = 0; i < 25; ++i) { f.Tick(4000); CHECK(f.Find(Type::DroppedWeapon, 7)); }
            for (int i = 0; i < 100; ++i) f.Tick(4000);
            CHECK(!f.Find(Type::DroppedWeapon, 7));
            f.mem.Set(address, original);
            for (int i = 0; i < 25; ++i) f.Tick(4000);
            CHECK(f.Find(Type::DroppedWeapon, 7));
            if (scenario == 0) {
                f.mem.Set(address, uintptr_t{0});
                f.Tick(100000);
                CHECK(!f.Find(Type::DroppedWeapon, 7));
            }
        } else if (scenario == 2) {
            const auto stamp = f.s_worldUtilityFieldTimes[65][static_cast<size_t>(esp::data::UtilityField::SmokeActive)];
            f.mem.Set(f.Entity(65) + f.ofs.C_SmokeGrenadeProjectile_m_bDidSmokeEffect, uint8_t{255});
            f.mem.Set(f.Entity(66) + f.ofs.C_Inferno_m_nFireLifetime, std::nanf(""));
            f.mem.Set(f.Entity(66) + f.ofs.C_Inferno_m_fireCount, int{-1});
            f.mem.Set(f.Entity(66) + f.ofs.C_Inferno_m_bInPostEffectTime, uint8_t{255});
            for (int i = 0; i < 25; ++i) f.Tick(4000);
            CHECK(f.s_worldPrevSmokeActive[65] == 1);
            CHECK(f.s_worldPrevInfernoLife[66] == 7);
            CHECK(f.s_worldPrevInfernoFireCount[66] == 2);
            CHECK(f.s_worldPrevInfernoInPostEffect[66] == 0);
            CHECK(f.s_worldUtilityFieldTimes[65][static_cast<size_t>(esp::data::UtilityField::SmokeActive)] == stamp);
        } else if (scenario == 3) {
            Vector3 points[64]{};
            for (size_t i = 0; i < std::size(points); ++i) points[i] = {800.0f + static_cast<float>(i), 900, 50};
            f.mem.Set(f.Entity(66) + 0x4FD8, points);
            f.mem.Set(f.Entity(66) + f.ofs.C_Inferno_m_nFireEffectTickBegin, static_cast<int>(f.currentGameTime * 64));
            f.mem.unreadablePage = f.Entity(66) + 0x4000;
            for (int i = 0; i < 25; ++i) f.Tick(4000);
            fire = f.Find(Type::Inferno, 46);
            CHECK(fire && fire->position.x >= 800);
        } else if (scenario == 4) {
            f.freezeGameClock = true;
            for (int i = 0; i < 1500; ++i) {
                f.Tick(4000);
                fire = f.Find(Type::Inferno, 46);
                CHECK(!fire || fire->expiresUs <= fireDeadline);
            }
            CHECK(!f.Find(Type::Inferno, 46));
        } else if (scenario == 8) {
            const int batches = f.mem.batches;
            ++f.s_sceneResetSerial;
            f.s_lastSceneResetUs = f.nowUs + 100000;
            f.Tick(4000);
            CHECK(f.mem.batches == batches && f.s_worldMarkerCount == 0);
            f.Tick(2200000);
            f.Tick(100000);
            CHECK(f.Find(Type::DroppedWeapon, 7));
        } else {
            f.mem.mutateAtBatch = f.mem.batches + 1;
            if (scenario == 5) {
                f.mem.mutateAddress = 0x200000 + 67 * f.s_activeEntitySlotSize;
                f.mem.mutateValue = 0;
            } else if (scenario == 6) {
                f.mem.mutateAddress = f.Entity(67) + f.ofs.C_BaseEntity_m_pGameSceneNode;
                f.mem.mutateValue = f.Node(67) + 0x1000;
                f.mem.Set(f.mem.mutateValue + f.ofs.CGameSceneNode_m_vecAbsOrigin, Vector3{1300, 1400, 50});
                f.mem.Set(f.mem.mutateValue + 0x10, Vector3{1300, 1400, 50});
            } else {
                f.mem.mutateAddress = f.Entity(67) + f.ofs.C_BaseEntity_m_hOwnerEntity;
                f.mem.mutateValue = 0x8080;
            }
            f.Tick(300000);
            item = f.Find(Type::DroppedWeapon, 7);
            if (scenario == 6) {
                CHECK(!item || item->sourceSampleUs <= itemSample);
                f.Tick(100000);
                item = f.Find(Type::DroppedWeapon, 7);
                CHECK(item && item->position.x == 1300);
            } else CHECK(!item);
        }
    }
}

void TestWorldFaultStress() {
    auto fixture = std::make_unique<WorldPipelineReplay>();
    auto& f = *fixture;
    f.highestEntityIndex = 250;
    uint32_t random = 0x41C64E6Du;
    const uintptr_t slotAddress = 0x200000 + 67 * f.s_activeEntitySlotSize;
    for (int tick = 0; tick < 5000; ++tick) {
        random = random * 1664525u + 1013904223u;
        f.mem.shortAddress = 0;
        f.mem.partialAddress = 0;
        f.mem.unreadablePage = 0;
        f.mem.Set(slotAddress, f.Entity(67));
        f.mem.Set(f.Entity(67) + f.ofs.C_BaseEntity_m_hOwnerEntity, uint32_t{UINT32_MAX});
        f.bombRoundChangedThisTick = false;
        switch ((random >> 24) % 9) {
        case 0: f.mem.shortAddress = slotAddress; break;
        case 1: f.mem.shortAddress = f.Entity(67) + f.ofs.C_BaseEntity_m_hOwnerEntity; break;
        case 2: f.mem.Set(slotAddress, uintptr_t{0x4A5B6B90C8603008ULL}); break;
        case 3: f.mem.Set(f.Entity(67) + f.ofs.C_BaseEntity_m_hOwnerEntity, uint32_t{0x8080}); break;
        case 4: f.mem.Set(slotAddress, uintptr_t{0}); break;
        case 5:
            f.mem.partialAddress = f.Entity(66) + f.ofs.C_Inferno_m_firePositions;
            f.mem.partialBytes = (random & 31) * sizeof(Vector3);
            break;
        case 6: f.mem.shortAddress = f.Node(67) + f.ofs.CGameSceneNode_m_vecAbsOrigin; break;
        case 7:
            f.mem.Set(f.Entity(65) + f.ofs.C_SmokeGrenadeProjectile_m_nSmokeEffectTickBegin,
                static_cast<int>(f.currentGameTime * 64));
            f.mem.Set(f.Entity(66) + f.ofs.C_Inferno_m_nFireEffectTickBegin,
                static_cast<int>(f.currentGameTime * 64));
            break;
        default: break;
        }
        f.Tick(4000);
        CHECK(f.s_worldMarkerCount >= 0 && f.s_worldMarkerCount <= 256);
        for (int i = 0; i < f.s_worldMarkerCount; ++i) {
            const auto& marker = f.s_worldMarkers[i];
            CHECK(marker.valid && f.isValidWorldPos(marker.position));
            CHECK(marker.sourceSampleUs <= f.nowUs && marker.expiresUs > f.nowUs);
            CHECK(marker.sourceSlot >= 64 && marker.sourceSlot <= 250);
            for (int j = 0; j < i; ++j)
                CHECK(marker.sourceSlot != f.s_worldMarkers[j].sourceSlot || marker.type != f.s_worldMarkers[j].type);
        }
        if (tick % 500 == 499) {
            f.s_engineInGame = false;
            f.Tick(4000);
            CHECK(f.s_worldMarkerCount == 0);
            f.s_engineInGame = true;
        }
    }
    f.mem.shortAddress = 0;
    f.mem.partialAddress = 0;
    f.mem.Set(slotAddress, f.Entity(67));
    f.mem.Set(f.Entity(67) + f.ofs.C_BaseEntity_m_hOwnerEntity, uint32_t{UINT32_MAX});
    for (int i = 0; i < 150; ++i) f.Tick(4000);
    CHECK(f.Find(esp::WorldMarkerType::DroppedWeapon, 7));
}

