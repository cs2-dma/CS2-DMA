if (g::espEnabled && g::espBombInfo && (bombState.planted || bombState.dropped)) {
    const bool plantedBomb = bombState.planted;
    const Vector3 renderBombPosition = bombState.position; // Smoothed render-only copy.
    const bool blowTimerValid =
        std::isfinite(bombState.blowTime) &&
        std::isfinite(bombState.currentGameTime) &&
        bombState.blowTime > bombState.currentGameTime &&
        bombState.blowTime < bombState.currentGameTime + 120.0f;
    const bool defuseTimerValid =
        std::isfinite(bombState.defuseEndTime) &&
        std::isfinite(bombState.currentGameTime) &&
        bombState.defuseEndTime > bombState.currentGameTime &&
        bombState.defuseEndTime < bombState.currentGameTime + 30.0f;
    const float blowLeft = blowTimerValid ? (bombState.blowTime - bombState.currentGameTime) : -1.0f;
    const float defuseLeft = defuseTimerValid ? (bombState.defuseEndTime - bombState.currentGameTime) : -1.0f;
    const float defuseTotal =
        esp::render::ResolveBombDefuseTotal(bombState.defuseLength);
    const esp::render::BombDefuseOutcome defuseOutcome =
        esp::render::EvaluateBombDefuseOutcome(
            bombState.defuseEndTime,
            bombState.blowTime);
    const ImU32 defuseColor =
        defuseOutcome == esp::render::BombDefuseOutcome::Completes
            ? IM_COL32(72, 235, 108, 245)
            : defuseOutcome == esp::render::BombDefuseOutcome::Explodes
                ? IM_COL32(255, 82, 72, 245)
                : IM_COL32(255, 194, 72, 245);

    const float bombFontSz = g::espBombTextSize > 0.0f ? g::espBombTextSize : 0.0f;
    ImFont* bombFont = ImGui::GetFont();
    const float bombFsUse = bombFontSz > 0.0f ? bombFontSz : ImGui::GetFontSize();
    char defText[32] = {};
    char blowText[24] = {};

    if (g::espBombText && !g::espBombTime && plantedBomb && bombState.beingDefused && defuseLeft > 0.0f) {
        std::snprintf(
            defText,
            sizeof(defText),
            KEVQ_TR("Defuse %.1fs"),
            static_cast<double>(defuseLeft));
        const ImVec2 defSize = bombFont->CalcTextSizeA(bombFsUse, FLT_MAX, 0.0f, defText);
        DrawTextShadow(
            drawList,
            nullptr,
            bombFontSz,
            ImVec2(screenW * 0.5f - defSize.x * 0.5f, 50.0f),
            defuseColor,
            defText);
    }

    const bool hasBombPos = isValidWorldPos(renderBombPosition);
    ImVec2 boxMin = {};
    ImVec2 boxMax = {};
    bool hasScreenBox = false;

    if (hasBombPos && bombState.boundsValid) {
        hasScreenBox = ProjectWorldAabbToScreen(
            renderBombPosition,
            bombState.boundsMins,
            bombState.boundsMaxs,
            viewMatrix,
            screenW,
            screenH,
            &boxMin,
            &boxMax);
    }

    if (hasScreenBox && !plantedBomb) {
        const float boxW = boxMax.x - boxMin.x;
        const float boxH = boxMax.y - boxMin.y;
        if (boxW < 14.0f || boxH < 10.0f)
            hasScreenBox = false;
    }

    if (!hasScreenBox && hasBombPos) {
        Vector3 bombPos = renderBombPosition;
        bombPos.z += plantedBomb ? 10.0f : 8.0f;
        const ScreenPos bombScreen = WorldToScreen(bombPos, viewMatrix, screenW, screenH);
        if (bombScreen.onScreen) {
            const float halfW = plantedBomb ? 14.0f : 15.0f;
            const float halfH = plantedBomb ? 12.0f : 11.0f;
            boxMin = ImVec2(bombScreen.x - halfW, bombScreen.y - halfH);
            boxMax = ImVec2(bombScreen.x + halfW, bombScreen.y + halfH);
            hasScreenBox = true;
        }
    }

    if (hasScreenBox) {
        drawList->AddRect(
            ImVec2(boxMin.x - 1.0f, boxMin.y - 1.0f),
            ImVec2(boxMax.x + 1.0f, boxMax.y + 1.0f),
            IM_COL32(0, 0, 0, 220),
            2.0f,
            3.0f,
            0);
        drawList->AddRect(boxMin, boxMax, bombCol, 2.0f, 1.8f, 0);

        const float centerX = (boxMin.x + boxMax.x) * 0.5f;

        if (g::espBombText && !plantedBomb) {
            const char* c4Icon = WeaponIconFromItemId(kWeaponC4Id);
            constexpr float c4IconSize = 17.0f;
            ImVec2 atlasSize = {};
            const bool atlasIconDrawn =
                esp::render::weapon_icons::CalculateDrawSize(
                    kWeaponC4Id,
                    c4IconSize,
                    &atlasSize) &&
                esp::render::weapon_icons::Draw(
                    drawList,
                    kWeaponC4Id,
                    ImVec2(
                        centerX - atlasSize.x * 0.5f,
                        boxMin.y - atlasSize.y - 5.0f),
                    c4IconSize,
                    bombCol);
            if (!atlasIconDrawn && c4Icon && g::fontWeaponIcons) {
                const ImVec2 c4SizePx = g::fontWeaponIcons->CalcTextSizeA(c4IconSize, FLT_MAX, 0.0f, c4Icon, nullptr);
                DrawTextShadow(
                    drawList,
                    g::fontWeaponIcons,
                    c4IconSize,
                    ImVec2(centerX - c4SizePx.x * 0.5f, boxMin.y - c4SizePx.y - 5.0f),
                    bombCol,
                    c4Icon);
            }
        }

        if (g::espBombText && plantedBomb) {
            const char* bombLabel = KEVQ_TR("Bomb");
            const ImVec2 bombLabelSize = bombFont->CalcTextSizeA(bombFsUse, FLT_MAX, 0.0f, bombLabel);
            const float labelY = boxMin.y - bombLabelSize.y - 4.0f;
            DrawTextShadow(
                drawList,
                nullptr,
                bombFontSz,
                ImVec2(centerX - bombLabelSize.x * 0.5f, labelY),
                bombCol,
                bombLabel);

            if (!g::espBombTime && plantedBomb && bombState.ticking && blowLeft > 0.0f) {
                std::snprintf(blowText, sizeof(blowText), "%.1fs", static_cast<double>(blowLeft));
                const ImVec2 blowSize = bombFont->CalcTextSizeA(bombFsUse, FLT_MAX, 0.0f, blowText);
                DrawTextShadow(
                    drawList,
                    nullptr,
                    bombFontSz,
                    ImVec2(centerX - blowSize.x * 0.5f, boxMax.y + 4.0f),
                    IM_COL32(255, 220, 140, 245),
                    blowText);
            }
        }

        if (plantedBomb && bombState.beingDefused && defuseLeft > 0.0f) {
            const float progress =
                esp::render::CalculateBombDefuseProgress(
                    defuseLeft,
                    defuseTotal);
            const float barW = std::max(54.0f, boxMax.x - boxMin.x);
            const float barH = 4.0f;
            const float barX = centerX - barW * 0.5f;
            const float barY = boxMax.y + 18.0f;
            drawList->AddRectFilled(
                ImVec2(barX - 1.0f, barY - 1.0f),
                ImVec2(barX + barW + 1.0f, barY + barH + 1.0f),
                IM_COL32(0, 0, 0, 220));
            drawList->AddRectFilled(
                ImVec2(barX, barY),
                ImVec2(barX + barW, barY + barH),
                IM_COL32(25, 25, 25, 200));
            drawList->AddRectFilled(
                ImVec2(barX, barY),
                ImVec2(barX + barW * progress, barY + barH),
                defuseColor);
        }
    }
}
