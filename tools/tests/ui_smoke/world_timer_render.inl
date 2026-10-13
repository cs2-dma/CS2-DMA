void TestWorldTimerRendering()
{
    using esp::WorldMarker;
    using esp::WorldMarkerType;
    using namespace esp::weapons;
    const auto saved = g::espSettings;
    g::espWorld = true;
    g::espWorldSmokeTimer = g::espWorldInfernoTimer = true;
    g::espWorldSmokeTimerColor[0] = 0.1f;
    g::espWorldInfernoTimerColor[0] = 0.9f;
    const auto ColorToImU32 = [](const float* c) {
        return ImGui::ColorConvertFloat4ToU32(ImVec4(c[0], c[1], c[2], c[3]));
    };
    const auto PickWeaponIconFont = [](float) { return ImGui::GetFont(); };
    const auto WorldMarkerName = [](WorldMarkerType type, uint16_t) {
        return type == WorldMarkerType::Smoke ? "Smoke" : "Molotov";
    };
    const auto WorldToScreen = [](const Vector3& p, int, float, float) {
        return ScreenPos{p.x, p.y, true};
    };
    auto* drawList = ImGui::GetWindowDrawList();
    const float screenW = ImGui::GetIO().DisplaySize.x, screenH = ImGui::GetIO().DisplaySize.y;
    const int viewMatrix = 0, worldMarkerCount = 2;
    const bool localHasBomb = false;
    esp::BombState bombState;
    esp::PlayerData players[64] = {};
    const ImU32 worldCol = IM_COL32_WHITE;
    WorldMarker worldMarkers[2];
    worldMarkers[0].type = WorldMarkerType::Smoke;
    worldMarkers[1].type = WorldMarkerType::Inferno;
    for (int i = 0; i < 2; ++i) {
        worldMarkers[i].valid = true;
        worldMarkers[i].position = {screenW * 0.5f, 100.0f + i * 40.0f, 40};
        worldMarkers[i].expiresUs = 6000000;
    }
    struct Text { std::string value; ImU32 color; ImVec2 point; };
    std::vector<Text> texts;
    const auto DrawTextShadow = [&](ImDrawList* list, ImFont*, float, ImVec2 point, ImU32 color, const char* text) {
        texts.push_back({text, color, point});
        list->AddText(point, color, text);
    };
    uint64_t nowUs = 1000000;
    for (int scenario = 0; scenario < 3; ++scenario) {
        texts.clear();
        g::espWorldSmokeTimer = g::espWorldInfernoTimer = scenario != 1;
        if (scenario == 2) nowUs = 6000000;
        #include "../../../src/Features/ESP/Render/world_esp.inl"
        CHECK(texts.size() == (scenario == 0 ? 4u : scenario == 1 ? 2u : 0u));
        if (scenario == 0 && texts.size() == 4) {
            CHECK(texts[1].value == " 5.0s" && texts[3].value == " 5.0s");
            CHECK(texts[1].color == ColorToImU32(g::espWorldSmokeTimerColor));
            CHECK(texts[3].color == ColorToImU32(g::espWorldInfernoTimerColor));
            CHECK(texts[0].color == IM_COL32(180, 180, 220, 255));
            CHECK(texts[2].color == IM_COL32(255, 120, 40, 255));
            for (int i : {0, 2}) {
                const float nameWidth = ImGui::CalcTextSize(texts[i].value.c_str()).x;
                const float timerWidth = ImGui::CalcTextSize(texts[i + 1].value.c_str()).x;
                CHECK(std::fabs(texts[i].point.x + nameWidth - texts[i + 1].point.x) < 0.01f);
                CHECK(std::fabs(texts[i].point.x + (nameWidth + timerWidth) * 0.5f - screenW * 0.5f) < 0.01f);
            }
        }
    }
    nowUs = 1000000;
    g::espEnabled = false;
    g::espWorld = false;
    g::espItem = g::espItemText = true;
    g::espItemIcon = g::espItemHighlight = false;
    CHECK(esp::render::HasOverlayDrawDemand(g::espEnabled, g::espWorld, g::espItem,
        false, false, false, false));
    for (int i = 0; i < 2; ++i) {
        worldMarkers[i].type = WorldMarkerType::DroppedWeapon;
        worldMarkers[i].weaponId = i == 0 ? 7 : 48;
        g::espItemEnabledMask.set(worldMarkers[i].weaponId);
    }
    texts.clear();
    {
        #include "../../../src/Features/ESP/Render/world_esp.inl"
    }
    CHECK(texts.size() == 2);
    g::espItemEnabledMask.reset(48);
    texts.clear();
    {
        #include "../../../src/Features/ESP/Render/world_esp.inl"
    }
    CHECK(texts.size() == 1);
    g::espSettings = saved;
}
