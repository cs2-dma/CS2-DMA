#pragma once

#include "Game/Schema/structs.h"

namespace ui {
class IStatusSink;
}

namespace world::grenade_helper {

enum class GrenadeType : uint8_t { Smoke, Molotov, He, Flash };

constexpr bool MatchesHeldGrenade(GrenadeType type, uint16_t itemId) noexcept
{
    switch (type) {
    case GrenadeType::Smoke: return itemId == 45;
    case GrenadeType::Molotov: return itemId == 46 || itemId == 48;
    case GrenadeType::He: return itemId == 44;
    case GrenadeType::Flash: return itemId == 43;
    }
    return false;
}

constexpr uint16_t FreshHeldItem(uint16_t itemId, bool alive, uint32_t handle,
    uintptr_t entity, uint64_t updatedAtUs, uint64_t nowUs) noexcept
{
    return alive && handle != 0 && handle != 0xFFFFFFFFu && entity != 0 &&
        updatedAtUs != 0 && nowUs >= updatedAtUs && nowUs - updatedAtUs <= 100000u
        ? itemId : 0;
}

void DrawOverlay(
    const view_matrix_t& viewMatrix,
    const Vector3& localPosition,
    const Vector3& viewAngles,
    const char* mapKey,
    uint16_t heldItemId,
    float screenWidth,
    float screenHeight);

void RenderSettings(ui::IStatusSink& statusSink);
void RenderSpotList(ui::IStatusSink& statusSink);
void RenderPopups(ui::IStatusSink& statusSink);

}
