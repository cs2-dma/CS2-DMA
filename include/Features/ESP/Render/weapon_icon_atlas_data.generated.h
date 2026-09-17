#pragma once

#include <array>
#include <cstdint>

namespace resources::weapon_icons
{
    struct WeaponIconAtlasRegion
    {
        uint16_t itemId;
        uint16_t x;
        uint16_t y;
        uint16_t width;
        uint16_t height;
    };

    inline constexpr uint16_t kAtlasWidth = 1440;
    inline constexpr uint16_t kAtlasHeight = 486;
    inline constexpr std::array kRegions = {
        WeaponIconAtlasRegion{ 1, 50, 2, 79, 50 },
        WeaponIconAtlasRegion{ 2, 216, 2, 107, 50 },
        WeaponIconAtlasRegion{ 3, 419, 2, 61, 50 },
        WeaponIconAtlasRegion{ 4, 595, 2, 70, 50 },
        WeaponIconAtlasRegion{ 7, 741, 2, 138, 50 },
        WeaponIconAtlasRegion{ 8, 930, 2, 119, 50 },
        WeaponIconAtlasRegion{ 9, 1084, 2, 171, 50 },
        WeaponIconAtlasRegion{ 10, 1289, 2, 122, 50 },
        WeaponIconAtlasRegion{ 11, 17, 56, 146, 50 },
        WeaponIconAtlasRegion{ 13, 199, 56, 141, 50 },
        WeaponIconAtlasRegion{ 14, 374, 56, 151, 50 },
        WeaponIconAtlasRegion{ 16, 568, 56, 123, 50 },
        WeaponIconAtlasRegion{ 17, 775, 56, 70, 50 },
        WeaponIconAtlasRegion{ 19, 936, 56, 107, 50 },
        WeaponIconAtlasRegion{ 23, 1111, 56, 118, 50 },
        WeaponIconAtlasRegion{ 24, 1284, 56, 131, 50 },
        WeaponIconAtlasRegion{ 25, 13, 110, 153, 50 },
        WeaponIconAtlasRegion{ 26, 199, 110, 141, 50 },
        WeaponIconAtlasRegion{ 27, 397, 110, 106, 50 },
        WeaponIconAtlasRegion{ 28, 566, 110, 127, 50 },
        WeaponIconAtlasRegion{ 29, 744, 110, 132, 50 },
        WeaponIconAtlasRegion{ 30, 949, 110, 82, 50 },
        WeaponIconAtlasRegion{ 31, 1137, 110, 65, 50 },
        WeaponIconAtlasRegion{ 32, 1320, 110, 59, 50 },
        WeaponIconAtlasRegion{ 33, 51, 164, 77, 50 },
        WeaponIconAtlasRegion{ 34, 212, 164, 115, 50 },
        WeaponIconAtlasRegion{ 35, 371, 164, 157, 50 },
        WeaponIconAtlasRegion{ 36, 600, 164, 59, 50 },
        WeaponIconAtlasRegion{ 38, 733, 164, 153, 50 },
        WeaponIconAtlasRegion{ 39, 920, 164, 140, 50 },
        WeaponIconAtlasRegion{ 40, 1092, 164, 156, 50 },
        WeaponIconAtlasRegion{ 41, 1275, 164, 149, 50 },
        WeaponIconAtlasRegion{ 42, 30, 218, 120, 50 },
        WeaponIconAtlasRegion{ 43, 246, 218, 47, 50 },
        WeaponIconAtlasRegion{ 44, 431, 218, 38, 50 },
        WeaponIconAtlasRegion{ 45, 618, 218, 23, 50 },
        WeaponIconAtlasRegion{ 46, 793, 218, 34, 50 },
        WeaponIconAtlasRegion{ 47, 966, 218, 47, 50 },
        WeaponIconAtlasRegion{ 48, 1158, 218, 23, 50 },
        WeaponIconAtlasRegion{ 49, 1325, 218, 50, 50 },
        WeaponIconAtlasRegion{ 57, 55, 272, 69, 50 },
        WeaponIconAtlasRegion{ 59, 208, 272, 124, 50 },
        WeaponIconAtlasRegion{ 60, 374, 272, 151, 50 },
        WeaponIconAtlasRegion{ 61, 576, 272, 108, 50 },
        WeaponIconAtlasRegion{ 63, 772, 272, 75, 50 },
        WeaponIconAtlasRegion{ 64, 949, 272, 82, 50 },
        WeaponIconAtlasRegion{ 500, 1098, 272, 144, 50 },
        WeaponIconAtlasRegion{ 503, 1278, 272, 144, 50 },
        WeaponIconAtlasRegion{ 505, 28, 326, 124, 50 },
        WeaponIconAtlasRegion{ 506, 213, 326, 113, 50 },
        WeaponIconAtlasRegion{ 507, 398, 326, 103, 50 },
        WeaponIconAtlasRegion{ 508, 562, 326, 136, 50 },
        WeaponIconAtlasRegion{ 509, 735, 326, 150, 50 },
        WeaponIconAtlasRegion{ 512, 920, 326, 139, 50 },
        WeaponIconAtlasRegion{ 514, 1122, 326, 96, 50 },
        WeaponIconAtlasRegion{ 515, 1287, 326, 125, 50 },
        WeaponIconAtlasRegion{ 516, 25, 380, 129, 50 },
        WeaponIconAtlasRegion{ 517, 201, 380, 138, 50 },
        WeaponIconAtlasRegion{ 518, 377, 380, 145, 50 },
        WeaponIconAtlasRegion{ 519, 558, 380, 144, 50 },
        WeaponIconAtlasRegion{ 520, 737, 380, 145, 50 },
        WeaponIconAtlasRegion{ 521, 917, 380, 145, 50 },
        WeaponIconAtlasRegion{ 522, 1097, 380, 146, 50 },
        WeaponIconAtlasRegion{ 523, 1279, 380, 141, 50 },
        WeaponIconAtlasRegion{ 525, 17, 434, 145, 50 },
        WeaponIconAtlasRegion{ 526, 197, 434, 145, 50 },
    };
}
