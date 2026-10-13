struct PlayerRepairReplay {
    struct Reader {
        struct Request { uintptr_t address; void* output; size_t size; DWORD* completed; };
        std::map<uintptr_t, std::vector<uint8_t>> memory;
        std::vector<Request> requests;
        uintptr_t shortAddress = 0;
        uintptr_t mutateAddress = 0;
        uintptr_t mutateValue = 0;
        int mutateAtBatch = 0;
        int batches = 0;
        template<class T> void Set(uintptr_t address, const T& value) {
            auto& bytes = memory[address];
            bytes.resize(sizeof(T));
            std::memcpy(bytes.data(), &value, sizeof(T));
        }
        void AddScatterReadRequest(int, uintptr_t address, void* output, size_t size, DWORD* completed) {
            *completed = 0;
            requests.push_back({address, output, size, completed});
        }
        bool Execute() {
            ++batches;
            for (auto request : requests) {
                auto found = memory.find(request.address);
                if (found == memory.end() || found->second.size() < request.size)
                    continue;
                const size_t bytes = request.address == shortAddress ? 1 : request.size;
                std::memcpy(request.output, found->second.data(), bytes);
                *request.completed = static_cast<DWORD>(bytes);
            }
            if (mutateAddress && (mutateAtBatch == 0 || batches == mutateAtBatch)) {
                Set(mutateAddress, mutateValue);
                mutateAddress = 0;
            }
            requests.clear();
            return true;
        }
    } mem;
    struct Offsets {
        uintptr_t C_BaseEntity_m_pGameSceneNode = 0x10;
        uintptr_t CGameSceneNode_m_vecAbsOrigin = 0x20;
        uintptr_t C_BasePlayerPawn_m_hController = 0x30;
        uintptr_t CCSPlayerController_m_hPlayerPawn = 0x40;
        uintptr_t C_BaseEntity_m_iHealth = 0x50;
        uintptr_t C_BaseEntity_m_lifeState = 0x60;
    } ofs;
    std::atomic<uint64_t> s_sceneResetSerial{0}, s_lastSceneResetUs{0};
    std::atomic<int> s_playerHierarchyHighWaterSlot{10};
    std::atomic<uint64_t> s_playerCoreGlobalRefreshAvoidedCount{0};
    int playerSlotScanLimit = 10, playerResolvedSlotCount = 10, handle = 0;
    int playerResolvedSlots[64] = {}, healths[64] = {}, armors[64] = {}, teams[64] = {};
    uintptr_t pawns[64] = {}, controllers[64] = {};
    uint32_t pawnHandles[64] = {};
    Vector3 positions[64] = {};
    uint8_t lifeStates[64] = {}, gunGameImmunityFlags[64] = {};
    DWORD gunGameImmunityBytesRead[64] = {}, corePositionBytesRead[64] = {};
    DWORD healthBytes[64] = {}, lifeBytes[64] = {};
    bool coreReadFresh[64] = {}, coreReadPlausible[64] = {}, coreReadAlive[64] = {};
    bool gunGameImmunityReadFresh[64] = {};
    bool sceneSettling = false, coreAnomalyEligible = true, wantsGunGameImmunity = false;
    uintptr_t localPawn = 0x100000;
    uint32_t localControllerPawnHandle = 65;
    uint64_t nowUs = 10000000u;
    int probeCount = 0, repairCount = 0;
    using DmaRefreshTier = esp::DmaRefreshTier;
    static constexpr uint32_t kEntityHandleMask = 0x7FFFu;

    PlayerRepairReplay() {
        static uint64_t serial = 100;
        s_sceneResetSerial = ++serial;
        for (int slot = 0; slot < 10; ++slot) {
            playerResolvedSlots[slot] = slot;
            pawns[slot] = 0x100000u + slot * 0x1000u;
            controllers[slot] = 0x200000u + slot * 0x1000u;
            pawnHandles[slot] = 0x8000u + 65u + slot;
            teams[slot] = 3;
            armors[slot] = 100;
            mem.Set(pawns[slot] + ofs.C_BaseEntity_m_iHealth, 100);
            mem.Set(pawns[slot] + ofs.C_BaseEntity_m_lifeState, uint8_t{0});
            mem.Set(pawns[slot] + 0x70, Vector3{100.f + slot, 200.f, 50.f});
            const uintptr_t node = 0x300000u + slot * 0x1000u;
            mem.Set(pawns[slot] + ofs.C_BaseEntity_m_pGameSceneNode, node);
            mem.Set(node + ofs.CGameSceneNode_m_vecAbsOrigin, Vector3{100.f + slot, 200.f, 50.f});
            mem.Set(controllers[slot] + ofs.CCSPlayerController_m_hPlayerPawn, pawnHandles[slot]);
            mem.Set(pawns[slot] + ofs.C_BasePlayerPawn_m_hController, uint32_t{0x8001u + slot});
        }
    }
    uint64_t TickNowUs() const { return nowUs; }
    bool isLikelyGamePointer(uintptr_t value) const { return value >= 0x10000u; }
    bool isValidWorldPos(const Vector3& v) const {
        return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z) &&
            std::fabs(v.x) <= 32768 && std::fabs(v.y) <= 32768 && std::fabs(v.z) <= 16384 &&
            std::fabs(v.x) + std::fabs(v.y) + std::fabs(v.z) > 1;
    }
    bool teamLooksValid(int value) const { return value == 2 || value == 3; }
    bool coreReadCompleted(int i) const {
        return healthBytes[i] == sizeof(int) && lifeBytes[i] == sizeof(uint8_t) &&
            corePositionBytesRead[i] == sizeof(Vector3);
    }
    bool coreStateLooksSane(int i) const {
        return esp::data::IsPlayerCoreStatePlausible(teamLooksValid(teams[i]),
            healths[i], armors[i], lifeStates[i], isValidWorldPos(positions[i]));
    }
    void markCoreReadResult(int i) {
        coreReadPlausible[i] = coreReadFresh[i] = coreReadCompleted(i) && coreStateLooksSane(i);
        coreReadAlive[i] = coreReadPlausible[i] && healths[i] > 0 && lifeStates[i] == 0;
    }
    void markPlayerFlagFreshness(int) {}
    void cacheCoreAuxState(int) {}
    void markCoreAvailability(int, bool) {}
    bool executeOptionalScatterRead() { return mem.Execute(); }
    void refreshDmaCaches(const char*, DmaRefreshTier tier) {
        probeCount += tier == DmaRefreshTier::Probe;
        repairCount += tier == DmaRefreshTier::Repair;
    }
    void queueMandatoryCoreReads(int i, uint64_t, bool) {
        mem.AddScatterReadRequest(handle, pawns[i] + ofs.C_BaseEntity_m_iHealth,
            &healths[i], sizeof(int), &healthBytes[i]);
        mem.AddScatterReadRequest(handle, pawns[i] + ofs.C_BaseEntity_m_lifeState,
            &lifeStates[i], sizeof(uint8_t), &lifeBytes[i]);
        mem.AddScatterReadRequest(handle, pawns[i] + 0x70,
            &positions[i], sizeof(Vector3), &corePositionBytesRead[i]);
    }
    void applyCoreBatchResults() {
        for (int i = 0; i < playerResolvedSlotCount; ++i) markCoreReadResult(i);
    }
    void Read() {
        for (int i = 0; i < playerResolvedSlotCount; ++i) queueMandatoryCoreReads(i, nowUs, false);
        mem.Execute();
        applyCoreBatchResults();
    }
    void Repair() {
        #include "../../../src/Features/ESP/DataReader/player_parts/player_repair.inl"
    }
    void Tick() { nowUs += 4000; Read(); Repair(); }
};

void TestPlayerRepairReplay()
{
    PlayerRepairReplay healthy;
    healthy.Tick();
    CHECK(healthy.mem.batches == 1);
    CHECK(healthy.probeCount == 0 && healthy.repairCount == 0);
    for (int scenario = 0; scenario < 6; ++scenario) {
        PlayerRepairReplay replay;
        replay.mem.Set(replay.pawns[4] + 0x70, Vector3{NAN, NAN, NAN});
        replay.Tick();
        replay.nowUs += 4000;
        replay.Read();
        if (scenario == 1)
            replay.mem.Set(replay.controllers[4] + replay.ofs.CCSPlayerController_m_hPlayerPawn,
                uint32_t{replay.pawnHandles[4] + 0x8000u});
        if (scenario == 2)
            replay.mem.Set(replay.pawns[4] + replay.ofs.C_BasePlayerPawn_m_hController, uint32_t{0x8001u});
        if (scenario == 3)
            replay.mem.shortAddress = 0x304000u + replay.ofs.CGameSceneNode_m_vecAbsOrigin;
        if (scenario == 4) {
            replay.mem.mutateAddress = replay.pawns[4] + replay.ofs.C_BaseEntity_m_iHealth;
            replay.mem.mutateValue = 0;
            replay.mem.mutateAtBatch = 4;
        }
        if (scenario == 5) {
            replay.mem.mutateAddress = replay.pawns[4] + replay.ofs.C_BaseEntity_m_pGameSceneNode;
            replay.mem.mutateValue = 0;
            replay.mem.mutateAtBatch = 4;
        }
        replay.Repair();
        if (scenario == 0) {
            CHECK(replay.coreReadPlausible[4]);
            CHECK(replay.positions[4].x == 104.f);
            CHECK(replay.coreReadAlive[4]);
        } else if (scenario == 4) {
            CHECK(replay.coreReadPlausible[4]);
            CHECK(replay.healths[4] == 0);
            CHECK(!replay.coreReadAlive[4]);
        } else {
            CHECK(!replay.coreReadPlausible[4]);
        }
        CHECK(replay.coreReadPlausible[3]);
        CHECK(replay.coreReadPlausible[9]);
    }
    PlayerRepairReplay persistent;
    for (int slot : {4, 9}) {
        persistent.mem.Set(persistent.pawns[slot] + 0x70, Vector3{NAN, NAN, NAN});
        persistent.mem.Set(persistent.pawns[slot] + persistent.ofs.C_BaseEntity_m_pGameSceneNode, uintptr_t{0});
    }
    for (int tick = 0; tick < 1100; ++tick) persistent.Tick();
    CHECK(persistent.probeCount == 1);
    CHECK(persistent.repairCount == 1);
    CHECK(!persistent.coreReadPlausible[4] && !persistent.coreReadPlausible[9]);
    CHECK(persistent.coreReadPlausible[0] && persistent.coreReadPlausible[8]);
    for (int slot : {4, 9})
        persistent.mem.Set(persistent.pawns[slot] + 0x70, Vector3{104.f, 200.f, 50.f});
    persistent.Tick();
    CHECK(persistent.coreReadPlausible[4] && persistent.coreReadPlausible[9]);

    PlayerRepairReplay sceneRecovery;
    sceneRecovery.mem.Set(sceneRecovery.pawns[4] + 0x70, Vector3{NAN, NAN, NAN});
    for (int tick = 0; tick < 1600; ++tick)
        sceneRecovery.Tick();
    CHECK(sceneRecovery.probeCount == 0 && sceneRecovery.repairCount == 0);
    sceneRecovery.mem.Set(sceneRecovery.pawns[4] + sceneRecovery.ofs.C_BaseEntity_m_iHealth, 0);
    sceneRecovery.Tick();
    CHECK(!sceneRecovery.coreReadAlive[4]);
}
