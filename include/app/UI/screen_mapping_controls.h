#pragma once

#include "app/Core/globals.h"
#include "app/UI/MenuShell/ui_widgets.h"

namespace ui {
inline void RenderScreenMappingControls()
{
    auto& mapping = g::displaySettings.mapping;
    ui::widgets::ToggleRow("game_auto_aspect", "Auto aspect (camera)", &mapping.autoAspect);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", KEVQ_TR("Detects the camera aspect, not the Fuser scaling. Uses the configured resolution when unavailable."));
    ImGui::TextDisabled("%s", KEVQ_TR("Game resolution (default 1920 x 1080)"));
    int resolution[2] = {mapping.gameWidth, mapping.gameHeight};
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    if (ImGui::InputInt2("##game_resolution", resolution)) {
        mapping.gameWidth = resolution[0];
        mapping.gameHeight = resolution[1];
    }
    if (ImGui::IsItemDeactivatedAfterEdit())
        mapping = app::display::Sanitize(mapping);

    constexpr const char* scalingModes[] = {"Stretch", "Keep aspect (black bars)", "Custom image area"};
    ImGui::TextDisabled("%s", KEVQ_TR("Game image scaling"));
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    if (ImGui::BeginCombo("##game_scaling", KEVQ_TR(scalingModes[std::clamp(mapping.scaling, 0, 2)]))) {
        for (int i = 0; i < 3; ++i)
            if (ImGui::Selectable(KEVQ_TR(scalingModes[i]), mapping.scaling == i)) mapping.scaling = i;
        ImGui::EndCombo();
    }
    if (mapping.scaling == 2) {
        const auto percent = [](const char* id, const char* label, float& value, float low, float high) {
            float amount = value * 100.0f;
            if (ui::widgets::SliderFloatRow(id, label, &amount, low, high, "%.1f%%"))
                value = amount * 0.01f;
        };
        percent("image_x", "Image left", mapping.x, 0.0f, 95.0f);
        percent("image_y", "Image top", mapping.y, 0.0f, 95.0f);
        percent("image_width", "Image width", mapping.width, 5.0f, 100.0f);
        percent("image_height", "Image height", mapping.height, 5.0f, 100.0f);
        mapping = app::display::Sanitize(mapping);
    }
    ImGui::TextDisabled("%s: %.0f x %.0f @ %.0f, %.0f", KEVQ_TR("Game image area"),
        g::gameViewport.width, g::gameViewport.height, g::gameViewport.x, g::gameViewport.y);
    if (mapping.autoAspect)
        ImGui::TextDisabled("%s: %.3f (%s)", KEVQ_TR("Aspect ratio"), g::gameViewport.sourceAspect,
            KEVQ_TR(g::gameViewport.detectedAspect ? "Camera" : "Configured fallback"));
    if (ui::widgets::FullButton("Reset Screen Mapping", 30.0f)) mapping = {};
}
}
