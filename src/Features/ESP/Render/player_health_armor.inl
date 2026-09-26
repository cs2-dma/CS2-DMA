ImFont* barValueFont = g::fontBarValues ? g::fontBarValues : ImGui::GetFont();
auto barLayout = esp::render::DrawPlayerBars(*drawList, barValueFont, g::fontUiIcons,
    ImVec2(boxLeft, boxTop), ImVec2(boxLeft+boxWidth, boxTop+boxHeight),
    ImVec2(0,0), ImVec2(screenW,screenH), p.health, p.armor,
    healthTrails[i].Update((static_cast<uint64_t>(p.pawnHandle)<<32) ^ p.pawn,
        snap.sceneSerial,p.health,nowUs),g::espSettings);
