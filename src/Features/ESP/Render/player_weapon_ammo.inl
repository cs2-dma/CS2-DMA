if (g::espWeapon && g::espWeaponAmmo && p.weaponIconId!=0 &&
    p.weaponPresentationUpdatedUs && nowUs>=p.weaponPresentationUpdatedUs &&
    nowUs-p.weaponPresentationUpdatedUs<=250000u && WeaponMaxClipFromItemId(p.weaponIconId)>0) {
    const std::string text=esp::render::FormatAmmo(p.displayAmmoClip,p.displayAmmoReserve,
        p.displayReserveUnitsKnown,p.displayReserveAsClips,p.weaponIconId);
    if (!text.empty()) {
        const bool low=p.displayAmmoCapacity>0 && p.displayAmmoClip<=std::max(1,p.displayAmmoCapacity/5);
        drawBottomLabel(text.c_str(),low ? IM_COL32(255,120,105,245) : ColorToImU32(g::espWeaponAmmoColor),
            false,true,g::fontOverlayText,g::espWeaponAmmoSize);
    }
}
