#pragma once

#include <cstdint>

#include <imgui.h>

struct ID3D11Device;

namespace esp::render::weapon_icons
{
    bool Initialize(ID3D11Device* device) noexcept;
    void Shutdown() noexcept;
    bool IsReady() noexcept;

    bool CalculateDrawSize(
        uint16_t itemId,
        float height,
        ImVec2* outSize) noexcept;

    bool Draw(
        ImDrawList* drawList,
        uint16_t itemId,
        const ImVec2& topLeft,
        float height,
        ImU32 color,
        bool shadow = true) noexcept;
}
