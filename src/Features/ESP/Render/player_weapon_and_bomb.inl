
if (g::espWeapon) {
    bottomTextY += 2.0f;
    
    
    
    const uint16_t weaponIconId = p.weaponIconId;
    const char* weaponIcon = WeaponIconFromItemId(weaponIconId);
    const bool hasWeaponVisualAsset = WeaponVisualKeyFromItemId(weaponIconId) != nullptr;
    const char* weaponIconFallback = WeaponIconFallbackTokenFromItemId(weaponIconId);
    const char* weaponName = WeaponNameFromItemId(p.weaponId);
    if (g::espWeaponIcon && weaponIconId != 0) {
        const ImU32 iconColor = ColorToImU32(g::espWeaponIconColor);
        if (!drawBottomWeaponIcon(weaponIconId, iconColor, g::espWeaponIconSize)) {
            ImFont* iconFont = PickWeaponIconFont(g::espWeaponIconSize);
            if (weaponIcon && iconFont) {
                drawBottomLabel(weaponIcon, iconColor, false, true, iconFont, g::espWeaponIconSize);
            } else if (hasWeaponVisualAsset && weaponIconFallback) {
                drawBottomLabel(weaponIconFallback, iconColor, false, true, g::fontEspName, g::espWeaponIconSize - 1.0f);
            }
        }
    }
    if (g::espWeaponText && weaponName)
        drawBottomLabel(weaponName, ColorToImU32(g::espWeaponTextColor), false, true, g::fontOverlayText, g::espWeaponTextSize);
}
// Bomb labels belong to Bomb ESP, not the Weapon Label master switch.
if (g::espBombInfo && g::espBombText && p.hasBomb && !bombState.dropped && !bombState.planted)
    drawBottomLabel(KEVQ_TR("Bomb"), bombCol, false, true, nullptr, g::espBombTextSize);
