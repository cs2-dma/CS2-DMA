#pragma once

#include "Game/Schema/structs.h"

namespace ui {
class IStatusSink;
}

namespace world::grenade_helper {

void DrawOverlay(
    const view_matrix_t& viewMatrix,
    const Vector3& localPosition,
    const Vector3& viewAngles,
    const char* mapKey,
    float screenWidth,
    float screenHeight);

void RenderSettings(ui::IStatusSink& statusSink);
void RenderSpotList(ui::IStatusSink& statusSink);
void RenderPopups(ui::IStatusSink& statusSink);

}
