#include "Features/ESP/weapon_catalog.h"

#include "Features/ESP/Render/weapon_icon_atlas_data.generated.h"
#include "app/Core/app_state.h"
#include "app/Core/globals.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <string_view>

namespace
{
    enum class WeaponSlotKind : uint8_t
    {
        Unknown = 0,
        Knife,
        Sidearm,
        Primary,
        Utility,
        Objective,
        Gear,
    };

    struct WeaponLookupEntry
    {
        uint16_t id;
        const char* name;
        const char* visualKey;
        const char* iconGlyph;
        const char* iconFallback;
        WeaponSlotKind slotKind;
        int maxClip;
    };

    constexpr WeaponLookupEntry kKnifeLookupEntry = {
        42, "Knife", "knife", "]", "KN", WeaponSlotKind::Knife, 30
    };

    constexpr std::array kWeaponLookup = {
        WeaponLookupEntry{ 1,  "Deagle",     "deagle",          "A", "DE",  WeaponSlotKind::Sidearm,  7 },
        WeaponLookupEntry{ 2,  "Elite",      "elite",           "B", "EL",  WeaponSlotKind::Sidearm, 30 },
        WeaponLookupEntry{ 3,  "Five-SeveN", "fiveseven",       "C", "57",  WeaponSlotKind::Sidearm, 20 },
        WeaponLookupEntry{ 4,  "Glock",       "glock",           "D", "GL",  WeaponSlotKind::Sidearm, 20 },
        WeaponLookupEntry{ 7,  "AK-47",       "ak47",            "W", "AK",  WeaponSlotKind::Primary, 30 },
        WeaponLookupEntry{ 8,  "AUG",         "aug",             "U", "AUG", WeaponSlotKind::Primary, 30 },
        WeaponLookupEntry{ 9,  "AWP",         "awp",             "Z", "AWP", WeaponSlotKind::Primary, 10 },
        WeaponLookupEntry{ 10, "FAMAS",       "famas",           "R", "FAM", WeaponSlotKind::Primary, 25 },
        WeaponLookupEntry{ 11, "G3SG1",       "g3sg1",           "X", "G3",  WeaponSlotKind::Primary, 20 },
        WeaponLookupEntry{ 13, "Galil",       "galilar",         "Q", "GAL", WeaponSlotKind::Primary, 35 },
        WeaponLookupEntry{ 14, "M249",        "m249",            "g", "249", WeaponSlotKind::Primary, 100 },
        WeaponLookupEntry{ 16, "M4A4",        "m4a1",            "S", "M4",  WeaponSlotKind::Primary, 30 },
        WeaponLookupEntry{ 17, "MAC-10",      "mac10",           "K", "MAC", WeaponSlotKind::Primary, 30 },
        WeaponLookupEntry{ 19, "P90",         "p90",             "P", "P90", WeaponSlotKind::Primary, 50 },
        WeaponLookupEntry{ 23, "MP5-SD",      "mp5sd",           "x", "MP5", WeaponSlotKind::Primary, 30 },
        WeaponLookupEntry{ 24, "UMP-45",      "ump45",           "L", "UMP", WeaponSlotKind::Primary, 30 },
        WeaponLookupEntry{ 25, "XM1014",      "xm1014",          "b", "XM",  WeaponSlotKind::Primary,  7 },
        WeaponLookupEntry{ 26, "PP-Bizon",    "bizon",           "M", "BZ",  WeaponSlotKind::Primary, 64 },
        WeaponLookupEntry{ 27, "MAG-7",       "mag7",            "d", "M7",  WeaponSlotKind::Primary,  5 },
        WeaponLookupEntry{ 28, "Negev",       "negev",           "f", "NEG", WeaponSlotKind::Primary, 150 },
        WeaponLookupEntry{ 29, "Sawed-Off",   "sawedoff",        "c", "SO",  WeaponSlotKind::Primary,  7 },
        WeaponLookupEntry{ 30, "Tec-9",       "tec9",            "I", "T9",  WeaponSlotKind::Sidearm, 18 },
        WeaponLookupEntry{ 31, "Zeus",        "taser",           "m", "ZR",  WeaponSlotKind::Gear,     1 },
        WeaponLookupEntry{ 32, "P2000",       "p2000",           "E", "P2K", WeaponSlotKind::Sidearm, 13 },
        WeaponLookupEntry{ 33, "MP7",         "mp7",             "N", "MP7", WeaponSlotKind::Primary, 30 },
        WeaponLookupEntry{ 34, "MP9",         "mp9",             "O", "MP9", WeaponSlotKind::Primary, 30 },
        WeaponLookupEntry{ 35, "Nova",        "nova",            "e", "NV",  WeaponSlotKind::Primary,  8 },
        WeaponLookupEntry{ 36, "P250",        "p250",            "F", "P25", WeaponSlotKind::Sidearm, 13 },
        WeaponLookupEntry{ 38, "SCAR-20",     "scar20",          "Y", "SC",  WeaponSlotKind::Primary, 20 },
        WeaponLookupEntry{ 39, "SG553",       "sg556",           "V", "SG",  WeaponSlotKind::Primary, 30 },
        WeaponLookupEntry{ 40, "SSG08",       "ssg08",           "a", "SSG", WeaponSlotKind::Primary, 10 },
        WeaponLookupEntry{ 41, "Gold Knife",  "knifegg",          "]", "KN",  WeaponSlotKind::Knife,   30 },
        WeaponLookupEntry{ 43, "Flash",       "flashbang",       "i", "FB",  WeaponSlotKind::Utility, 30 },
        WeaponLookupEntry{ 44, "HE",          "hegrenade",       "j", "HE",  WeaponSlotKind::Utility, 30 },
        WeaponLookupEntry{ 45, "Smoke",       "smokegrenade",    "k", "SM",  WeaponSlotKind::Utility, 30 },
        WeaponLookupEntry{ 46, "Molotov",     "molotov",         "l", "ML",  WeaponSlotKind::Utility, 30 },
        WeaponLookupEntry{ 47, "Decoy",       "decoy",           "j", "DC",  WeaponSlotKind::Utility, 30 },
        WeaponLookupEntry{ 48, "Incendiary",  "incgrenade",      "n", "IN",  WeaponSlotKind::Utility, 30 },
        WeaponLookupEntry{ 49, "C4",          "c4",              "o", "C4",  WeaponSlotKind::Objective, 30 },
        WeaponLookupEntry{ 57, "Healthshot",  "health",          nullptr, "HP", WeaponSlotKind::Gear, 30 },
        WeaponLookupEntry{ 59, "Knife",       "knife_t",         "]", "KN",  WeaponSlotKind::Knife,   30 },
        WeaponLookupEntry{ 60, "M4A1-S",      "m4a1_silencer",   "T", "A1S", WeaponSlotKind::Primary, 25 },
        WeaponLookupEntry{ 61, "USP-S",       "usp_silencer",    "G", "USP", WeaponSlotKind::Sidearm, 12 },
        WeaponLookupEntry{ 63, "CZ75",        "cz75a",           "h", "CZ",  WeaponSlotKind::Sidearm, 12 },
        WeaponLookupEntry{ 64, "R8",          "revolver",        "J", "R8",  WeaponSlotKind::Sidearm,  8 },
        WeaponLookupEntry{ 500, "Bayonet",         "bayonet",                 "]", "KN", WeaponSlotKind::Knife, 30 },
        WeaponLookupEntry{ 503, "Classic Knife",   "knife_css",               "]", "KN", WeaponSlotKind::Knife, 30 },
        WeaponLookupEntry{ 505, "Flip Knife",      "knife_flip",              "]", "KN", WeaponSlotKind::Knife, 30 },
        WeaponLookupEntry{ 506, "Gut Knife",       "knife_gut",               "]", "KN", WeaponSlotKind::Knife, 30 },
        WeaponLookupEntry{ 507, "Karambit",        "knife_karambit",          "]", "KN", WeaponSlotKind::Knife, 30 },
        WeaponLookupEntry{ 508, "M9 Bayonet",      "knife_m9_bayonet",        "]", "KN", WeaponSlotKind::Knife, 30 },
        WeaponLookupEntry{ 509, "Huntsman Knife",  "knife_tactical",          "]", "KN", WeaponSlotKind::Knife, 30 },
        WeaponLookupEntry{ 512, "Falchion Knife",  "knife_falchion",          "]", "KN", WeaponSlotKind::Knife, 30 },
        WeaponLookupEntry{ 514, "Bowie Knife",     "knife_survival_bowie",    "]", "KN", WeaponSlotKind::Knife, 30 },
        WeaponLookupEntry{ 515, "Butterfly Knife", "knife_butterfly",         "]", "KN", WeaponSlotKind::Knife, 30 },
        WeaponLookupEntry{ 516, "Shadow Daggers",  "knife_push",              "]", "KN", WeaponSlotKind::Knife, 30 },
        WeaponLookupEntry{ 517, "Paracord Knife",  "knife_cord",              "]", "KN", WeaponSlotKind::Knife, 30 },
        WeaponLookupEntry{ 518, "Survival Knife",  "knife_canis",             "]", "KN", WeaponSlotKind::Knife, 30 },
        WeaponLookupEntry{ 519, "Ursus Knife",     "knife_ursus",             "]", "KN", WeaponSlotKind::Knife, 30 },
        WeaponLookupEntry{ 520, "Navaja Knife",    "knife_gypsy_jackknife",   "]", "KN", WeaponSlotKind::Knife, 30 },
        WeaponLookupEntry{ 521, "Nomad Knife",     "knife_outdoor",           "]", "KN", WeaponSlotKind::Knife, 30 },
        WeaponLookupEntry{ 522, "Stiletto Knife",  "knife_stiletto",          "]", "KN", WeaponSlotKind::Knife, 30 },
        WeaponLookupEntry{ 523, "Talon Knife",     "knife_widowmaker",        "]", "KN", WeaponSlotKind::Knife, 30 },
        WeaponLookupEntry{ 525, "Skeleton Knife",  "knife_skeleton",          "]", "KN", WeaponSlotKind::Knife, 30 },
        WeaponLookupEntry{ 526, "Kukri Knife",     "knife_kukri",             "]", "KN", WeaponSlotKind::Knife, 30 },
    };

    const WeaponLookupEntry* FindWeaponLookupEntry(uint16_t id) noexcept
    {
        for (const WeaponLookupEntry& entry : kWeaponLookup) {
            if (entry.id == id)
                return &entry;
        }
        return app::state::IsKnifeItemId(id) ? &kKnifeLookupEntry : nullptr;
    }

    bool AsciiEqualsIgnoreCase(std::string_view left, std::string_view right) noexcept
    {
        if (left.size() != right.size())
            return false;
        for (size_t i = 0; i < left.size(); ++i) {
            const char lhs = left[i] >= 'A' && left[i] <= 'Z'
                ? static_cast<char>(left[i] + ('a' - 'A'))
                : left[i];
            if (lhs != right[i])
                return false;
        }
        return true;
    }

    const resources::weapon_icons::WeaponIconAtlasRegion*
    FindWeaponIconAtlasRegion(uint16_t id) noexcept
    {
        const auto& regions = resources::weapon_icons::kRegions;
        auto it = std::lower_bound(
            regions.begin(),
            regions.end(),
            id,
            [](const auto& region, uint16_t itemId) {
                return region.itemId < itemId;
            });
        if (it != regions.end() && it->itemId == id)
            return &*it;

        if (app::state::IsKnifeItemId(id)) {
            it = std::lower_bound(
                regions.begin(),
                regions.end(),
                static_cast<uint16_t>(42),
                [](const auto& region, uint16_t itemId) {
                    return region.itemId < itemId;
                });
            if (it != regions.end() && it->itemId == 42)
                return &*it;
        }
        return nullptr;
    }
}

bool esp::weapons::IsKnifeItemId(uint16_t id) noexcept
{
    return app::state::IsKnifeItemId(id);
}

const char* esp::weapons::WeaponNameFromItemId(uint16_t id) noexcept
{
    const WeaponLookupEntry* entry = FindWeaponLookupEntry(id);
    return entry ? entry->name : nullptr;
}

const char* esp::weapons::WeaponVisualKeyFromItemId(uint16_t id) noexcept
{
    const WeaponLookupEntry* entry = FindWeaponLookupEntry(id);
    return entry ? entry->visualKey : nullptr;
}

const char* esp::weapons::WeaponIconFromItemId(uint16_t id) noexcept
{
    const WeaponLookupEntry* entry = FindWeaponLookupEntry(id);
    return entry ? entry->iconGlyph : nullptr;
}

const char* esp::weapons::WeaponIconFallbackTokenFromItemId(uint16_t id) noexcept
{
    const WeaponLookupEntry* entry = FindWeaponLookupEntry(id);
    return entry ? entry->iconFallback : nullptr;
}

uint16_t esp::weapons::WeaponItemIdFromDesignerName(
    std::string_view designerName) noexcept
{
    constexpr std::string_view prefix = "weapon_";
    size_t prefixOffset = std::string_view::npos;
    for (size_t i = 0; i + prefix.size() <= designerName.size(); ++i) {
        if (AsciiEqualsIgnoreCase(designerName.substr(i, prefix.size()), prefix)) {
            prefixOffset = i + prefix.size();
            break;
        }
    }
    if (prefixOffset == std::string_view::npos)
        return 0;

    const std::string_view visualKey = designerName.substr(prefixOffset);
    for (const WeaponLookupEntry& entry : kWeaponLookup) {
        if (entry.visualKey && AsciiEqualsIgnoreCase(visualKey, entry.visualKey))
            return entry.id;
    }
    return 0;
}

esp::weapons::DroppedWeaponCategory
esp::weapons::DroppedWeaponCategoryFromItemId(uint16_t id) noexcept
{
    switch (id) {
    case 1: case 2: case 3: case 4: case 30:
    case 32: case 36: case 61: case 63: case 64:
        return DroppedWeaponCategory::Pistols;
    case 7: case 8: case 10: case 13: case 16: case 39: case 60:
        return DroppedWeaponCategory::Rifles;
    case 9: case 11: case 38: case 40:
        return DroppedWeaponCategory::Snipers;
    case 17: case 19: case 23: case 24: case 26: case 33: case 34:
        return DroppedWeaponCategory::Smgs;
    case 25: case 27: case 29: case 35:
        return DroppedWeaponCategory::Shotguns;
    case 14: case 28:
        return DroppedWeaponCategory::MachineGuns;
    case 31: case 57:
        return DroppedWeaponCategory::Equipment;
    default:
        return DroppedWeaponCategory::Unknown;
    }
}

bool esp::weapons::IsDroppedWeaponItemId(uint16_t id) noexcept
{
    return DroppedWeaponCategoryFromItemId(id) != DroppedWeaponCategory::Unknown;
}

bool esp::weapons::WeaponIconRegionFromItemId(
    uint16_t id,
    WeaponIconRegion* outRegion) noexcept
{
    if (!outRegion)
        return false;
    *outRegion = {};

    const auto* region = FindWeaponIconAtlasRegion(id);
    if (!region)
        return false;

    outRegion->x = region->x;
    outRegion->y = region->y;
    outRegion->width = region->width;
    outRegion->height = region->height;
    return true;
}

bool esp::weapons::IsPrimaryWeaponItemId(uint16_t id) noexcept
{
    const WeaponLookupEntry* entry = FindWeaponLookupEntry(id);
    return entry && entry->slotKind == WeaponSlotKind::Primary;
}

int esp::weapons::WeaponMaxClipFromItemId(uint16_t id) noexcept
{
    const WeaponLookupEntry* entry = FindWeaponLookupEntry(id);
    return entry ? entry->maxClip : 30;
}

ImFont* esp::weapons::PickWeaponIconFont(float requestedSize) noexcept
{
    if (requestedSize <= 14.5f && g::fontWeaponIconsSmall)
        return g::fontWeaponIconsSmall;
    if (requestedSize >= 20.5f && g::fontWeaponIconsLarge)
        return g::fontWeaponIconsLarge;
    return g::fontWeaponIcons;
}
