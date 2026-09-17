if (g::radarSpectatorList &&
    esp::render::ShouldRenderOverlayForMenuState(
        g::menuOpen,
        g::radarSpectatorListShowWithMenu)) {
    std::array<const SpectatorEntry*, 64> visibleSpectators = {};
    int visibleSpectatorCount = 0;
    const int publishedSpectatorCount = std::clamp(snap.spectatorCount, 0, 64);
    for (int i = 0; i < publishedSpectatorCount; ++i) {
        if (snap.spectators[i].valid)
            visibleSpectators[visibleSpectatorCount++] = &snap.spectators[i];
    }
    std::sort(
        visibleSpectators.begin(),
        visibleSpectators.begin() + visibleSpectatorCount,
        [](const SpectatorEntry* left, const SpectatorEntry* right) {
            if (left->targetIsLocal != right->targetIsLocal)
                return left->targetIsLocal;
            return std::strcmp(left->name, right->name) < 0;
        });

    if (visibleSpectatorCount > 0 || g::menuOpen) {
        ImGuiWindowFlags windowFlags =
            ImGuiWindowFlags_NoDecoration |
            ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoBackground;
        if (!g::menuOpen)
            windowFlags |= ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoMove;

        static bool s_spectatorPositionApplied = false;
        if (g::configJustLoaded)
            s_spectatorPositionApplied = false;
        const bool forcePos = !s_spectatorPositionApplied;
        ImGui::SetNextWindowPos(
            ImVec2(g::radarSpectatorListX, g::radarSpectatorListY),
            forcePos ? ImGuiCond_Always : ImGuiCond_Once);
        s_spectatorPositionApplied = true;
        ImGui::SetNextWindowSizeConstraints(ImVec2(284.0f, 0.0f), ImVec2(284.0f, 540.0f));
        ImGui::SetNextWindowBgAlpha(0.0f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_BorderShadow, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(11.0f, 9.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(5.0f, 4.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        const bool useSpectatorFont = g::fontOverlayText != nullptr;
        if (useSpectatorFont)
            ImGui::PushFont(g::fontOverlayText);
        if (ImGui::Begin("##kevqdma_spectator_list", nullptr, windowFlags)) {
            static bool s_spectatorWindowDragging = false;
            if (g::menuOpen) {
                if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) &&
                    ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                    s_spectatorWindowDragging = true;
                }

                if (s_spectatorWindowDragging && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                    const ImVec2 delta = ImGui::GetIO().MouseDelta;
                    g::radarSpectatorListX += delta.x;
                    g::radarSpectatorListY += delta.y;
                    ImGui::SetWindowPos(
                        ImVec2(g::radarSpectatorListX, g::radarSpectatorListY),
                        ImGuiCond_Always);
                } else if (s_spectatorWindowDragging) {
                    s_spectatorWindowDragging = false;
                    config::SaveAsync();
                }
            }

            ImDrawList* spectatorDrawList = ImGui::GetWindowDrawList();
            const auto drawSpectatorText = [](
                ImDrawList* drawList,
                const ImVec2& pos,
                ImU32 color,
                const char* text) {
                ImFont* font = ImGui::GetFont();
                const float fontSize = ImGui::GetFontSize();
                drawList->AddText(
                    font,
                    fontSize,
                    ImVec2(pos.x + 1.0f, pos.y + 1.0f),
                    IM_COL32(0, 0, 0, 190),
                    text);
                drawList->AddText(font, fontSize, pos, color, text);
            };

            const ImVec2 titlePos = ImGui::GetCursorScreenPos();
            spectatorDrawList->AddRectFilled(
                ImVec2(titlePos.x, titlePos.y + 1.0f),
                ImVec2(titlePos.x + 3.0f, titlePos.y + 17.0f),
                visibleSpectatorCount > 0
                    ? IM_COL32(57, 139, 255, 240)
                    : IM_COL32(116, 127, 144, 150),
                2.0f);
            char title[48] = {};
            std::snprintf(
                title,
                sizeof(title),
                KEVQ_TR("Spectators %d"),
                visibleSpectatorCount);
            drawSpectatorText(
                spectatorDrawList,
                ImVec2(titlePos.x + 10.0f, titlePos.y),
                visibleSpectatorCount > 0
                    ? IM_COL32(229, 240, 255, 255)
                    : IM_COL32(174, 183, 197, 215),
                title);
            ImGui::Dummy(ImVec2(262.0f, ImGui::GetTextLineHeight() + 5.0f));

            const ImVec2 separatorPos = ImGui::GetCursorScreenPos();
            spectatorDrawList->AddLine(
                separatorPos,
                ImVec2(separatorPos.x + 262.0f, separatorPos.y),
                IM_COL32(62, 78, 102, 125),
                1.0f);
            ImGui::Dummy(ImVec2(262.0f, 3.0f));

            for (int i = 0; i < visibleSpectatorCount; ++i) {
                const SpectatorEntry& spectator = *visibleSpectators[i];
                const ImVec2 rowPos = ImGui::GetCursorScreenPos();
                spectatorDrawList->AddCircleFilled(
                    ImVec2(rowPos.x + 5.0f, rowPos.y + 8.0f),
                    2.25f,
                    spectator.targetIsLocal
                        ? IM_COL32(255, 112, 82, 245)
                        : IM_COL32(115, 139, 170, 205));

                char label[256] = {};
                ImU32 textColor = IM_COL32(204, 218, 237, 238);
                if (spectator.targetIsLocal) {
                    std::snprintf(
                        label,
                        sizeof(label),
                        "%s  ->  %s",
                        spectator.name[0] ? spectator.name : KEVQ_TR("Player"),
                        KEVQ_TR("You"));
                    textColor = IM_COL32(255, 170, 139, 255);
                } else if (spectator.targetName[0]) {
                    std::snprintf(
                        label,
                        sizeof(label),
                        "%s  ->  %s",
                        spectator.name[0] ? spectator.name : KEVQ_TR("Player"),
                        spectator.targetName);
                } else {
                    std::snprintf(
                        label,
                        sizeof(label),
                        "%s  [%s]",
                        spectator.name[0] ? spectator.name : KEVQ_TR("Player"),
                        KEVQ_TR("Free camera"));
                    textColor = IM_COL32(145, 157, 176, 195);
                }
                drawSpectatorText(
                    spectatorDrawList,
                    ImVec2(rowPos.x + 14.0f, rowPos.y + 1.0f),
                    textColor,
                    label);
                ImGui::Dummy(ImVec2(262.0f, ImGui::GetTextLineHeight() + 5.0f));
            }
        }
        ImGui::End();
        if (useSpectatorFont)
            ImGui::PopFont();
        ImGui::PopStyleVar(4);
        ImGui::PopStyleColor(3);
    }
}
