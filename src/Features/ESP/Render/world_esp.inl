if (g::espWorld || (g::espEnabled && g::espItem)) {
    for (int i = 0; i < worldMarkerCount; ++i) {
        const WorldMarker& marker = worldMarkers[i];
        if (!marker.valid)
            continue;
        if (marker.expiresUs > 0 && marker.expiresUs <= nowUs)
            continue;
        if (!IsFiniteVec(marker.position))
            continue;
        
        if (std::fabs(marker.position.x) < 1.0f && std::fabs(marker.position.y) < 1.0f && std::fabs(marker.position.z) < 1.0f)
            continue;

        const bool isDroppedWeapon = (marker.type == WorldMarkerType::DroppedWeapon);
        const bool isUtilityEffect =
            marker.type == WorldMarkerType::Smoke ||
            marker.type == WorldMarkerType::Inferno ||
            marker.type == WorldMarkerType::Decoy ||
            marker.type == WorldMarkerType::Explosive;
        const bool isUtilityProjectile =
            marker.type == WorldMarkerType::SmokeProjectile ||
            marker.type == WorldMarkerType::MolotovProjectile ||
            marker.type == WorldMarkerType::DecoyProjectile;

        if (isDroppedWeapon) {
            if (marker.weaponId == 0 || marker.weaponId >= 1200)
                continue;
            if (marker.weaponId == 49) {
                bool bombCarriedByAnyPlayer = localHasBomb;
                if (!bombCarriedByAnyPlayer) {
                    for (int pi = 0; pi < 64; ++pi) {
                        if (players[pi].valid && players[pi].hasBomb) {
                            bombCarriedByAnyPlayer = true;
                            break;
                        }
                    }
                }
                if (bombCarriedByAnyPlayer)
                    continue;
                const bool bombEspAlreadyVisible =
                    g::espBombInfo &&
                    (bombState.planted || bombState.dropped) &&
                    IsFiniteVec(bombState.position);
                if (bombEspAlreadyVisible)
                    continue;
                if (!g::espBombInfo)
                    continue;
            } else {
                if (!g::espItem)
                    continue;
            }
            if (IsKnifeItemId(marker.weaponId))
                continue;
            if (marker.weaponId != 49 && !g::espItemEnabledMask.test(marker.weaponId))
                continue;
        } else if (isUtilityEffect) {
            if (!g::espWorld)
                continue;
        } else if (isUtilityProjectile) {
            if (!g::espWorld || !g::espWorldProjectiles)
                continue;
        } else {
            continue;
        }

        Vector3 drawPos = marker.position;
        drawPos.z += 8.0f;
        const ScreenPos markerScreen = WorldToScreen(drawPos, viewMatrix, screenW, screenH);
        if (!markerScreen.onScreen)
            continue;

        const char* rawMarkerName =
            WorldMarkerName(marker.type, marker.weaponId);
        if (!rawMarkerName || rawMarkerName[0] == '\0')
            continue;
        const char* markerName =
            app::localization::Get(rawMarkerName);

        ImU32 markerColor = isDroppedWeapon
            ? ColorToImU32(g::espItemColor)
            : worldCol;
        
        if (marker.type == WorldMarkerType::Smoke)
            markerColor = IM_COL32(180, 180, 220, 255);
        else if (marker.type == WorldMarkerType::Inferno)
            markerColor = IM_COL32(255, 120, 40, 255);
        else if (marker.type == WorldMarkerType::Decoy)
            markerColor = IM_COL32(200, 200, 80, 255);
        else if (marker.type == WorldMarkerType::Explosive)
            markerColor = IM_COL32(255, 80, 80, 255);
        else if (marker.type == WorldMarkerType::SmokeProjectile)
            markerColor = IM_COL32(180, 180, 220, 255);
        else if (marker.type == WorldMarkerType::MolotovProjectile)
            markerColor = IM_COL32(255, 140, 60, 255);
        else if (marker.type == WorldMarkerType::DecoyProjectile)
            markerColor = IM_COL32(210, 210, 110, 255);

        const float lifeLeft = (marker.expiresUs > nowUs)
            ? static_cast<float>(marker.expiresUs - nowUs) / 1000000.0f
            : 0.0f;
        const bool showTimer =
            (marker.type == WorldMarkerType::Smoke && g::espWorldSmokeTimer) ||
            (marker.type == WorldMarkerType::Inferno && g::espWorldInfernoTimer) ||
            (marker.type == WorldMarkerType::Decoy && g::espWorldDecoyTimer);
        char markerText[96] = {};
        if (showTimer && lifeLeft > 0.0f)
            std::snprintf(markerText, sizeof(markerText), "%s %.1fs", markerName, static_cast<double>(lifeLeft));
        else
            std::snprintf(markerText, sizeof(markerText), "%s", markerName);

        ImVec2 itemBoxMin = {};
        ImVec2 itemBoxMax = {};
        if (isDroppedWeapon && g::espItemHighlight) {
            itemBoxMin = ImVec2(markerScreen.x - 15.0f, markerScreen.y - 9.0f);
            itemBoxMax = ImVec2(markerScreen.x + 15.0f, markerScreen.y + 9.0f);
            drawList->AddRect(
                ImVec2(itemBoxMin.x - 1.0f, itemBoxMin.y - 1.0f),
                ImVec2(itemBoxMax.x + 1.0f, itemBoxMax.y + 1.0f),
                IM_COL32(0, 0, 0, 220),
                2.0f,
                3.0f,
                0);
            drawList->AddRect(itemBoxMin, itemBoxMax, markerColor, 2.0f, 1.6f, 0);
        }

        const char* icon = nullptr;
        ImFont* iconFont = PickWeaponIconFont(16.0f);
        const char* iconFallbackToken = nullptr;
        bool atlasIconDrawn = false;
        if ((isDroppedWeapon && g::espItemIcon) || isUtilityProjectile) {
            const char* iconCandidate = WeaponIconFromItemId(marker.weaponId);
            if (WeaponNameFromItemId(marker.weaponId)) {
                icon = iconCandidate;
                if (!icon && WeaponVisualKeyFromItemId(marker.weaponId))
                    iconFallbackToken = WeaponIconFallbackTokenFromItemId(marker.weaponId);

                ImVec2 atlasSize = {};
                constexpr float atlasHeight = 16.0f;
                if (esp::render::weapon_icons::CalculateDrawSize(
                        marker.weaponId,
                        atlasHeight,
                        &atlasSize)) {
                    atlasIconDrawn = esp::render::weapon_icons::Draw(
                        drawList,
                        marker.weaponId,
                        ImVec2(
                            markerScreen.x - atlasSize.x * 0.5f,
                            markerScreen.y - atlasSize.y - 8.0f),
                        atlasHeight,
                        isDroppedWeapon
                            ? markerColor
                            : IM_COL32(240, 240, 240, 240));
                }
            }
        }
        if (!atlasIconDrawn && ((icon && iconFont) || iconFallbackToken)) {
            const char* iconText = icon ? icon : iconFallbackToken;
            if (!icon)
                iconFont = g::fontEspName ? g::fontEspName : ImGui::GetFont();
            const float iconSize = icon ? 16.0f : 13.5f;
            const ImVec2 iconBounds = iconFont->CalcTextSizeA(iconSize, FLT_MAX, 0.0f, iconText, nullptr);
            const float iconX = markerScreen.x - iconBounds.x * 0.5f;
            const float iconY = markerScreen.y - iconBounds.y - 8.0f;
            DrawTextShadow(
                drawList,
                iconFont,
                iconSize,
                ImVec2(iconX, iconY),
                isDroppedWeapon ? markerColor : IM_COL32(240, 240, 240, 240),
                iconText);
        }

        if (!isDroppedWeapon || g::espItemText) {
            const ImVec2 textSize = ImGui::CalcTextSize(markerText);
            const float textY = isDroppedWeapon && g::espItemHighlight
                ? itemBoxMax.y + 3.0f
                : markerScreen.y - textSize.y * 0.5f;
            DrawTextShadow(
                drawList,
                nullptr,
                0.0f,
                ImVec2(markerScreen.x - textSize.x * 0.5f, textY),
                markerColor,
                markerText);
        }
    }
}
