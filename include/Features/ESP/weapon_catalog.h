#pragma once

#include <cstdint>
#include <string_view>

struct ImFont;

namespace esp::weapons
{
    enum class DroppedWeaponCategory : uint8_t
    {
        Unknown = 0,
        Pistols,
        Rifles,
        Snipers,
        Smgs,
        Shotguns,
        MachineGuns,
        Equipment,
    };

    struct WeaponIconRegion
    {
        uint16_t x = 0;
        uint16_t y = 0;
        uint16_t width = 0;
        uint16_t height = 0;
    };

    bool IsKnifeItemId(uint16_t id) noexcept;
    const char* WeaponNameFromItemId(uint16_t id) noexcept;
    const char* WeaponVisualKeyFromItemId(uint16_t id) noexcept;
    const char* WeaponIconFromItemId(uint16_t id) noexcept;
    const char* WeaponIconFallbackTokenFromItemId(uint16_t id) noexcept;
    uint16_t WeaponItemIdFromDesignerName(std::string_view designerName) noexcept;
    DroppedWeaponCategory DroppedWeaponCategoryFromItemId(uint16_t id) noexcept;
    bool IsDroppedWeaponItemId(uint16_t id) noexcept;
    bool WeaponIconRegionFromItemId(
        uint16_t id,
        WeaponIconRegion* outRegion) noexcept;
    bool IsPrimaryWeaponItemId(uint16_t id) noexcept;
    int WeaponMaxClipFromItemId(uint16_t id) noexcept;
    ImFont* PickWeaponIconFont(float requestedSize) noexcept;
}
