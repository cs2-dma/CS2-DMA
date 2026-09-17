#include "Features/World/UI/world_tab.h"

#include "Features/World/grenade_helper.h"
#include "Features/ESP/weapon_catalog.h"
#include "app/Core/globals.h"
#include "app/Localization/localization.h"
#include "app/UI/MenuShell/ui_icons.h"
#include "app/UI/MenuShell/ui_widgets.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cfloat>
#include <cstdio>
#include <cstdint>

namespace
{
    enum class WorldSettingsPanel : uint8_t
    {
        None,
        WorldEsp,
        DroppedItems,
        GrenadeHelper,
    };

    void PushSettingsWindowStyle()
    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f, 12.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 7.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 8.0f));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.035f, 0.055f, 0.085f, 0.98f));
        ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.035f, 0.055f, 0.085f, 0.98f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.16f, 0.24f, 0.36f, 0.82f));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.07f, 0.10f, 0.15f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.10f, 0.14f, 0.21f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(0.20f, 0.48f, 1.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_CheckMark, ImVec4(0.20f, 0.48f, 1.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_TitleBg, ImVec4(0.035f, 0.055f, 0.085f, 0.98f));
        ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.055f, 0.085f, 0.125f, 0.98f));
        ImGui::PushStyleColor(ImGuiCol_TitleBgCollapsed, ImVec4(0.035f, 0.055f, 0.085f, 0.98f));
    }

    void PopSettingsWindowStyle()
    {
        ImGui::PopStyleColor(10);
        ImGui::PopStyleVar(4);
    }

    void DrawSettingsWindowHeader()
    {
        const ImVec2 linePos = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddRectFilled(
            linePos,
            ImVec2(linePos.x + ImGui::GetContentRegionAvail().x, linePos.y + 1.0f),
            ui::widgets::ColorU32(52, 105, 186, 138),
            1.0f);
        ImGui::Spacing();
        ImGui::Spacing();
    }

    bool DrawFeatureRow(
        const char* id,
        const char* label,
        ui::icons::Icon icon,
        bool* enabled,
        bool settingsSelected,
        bool showSettings = true)
    {
        const float width = std::max(260.0f, ImGui::GetContentRegionAvail().x - 2.0f);
        const auto row = ui::widgets::BeginControlRow(
            id,
            "",
            width,
            50.0f,
            enabled && *enabled,
            enabled && *enabled ? 255 : 210);
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImVec2 iconMin(row.min.x + 14.0f, row.min.y + 15.0f);
        (void)ui::icons::DrawCentered(
            drawList,
            g::fontUiIcons,
            icon,
            iconMin,
            20.0f,
            18.0f,
            ui::widgets::ColorU32(174, 196, 225, 238));
        drawList->AddText(
            ImVec2(row.min.x + 48.0f, row.min.y + (row.height - ImGui::GetFontSize()) * 0.5f),
            ui::widgets::ColorU32(226, 234, 246, enabled && *enabled ? 255 : 210),
            app::localization::Get(label));

        ImGui::SetCursorScreenPos(ImVec2(row.max.x - (showSettings ? 88.0f : 50.0f), row.min.y + 15.0f));
        ui::widgets::ToggleSwitch("toggle", enabled);

        bool settingsClicked = false;
        if (showSettings) {
            ImGui::SetCursorScreenPos(ImVec2(row.max.x - 38.0f, row.min.y + 12.0f));
            ImGui::InvisibleButton("##settings", ImVec2(26.0f, 26.0f));
            settingsClicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
            const bool settingsHovered = ImGui::IsItemHovered();
            if (settingsHovered)
                ImGui::SetItemTooltip("%s", KEVQ_TR("Feature settings"));
            const ImVec2 buttonMin = ImGui::GetItemRectMin();
            const ImVec2 buttonMax = ImGui::GetItemRectMax();
            if (settingsSelected || settingsHovered) {
                drawList->AddRectFilled(
                    buttonMin,
                    buttonMax,
                    settingsSelected
                        ? ui::widgets::ColorU32(17, 45, 83, 235)
                        : ui::widgets::ColorU32(15, 27, 42, 225),
                    7.0f);
                drawList->AddRect(
                    buttonMin,
                    buttonMax,
                    settingsSelected
                        ? ui::widgets::ColorU32(65, 132, 238, 205)
                        : ui::widgets::ColorU32(72, 91, 116, 145),
                    7.0f,
                    0,
                    0.8f);
            }
            (void)ui::icons::DrawCentered(
                drawList,
                g::fontUiIcons,
                ui::icons::Icon::Sliders,
                buttonMin,
                26.0f,
                16.0f,
                ui::widgets::ColorU32(188, 208, 235, settingsSelected ? 255 : 205));
        }
        ui::widgets::EndControlRow(row);
        return settingsClicked;
    }

    using DroppedCategory = esp::weapons::DroppedWeaponCategory;

    struct DroppedCategoryRow
    {
        DroppedCategory category;
        const char* label;
    };

    constexpr std::array kDroppedCategories = {
        DroppedCategoryRow{ DroppedCategory::Pistols, "Pistols" },
        DroppedCategoryRow{ DroppedCategory::Rifles, "Rifles" },
        DroppedCategoryRow{ DroppedCategory::Snipers, "Sniper Rifles" },
        DroppedCategoryRow{ DroppedCategory::Smgs, "SMGs" },
        DroppedCategoryRow{ DroppedCategory::Shotguns, "Shotguns" },
        DroppedCategoryRow{ DroppedCategory::MachineGuns, "Machine Guns" },
        DroppedCategoryRow{ DroppedCategory::Equipment, "Equipment" },
    };

    void SetDroppedCategoryEnabled(DroppedCategory category, bool enabled)
    {
        for (uint16_t itemId = 1; itemId < g::espItemEnabledMask.size(); ++itemId) {
            if (esp::weapons::DroppedWeaponCategoryFromItemId(itemId) == category)
                g::espItemEnabledMask.set(itemId, enabled);
        }
    }

    void SetAllDroppedItemsEnabled(bool enabled)
    {
        for (uint16_t itemId = 1; itemId < g::espItemEnabledMask.size(); ++itemId) {
            if (esp::weapons::IsDroppedWeaponItemId(itemId))
                g::espItemEnabledMask.set(itemId, enabled);
        }
    }

    void DrawDroppedItemsSettings()
    {
        ImGui::BeginDisabled(!g::espItem);
        ui::widgets::ToggleRow("item_name", "Weapon Name", &g::espItemText);
        ui::widgets::ToggleRow("item_icon", "Weapon Icon", &g::espItemIcon);
        ui::widgets::ToggleRow("item_highlight", "Weapon Highlight", &g::espItemHighlight);
        ui::widgets::ColorRow(
            "item_color",
            "Dropped Item Color",
            g::espItemColor,
            ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoInputs);

        ImGui::SeparatorText(KEVQ_TR("Categories"));
        if (ImGui::Button(KEVQ_TR("Select All"), ImVec2(112.0f, 30.0f)))
            SetAllDroppedItemsEnabled(true);
        ImGui::SameLine();
        if (ImGui::Button(KEVQ_TR("Clear All"), ImVec2(112.0f, 30.0f)))
            SetAllDroppedItemsEnabled(false);

        if (ImGui::BeginTable(
                "##dropped_categories",
                2,
                ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoPadOuterX)) {
            for (size_t categoryIndex = 0;
                 categoryIndex < kDroppedCategories.size();
                 ++categoryIndex) {
                const DroppedCategoryRow& row = kDroppedCategories[categoryIndex];
                bool hasItems = false;
                bool anyEnabled = false;
                bool allEnabled = true;
                for (uint16_t itemId = 1;
                     itemId < g::espItemEnabledMask.size();
                     ++itemId) {
                    if (esp::weapons::DroppedWeaponCategoryFromItemId(itemId) != row.category)
                        continue;
                    hasItems = true;
                    const bool enabled = g::espItemEnabledMask.test(itemId);
                    anyEnabled = anyEnabled || enabled;
                    allEnabled = allEnabled && enabled;
                }
                if (!hasItems)
                    continue;

                if ((categoryIndex % 2u) == 0u)
                    ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(static_cast<int>(categoryIndex % 2u));
                bool categoryEnabled = allEnabled;
                ImGui::PushID(static_cast<int>(categoryIndex));
                if (ImGui::Checkbox(KEVQ_TR(row.label), &categoryEnabled))
                    SetDroppedCategoryEnabled(row.category, categoryEnabled);
                ImGui::PopID();
                if (anyEnabled && !allEnabled) {
                    ImGui::SameLine();
                    ImGui::TextDisabled("%s", KEVQ_TR("Partial"));
                }
            }
            ImGui::EndTable();
        }

        ImGui::SeparatorText(KEVQ_TR("Individual Weapons"));
        if (ImGui::BeginChild(
                "##dropped_weapon_list",
                ImVec2(0.0f, 220.0f),
                ImGuiChildFlags_Borders)) {
            if (ImGui::BeginTable(
                    "##dropped_weapon_grid",
                    2,
                    ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoPadOuterX)) {
                int visibleIndex = 0;
                for (uint16_t itemId = 1;
                     itemId < g::espItemEnabledMask.size();
                     ++itemId) {
                    if (!esp::weapons::IsDroppedWeaponItemId(itemId))
                        continue;
                    const char* weaponName = esp::weapons::WeaponNameFromItemId(itemId);
                    if (!weaponName)
                        continue;
                    if ((visibleIndex % 2) == 0)
                        ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(visibleIndex % 2);
                    bool enabled = g::espItemEnabledMask.test(itemId);
                    ImGui::PushID(static_cast<int>(itemId));
                    if (ImGui::Checkbox(weaponName, &enabled))
                        g::espItemEnabledMask.set(itemId, enabled);
                    ImGui::PopID();
                    ++visibleIndex;
                }
                ImGui::EndTable();
            }
        }
        ImGui::EndChild();
        ImGui::EndDisabled();
    }
}

const char* ui::tabs::WorldTab::Label() const
{
    return "World";
}

void ui::tabs::WorldTab::Render(MenuState& state, IStatusSink& statusSink)
{
    (void)state;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 12.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10.0f, 8.0f));
    ImGui::BeginChild("##worldchild", ImVec2(0, 0), ImGuiChildFlags_Borders);

    static WorldSettingsPanel activePanel = WorldSettingsPanel::None;
    if (ImGui::BeginTable(
            "##world_features",
            2,
            ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoPadOuterX)) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        if (DrawFeatureRow(
                "world_esp",
                "World ESP",
                ui::icons::Icon::Globe,
                &g::espWorld,
                activePanel == WorldSettingsPanel::WorldEsp)) {
            activePanel = activePanel == WorldSettingsPanel::WorldEsp
                ? WorldSettingsPanel::None
                : WorldSettingsPanel::WorldEsp;
        }
        ImGui::TableSetColumnIndex(1);
        if (DrawFeatureRow(
                "dropped_items",
                "Dropped Items",
                ui::icons::Icon::Display,
                &g::espItem,
                activePanel == WorldSettingsPanel::DroppedItems)) {
            activePanel = activePanel == WorldSettingsPanel::DroppedItems
                ? WorldSettingsPanel::None
                : WorldSettingsPanel::DroppedItems;
        }
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        if (DrawFeatureRow(
                "grenade_helper",
                "Grenade Helper",
                ui::icons::Icon::Crosshair,
                &g::grenadeHelperEnabled,
                activePanel == WorldSettingsPanel::GrenadeHelper)) {
            activePanel = activePanel == WorldSettingsPanel::GrenadeHelper
                ? WorldSettingsPanel::None
                : WorldSettingsPanel::GrenadeHelper;
        }
        ImGui::EndTable();
    }

    ImGui::EndChild();
    ImGui::PopStyleVar(2);

    if (activePanel == WorldSettingsPanel::None)
        return;

    const ImVec2 parentPos = ImGui::GetWindowPos();
    ImGui::SetNextWindowPos(ImVec2(parentPos.x + 550.0f, parentPos.y), ImGuiCond_FirstUseEver);
    if (activePanel == WorldSettingsPanel::GrenadeHelper)
        ImGui::SetNextWindowSizeConstraints(ImVec2(760.0f, 0.0f), ImVec2(920.0f, FLT_MAX));
    else if (activePanel == WorldSettingsPanel::DroppedItems)
        ImGui::SetNextWindowSizeConstraints(ImVec2(480.0f, 0.0f), ImVec2(620.0f, FLT_MAX));
    else
        ImGui::SetNextWindowSizeConstraints(ImVec2(380.0f, 0.0f), ImVec2(560.0f, FLT_MAX));
    PushSettingsWindowStyle();

    bool open = true;
    if (activePanel == WorldSettingsPanel::WorldEsp) {
        char title[128] = {};
        std::snprintf(
            title,
            sizeof(title),
            "%s###world_esp_settings_window",
            KEVQ_TR("World ESP Settings"));
        if (ImGui::Begin(title, &open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize)) {
            DrawSettingsWindowHeader();
            ImGui::BeginDisabled(!g::espWorld);
            ui::widgets::ToggleRow("projectiles", "Projectiles", &g::espWorldProjectiles);
            ui::widgets::ToggleRow("smoke_timer", "Smoke Timer", &g::espWorldSmokeTimer);
            ui::widgets::ToggleRow("molotov_timer", "Molotov Timer", &g::espWorldInfernoTimer);
            ui::widgets::ToggleRow("decoy_timer", "Decoy Timer", &g::espWorldDecoyTimer);
            ui::widgets::ColorRow("world_color", "World Color", g::espWorldColor, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoInputs);
            ImGui::EndDisabled();
        }
        ImGui::End();
    } else if (activePanel == WorldSettingsPanel::DroppedItems) {
        char title[128] = {};
        std::snprintf(
            title,
            sizeof(title),
            "%s###dropped_items_settings_window",
            KEVQ_TR("Dropped Items Settings"));
        if (ImGui::Begin(
                title,
                &open,
                ImGuiWindowFlags_NoCollapse |
                    ImGuiWindowFlags_AlwaysAutoResize)) {
            DrawSettingsWindowHeader();
            DrawDroppedItemsSettings();
        }
        ImGui::End();
    } else {
        char title[128] = {};
        std::snprintf(
            title,
            sizeof(title),
            "%s###grenade_helper_settings_window",
            KEVQ_TR("Grenade Helper Settings"));
        if (ImGui::Begin(title, &open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize)) {
            DrawSettingsWindowHeader();
            if (ImGui::BeginTabBar("##grenade_helper_tabs")) {
                if (ImGui::BeginTabItem(KEVQ_TR("Options"))) {
                    ImGui::BeginDisabled(!g::grenadeHelperEnabled);
                    world::grenade_helper::RenderSettings(statusSink);
                    ImGui::EndDisabled();
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem(KEVQ_TR("Lineups"))) {
                    world::grenade_helper::RenderSpotList(statusSink);
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
        }
        ImGui::End();
    }

    if (!open)
        activePanel = WorldSettingsPanel::None;
    PopSettingsWindowStyle();
}
