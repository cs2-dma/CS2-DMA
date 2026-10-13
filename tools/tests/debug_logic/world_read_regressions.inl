void TestWorldIdentityReplay()
{
    using namespace esp::data;
    for (int scenario = 0; scenario < 11; ++scenario) {
        constexpr size_t capacity = 80;
        PlayerRepairReplay::Reader mem;
        struct Offsets {
            uintptr_t C_BaseEntity_m_pGameSceneNode = 0x10, C_BaseEntity_m_hOwnerEntity = 0x20;
            uintptr_t C_BaseEntity_m_nSubclassID = 0x30, CEntityInstance_m_pEntity = 0x40;
            uintptr_t C_EconEntity_m_AttributeManager = 0x50, C_AttributeContainer_m_Item = 0x10;
            uintptr_t C_EconItemView_m_iItemDefinitionIndex = 0x10, CEntityIdentity_m_designerName = 0x20;
        } ofs;
        uintptr_t worldEntities[capacity] = {}, s_worldEntityRefs[capacity] = {};
        uintptr_t worldSceneNodes[capacity] = {}, s_cachedWorldSceneNodes[capacity] = {};
        uintptr_t worldIdentities[capacity] = {}, worldDesignerNamePtrs[capacity] = {};
        char worldDesignerNames[capacity][48] = {};
        uint32_t worldOwnerHandles[capacity] = {}, s_cachedWorldOwnerHandles[capacity] = {};
        uint32_t worldSubclassIds[capacity] = {}, s_worldEntitySubclassIds[capacity] = {};
        uint16_t worldItemDefs[capacity] = {}, s_worldEntityItemIds[capacity] = {};
        uint8_t worldEntityReadComplete[capacity] = {}, worldBasicDetailsComplete[capacity] = {};
        uint8_t worldDetailsReadQueued[capacity] = {}, worldClassKinds[capacity] = {};
        uint8_t s_worldEntityClassKinds[capacity] = {}, worldSceneNodeChanged[capacity] = {};
        uint8_t worldSceneNodeRefreshRequested[capacity] = {}, s_identityRefreshMask[capacity] = {};
        uint8_t s_positionReadAttemptedMask[capacity] = {};
        DWORD worldBlockReadBytes[1] = {sizeof(uintptr_t)}, worldEntityReadBytes[capacity] = {};
        DWORD worldSceneNodeReadBytes[capacity] = {}, worldOwnerReadBytes[capacity] = {};
        DWORD worldSubclassReadBytes[capacity] = {}, worldItemDefReadBytes[capacity] = {};
        DWORD worldIdentityReadBytes[capacity] = {}, worldDesignerNamePtrReadBytes[capacity] = {};
        DWORD worldDesignerNameReadBytes[capacity] = {};
        const int s_worldCandidateIndices[] = {65, 66};
        const int worldCandidateCount = 2, blockCount = 1, handle = 0, cachedIdentityRefreshCount = 0;
        constexpr uint16_t kWeaponC4Id = 49;
        const bool worldScanOk = true, bombOnlyWorldMode = false, wantsWorldOwnerHandles = true;
        const bool wantsWorldIdentityData = true, wantsWorldUtilityData = true;
        const bool worldDomainDroppedItemsDue = false, worldDomainBombRescueDue = false;
        bool worldScanC4ReadIncomplete = false;
        const auto isLikelyGamePointer = [](uintptr_t pointer) { return pointer >= 0x10000; };
        const auto needsWorldOwnerRefreshForItemId = [](uint16_t) { return true; };
        const auto rememberEntityClass = [](uint32_t, WorldEntityClass) {};
        const auto executeOptionalScatterRead = [&]() { return mem.Execute(); };
        const auto resetWorldScratchSlot = [&](int idx) {
            worldEntities[idx] = worldSceneNodes[idx] = worldIdentities[idx] = worldDesignerNamePtrs[idx] = 0;
            std::memset(worldDesignerNames[idx], 0, sizeof(worldDesignerNames[idx]));
            worldOwnerHandles[idx] = worldSubclassIds[idx] = worldItemDefs[idx] = 0;
            worldEntityReadBytes[idx] = worldSceneNodeReadBytes[idx] = worldIdentityReadBytes[idx] = 0;
            worldOwnerReadBytes[idx] = worldSubclassReadBytes[idx] = worldItemDefReadBytes[idx] = 0;
            worldDesignerNamePtrReadBytes[idx] = worldDesignerNameReadBytes[idx] = 0;
            worldEntityReadComplete[idx] = worldBasicDetailsComplete[idx] = worldDetailsReadQueued[idx] = 0;
            worldClassKinds[idx] = worldSceneNodeChanged[idx] = worldSceneNodeRefreshRequested[idx] = 0;
        };
        for (int slot : s_worldCandidateIndices) {
            const uintptr_t entity = 0x100000 + (slot - 65) * 0x10000;
            const uintptr_t identity = entity + 0x200000;
            const uintptr_t name = entity + 0x300003;
            worldEntities[slot] = entity;
            worldEntityReadBytes[slot] = sizeof(uintptr_t);
            if (scenario != 0) s_worldEntityRefs[slot] = entity;
            s_worldEntitySubclassIds[slot] = 42;
            if (scenario == 7)
                s_worldEntityClassKinds[slot] = static_cast<uint8_t>(WorldEntityClass::DroppedWeapon);
            mem.Set(entity + 0x10, uintptr_t{scenario == 5 ? 0u : 0x200000u});
            mem.Set(entity + 0x20, uint32_t{UINT32_MAX});
            mem.Set(entity + 0x30, uint32_t{42});
            mem.Set(entity + 0x40, identity);
            mem.Set(entity + 0x70, static_cast<uint16_t>(scenario == 10 ? 65535 : 0));
            mem.Set(identity + 0x20, name);
            char text[48] = {};
            strcpy_s(text, scenario >= 8 ? (slot == 65 ? "weapon_ak47" : "weapon_incgrenade") :
                (slot == 65 ? "smokegrenade_projectile" : "inferno"));
            mem.Set(name, text);
        }
        if (scenario == 2) mem.shortAddress = 0x100010;
        if (scenario == 3) {
            mem.Set(0x100030, uint32_t{0x12340077});
            mem.shortAddress = 0x100030;
        }
        if (scenario == 4) mem.shortAddress = 0x100040;
        #include "../../../src/Features/ESP/DataReader/world_parts/world_read_identity.inl"
        CHECK(worldDetailsReadQueued[65] == 1 && worldDetailsReadQueued[66] == 1);
        CHECK(worldEntityReadComplete[65] == 1 && newEntityCount == 2);
        CHECK(worldBasicDetailsComplete[65] == (scenario != 2 && scenario != 3 && scenario != 5));
        CHECK(worldBasicDetailsComplete[66] == (scenario != 5));
        CHECK(dynamicRefreshCount == (scenario == 5 ? 0 : scenario == 2 || scenario == 3 ? 1 : 2));
        CHECK(worldClassKinds[65] == static_cast<uint8_t>(scenario >= 8 ? WorldEntityClass::DroppedWeapon :
            scenario == 4 ? WorldEntityClass::Unknown : WorldEntityClass::SmokeProjectile));
        CHECK(worldClassKinds[66] == static_cast<uint8_t>(scenario >= 8 ? WorldEntityClass::DroppedWeapon : WorldEntityClass::Inferno));
        if (scenario >= 8) {
            CHECK(worldItemDefs[65] == 7);
            CHECK(worldItemDefs[66] == 48);
        }
        CHECK(!worldScanC4ReadIncomplete);
        if (scenario == 3) CHECK(worldSubclassIds[65] == 42);
    }
    Vector3 output = {};
    const auto valid = [](const Vector3& p) {
        return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) &&
            std::fabs(p.x) + std::fabs(p.y) + std::fabs(p.z) > 1;
    };
    CHECK(ResolveSmokeOrigin(Vector3{200, 300, 40}, 12, 1, 1, 0, 0, output, valid));
    CHECK(output.x == 200 && output.z == 40);
    CHECK(!ResolveSmokeOrigin(Vector3{200, 300, 40}, 11, 1, 1, 0, 0, output, valid));
    CHECK(!ResolveSmokeOrigin(Vector3{200, 300, 40}, 12, 1, 0, 0, 0, output, valid));
    CHECK(!ResolveSmokeOrigin(Vector3{200, 300, 40}, 12, 0, 1, 0, 1, output, valid));
    CHECK(!ResolveSmokeOrigin(Vector3{200, 300, 40}, 12, 2, 1, 2, 1, output, valid));
    CHECK(!ResolveSmokeOrigin(Vector3{NAN, 300, 40}, 12, 1, 1, 1, 1, output, valid));
    CHECK(ResolveSmokeOrigin(Vector3{200, 300, 40}, 12, 0, 0, 1, 1, output, valid));
    CHECK(esp::weapons::WeaponItemIdFromDesignerName("weapon_hkp2000") == 32);
    CHECK(esp::weapons::WeaponItemIdFromDesignerName("weapon_healthshot") == 57);
    CHECK(CanonicalWorldItemId(60, WorldEntityClass::DroppedWeapon, 16) == 60);
    CHECK(IsDroppedWorldItem(7, WorldEntityClass::DroppedWeapon));
    for (uint16_t item = 43; item <= 48; ++item)
        CHECK(IsDroppedWorldItem(item, WorldEntityClass::DroppedWeapon));
    CHECK(!IsDroppedWorldItem(46, WorldEntityClass::Inferno));
    CHECK(!IsDroppedWorldItem(46, WorldEntityClass::MolotovProjectile));
    CHECK(!IsDroppedWorldItem(0, WorldEntityClass::DroppedWeapon));
    CHECK(!IsDroppedWorldItem(49, WorldEntityClass::DroppedWeapon));
    CHECK(!IsDroppedWorldItem(7, WorldEntityClass::Unknown));
}
