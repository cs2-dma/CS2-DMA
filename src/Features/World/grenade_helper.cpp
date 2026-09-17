#include "Features/World/grenade_helper.h"

#include "app/Config/project_paths.h"
#include "app/Core/globals.h"
#include "app/Input/input_device.h"
#include "app/Localization/localization.h"
#include "app/Platform/file_replace.h"
#include "app/UI/MenuShell/menu_utils.h"
#include "app/UI/MenuShell/tab_page.h"
#include "app/UI/MenuShell/ui_widgets.h"

#include <Windows.h>
#include <imgui.h>
#include <json/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <numbers>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace
{
    constexpr wchar_t kDefaultsResourceName[] = L"GRENADE_HELPER_DEFAULTS_JSON";
    constexpr size_t kMaxDatabaseBytes = 4u * 1024u * 1024u;
    constexpr size_t kMaxMaps = 128u;
    constexpr size_t kMaxSpotsPerMap = 4096u;
    constexpr size_t kMaxAimPointsPerSpot = 32u;

    enum class GrenadeType : uint8_t { Smoke, Molotov, He, Flash };

    struct AimPoint {
        std::string label;
        GrenadeType type = GrenadeType::Smoke;
        float pitch = 0.0f;
        float yaw = 0.0f;
        std::string throwType;
        mutable std::string displayLabel;
        mutable std::string displayLabelSource;
        mutable app::localization::Language displayLanguage =
            static_cast<app::localization::Language>(0xFF);
    };

    struct GrenadeSpot {
        Vector3 position = {};
        std::vector<AimPoint> aimPoints;
    };

    struct MapSpots {
        std::string map;
        std::vector<GrenadeSpot> spots;
    };

    enum class CaptureAction : uint8_t { None, Toggle, Add, Delete };

    bool IsFinite(float value)
    {
        return std::isfinite(value);
    }

    ImU32 ToColor(const float color[4], float alphaScale = 1.0f)
    {
        return ImGui::ColorConvertFloat4ToU32(ImVec4(
            std::clamp(color[0], 0.0f, 1.0f),
            std::clamp(color[1], 0.0f, 1.0f),
            std::clamp(color[2], 0.0f, 1.0f),
            std::clamp(color[3] * alphaScale, 0.0f, 1.0f)));
    }

    GrenadeType ParseType(std::string_view value)
    {
        if (value == "molotov") return GrenadeType::Molotov;
        if (value == "frag" || value == "he") return GrenadeType::He;
        if (value == "flash") return GrenadeType::Flash;
        return GrenadeType::Smoke;
    }

    const char* TypeKey(GrenadeType type)
    {
        switch (type) {
        case GrenadeType::Molotov: return "molotov";
        case GrenadeType::He: return "frag";
        case GrenadeType::Flash: return "flash";
        default: return "smoke";
        }
    }

    const char* TypeName(GrenadeType type)
    {
        switch (type) {
        case GrenadeType::Molotov: return KEVQ_TR("Molotov");
        case GrenadeType::He: return KEVQ_TR("HE Grenade");
        case GrenadeType::Flash: return KEVQ_TR("Flashbang");
        default: return KEVQ_TR("Smoke");
        }
    }

    const char* LocalizedThrowType(const std::string& throwType)
    {
        return throwType.empty() ? "" : app::localization::Get(throwType.c_str());
    }

    ImU32 TypeColor(GrenadeType type)
    {
        switch (type) {
        case GrenadeType::Molotov: return IM_COL32(255, 132, 48, 255);
        case GrenadeType::He: return IM_COL32(102, 224, 126, 255);
        case GrenadeType::Flash: return IM_COL32(255, 230, 105, 255);
        default: return IM_COL32(184, 194, 216, 255);
        }
    }

    bool IsMapKeySafe(std::string_view map)
    {
        if (map.empty() || map.size() > 63u || map == "unknown")
            return false;
        return std::all_of(map.begin(), map.end(), [](unsigned char c) {
            return (c >= 'a' && c <= 'z') ||
                   (c >= 'A' && c <= 'Z') ||
                   (c >= '0' && c <= '9') || c == '_';
        });
    }

    std::string TrimLineupLabel(std::string_view value)
    {
        constexpr std::string_view whitespace = " \t\r\n";
        const size_t first = value.find_first_not_of(whitespace);
        if (first == std::string_view::npos)
            return {};
        const size_t last = value.find_last_not_of(whitespace);
        return std::string(value.substr(first, last - first + 1u));
    }

    std::string LocalizedDataName(std::string_view prefix, std::string_view value)
    {
        std::string key(prefix);
        key.append(value);
        const std::string translated = app::localization::GetCopy(key);
        return translated == key ? std::string(value) : translated;
    }

    std::string LocalizedMapName(std::string_view mapKey)
    {
        return LocalizedDataName("Grenade map: ", mapKey);
    }

    std::string LocalizedLineupLabel(std::string_view label)
    {
        if (app::localization::GetLanguage() !=
            app::localization::Language::SimplifiedChinese) {
            return std::string(label);
        }

        constexpr std::string_view separator = " From ";
        const size_t separatorAt = label.find(separator);
        if (separatorAt == std::string_view::npos)
            return LocalizedDataName("Grenade callout: ", label);

        const std::string target = LocalizedDataName(
            "Grenade callout: ",
            label.substr(0, separatorAt));
        const std::string source = LocalizedDataName(
            "Grenade callout: ",
            label.substr(separatorAt + separator.size()));
        return app::localization::Format("Lineup: {} from {}", target, source);
    }

    const std::string& DisplayLabel(const AimPoint& aim)
    {
        const app::localization::Language language =
            app::localization::GetLanguage();
        if (aim.displayLanguage != language ||
            aim.displayLabelSource != aim.label) {
            aim.displayLabel = LocalizedLineupLabel(aim.label);
            aim.displayLabelSource = aim.label;
            aim.displayLanguage = language;
        }
        return aim.displayLabel;
    }

    bool ContainsSearchText(std::string_view text, std::string_view query)
    {
        if (query.empty())
            return true;
        return std::search(
            text.begin(),
            text.end(),
            query.begin(),
            query.end(),
            [](unsigned char lhs, unsigned char rhs) {
                if (lhs < 0x80u && rhs < 0x80u)
                    return std::tolower(lhs) == std::tolower(rhs);
                return lhs == rhs;
            }) != text.end();
    }

    bool ReadResourceText(std::string* output)
    {
        if (!output)
            return false;
        const HMODULE module = GetModuleHandleW(nullptr);
        const HRSRC resource = FindResourceW(module, kDefaultsResourceName, RT_RCDATA);
        if (!resource)
            return false;
        const DWORD size = SizeofResource(module, resource);
        if (size == 0 || size > kMaxDatabaseBytes)
            return false;
        const HGLOBAL loaded = LoadResource(module, resource);
        const void* data = loaded ? LockResource(loaded) : nullptr;
        if (!data)
            return false;
        output->assign(static_cast<const char*>(data), static_cast<size_t>(size));
        return true;
    }

    bool ReadFileText(const std::filesystem::path& path, std::string* output)
    {
        if (path.empty() || !output)
            return false;
        std::error_code ec;
        const uintmax_t size = std::filesystem::file_size(path, ec);
        if (ec || size == 0u || size > kMaxDatabaseBytes)
            return false;
        std::ifstream input(path, std::ios::binary);
        if (!input.is_open())
            return false;
        output->assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
        return input.good() || input.eof();
    }

    bool ReadFiniteNumber(const nlohmann::json& object, const char* key, float* output)
    {
        if (!output || !object.is_object())
            return false;
        const auto it = object.find(key);
        if (it == object.end() || !it->is_number())
            return false;
        const float value = it->get<float>();
        if (!IsFinite(value))
            return false;
        *output = value;
        return true;
    }

    bool ParseDatabase(std::string_view text, std::vector<MapSpots>* output)
    {
        if (!output || text.empty() || text.size() > kMaxDatabaseBytes)
            return false;
        const nlohmann::json root = nlohmann::json::parse(text, nullptr, false);
        if (root.is_discarded() || !root.is_object())
            return false;
        const auto mapsIt = root.find("maps");
        if (mapsIt == root.end() || !mapsIt->is_array() || mapsIt->size() > kMaxMaps) {
            return false;
        }

        std::vector<MapSpots> parsed;
        parsed.reserve(mapsIt->size());
        for (const auto& mapJson : *mapsIt) {
            if (!mapJson.is_object())
                continue;
            const auto mapIt = mapJson.find("map");
            const auto spotsIt = mapJson.find("spots");
            if (mapIt == mapJson.end() || !mapIt->is_string() ||
                spotsIt == mapJson.end() || !spotsIt->is_array() ||
                spotsIt->size() > kMaxSpotsPerMap) {
                continue;
            }
            MapSpots map;
            map.map = mapIt->get<std::string>();
            if (!IsMapKeySafe(map.map))
                continue;
            map.spots.reserve(spotsIt->size());
            for (const auto& spotJson : *spotsIt) {
                GrenadeSpot spot;
                if (!ReadFiniteNumber(spotJson, "x", &spot.position.x) ||
                    !ReadFiniteNumber(spotJson, "y", &spot.position.y) ||
                    !ReadFiniteNumber(spotJson, "z", &spot.position.z)) {
                    continue;
                }
                const auto aimsIt = spotJson.find("aim_points");
                if (aimsIt == spotJson.end() || !aimsIt->is_array() ||
                    aimsIt->empty() || aimsIt->size() > kMaxAimPointsPerSpot) {
                    continue;
                }
                spot.aimPoints.reserve(aimsIt->size());
                for (const auto& aimJson : *aimsIt) {
                    if (!aimJson.is_object())
                        continue;
                    const auto labelIt = aimJson.find("label");
                    if (labelIt == aimJson.end() || !labelIt->is_string())
                        continue;
                    AimPoint aim;
                    aim.label = labelIt->get<std::string>();
                    if (aim.label.empty() || aim.label.size() > 127u ||
                        !ReadFiniteNumber(aimJson, "pitch", &aim.pitch) ||
                        !ReadFiniteNumber(aimJson, "yaw", &aim.yaw)) {
                        continue;
                    }
                    const auto typeIt = aimJson.find("type");
                    if (typeIt != aimJson.end() && typeIt->is_string())
                        aim.type = ParseType(typeIt->get<std::string>());
                    const auto throwIt = aimJson.find("throw_type");
                    if (throwIt != aimJson.end() && throwIt->is_string()) {
                        aim.throwType = throwIt->get<std::string>();
                        if (aim.throwType.size() > 63u)
                            aim.throwType.resize(63u);
                    }
                    aim.pitch = std::clamp(aim.pitch, -89.0f, 89.0f);
                    aim.yaw = std::remainder(aim.yaw, 360.0f);
                    spot.aimPoints.push_back(std::move(aim));
                }
                if (!spot.aimPoints.empty())
                    map.spots.push_back(std::move(spot));
            }
            if (!map.spots.empty())
                parsed.push_back(std::move(map));
        }
        if (parsed.empty())
            return false;
        *output = std::move(parsed);
        return true;
    }

    bool Project(
        const Vector3& world,
        const view_matrix_t& matrix,
        float screenWidth,
        float screenHeight,
        ImVec2* output,
        bool requireOnScreen)
    {
        if (!output || !IsFiniteVec(world) || screenWidth <= 1.0f || screenHeight <= 1.0f)
            return false;
        const float w = matrix[3][0] * world.x + matrix[3][1] * world.y +
                        matrix[3][2] * world.z + matrix[3][3];
        if (!IsFinite(w) || w < 0.001f)
            return false;
        const float nx = (matrix[0][0] * world.x + matrix[0][1] * world.y +
                          matrix[0][2] * world.z + matrix[0][3]) / w;
        const float ny = (matrix[1][0] * world.x + matrix[1][1] * world.y +
                          matrix[1][2] * world.z + matrix[1][3]) / w;
        if (!IsFinite(nx) || !IsFinite(ny) || std::fabs(nx) > 16.0f || std::fabs(ny) > 16.0f)
            return false;
        output->x = screenWidth * 0.5f + nx * screenWidth * 0.5f;
        output->y = screenHeight * 0.5f - ny * screenHeight * 0.5f;
        return !requireOnScreen ||
               (output->x >= -64.0f && output->x <= screenWidth + 64.0f &&
                output->y >= -64.0f && output->y <= screenHeight + 64.0f);
    }

    class GrenadeHelperService
    {
    public:
        void Draw(
            const view_matrix_t& viewMatrix,
            const Vector3& localPosition,
            const Vector3& viewAngles,
            const char* mapKey,
            float screenWidth,
            float screenHeight)
        {
            EnsureLoaded();
            currentMap_ = mapKey && IsMapKeySafe(mapKey) ? mapKey : std::string();
            currentPosition_ = localPosition;
            currentAngles_ = viewAngles;
            HandleHotkeys();

            if (!g::grenadeHelperEnabled || !g::grenadeHelperVisible ||
                currentMap_.empty() || !IsFiniteVec(localPosition)) {
                return;
            }
            ImDrawList* drawList = ImGui::GetBackgroundDrawList();
            const MapSpots* map = FindMap(currentMap_);
            if (!drawList || !map)
                return;

            const float maxDistance = std::clamp(g::grenadeHelperMaxDistance, 500.0f, 6000.0f);
            const float maxDistanceSq = maxDistance * maxDistance;
            ImFont* font = g::fontEspName ? g::fontEspName : ImGui::GetFont();
            const float fontSize = std::clamp(g::grenadeHelperTextSize, 10.0f, 24.0f);
            for (const GrenadeSpot& spot : map->spots) {
                const Vector3 delta = spot.position - localPosition;
                if ((delta.x * delta.x + delta.y * delta.y + delta.z * delta.z) > maxDistanceSq ||
                    !SpotPassesFilter(spot)) {
                    continue;
                }
                const bool active = IsPlayerInSpot(spot, localPosition);
                DrawSpot(drawList, spot, active, viewMatrix, screenWidth, screenHeight, font, fontSize);
                if (active) {
                    for (const AimPoint& aim : spot.aimPoints) {
                        if (AimPassesFilter(aim))
                            DrawAim(drawList, aim, localPosition, viewMatrix, screenWidth, screenHeight, font, fontSize);
                    }
                }
            }
        }

        void RenderSettings(ui::IStatusSink& statusSink)
        {
            EnsureLoaded();
            UpdateKeyCapture(statusSink);
            if (ImGui::BeginTable(
                    "##grenade_settings_columns",
                    2,
                    ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoPadOuterX)) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::PushID("general");
                ui::widgets::ToggleRow("visible", "Show Lineups", &g::grenadeHelperVisible);
                ui::widgets::ToggleRow("smoke", "Smoke", &g::grenadeHelperSmoke);
                ui::widgets::ToggleRow("molotov", "Molotov", &g::grenadeHelperMolotov);
                ui::widgets::ToggleRow("he", "HE Grenade", &g::grenadeHelperHe);
                ui::widgets::ToggleRow("flash", "Flashbang", &g::grenadeHelperFlash);
                ui::widgets::SliderFloatRow("radius", "Activation Radius", &g::grenadeHelperRadius, 4.0f, 64.0f, "%.0f");
                ui::widgets::SliderFloatRow("distance", "Display Distance", &g::grenadeHelperMaxDistance, 500.0f, 6000.0f, "%.0f");
                ui::widgets::SliderFloatRow("thickness", "Circle Thickness", &g::grenadeHelperThickness, 0.5f, 5.0f, "%.1f");
                ImGui::PopID();

                ImGui::TableSetColumnIndex(1);
                ImGui::PushID("appearance");
                ui::widgets::SliderFloatRow("text_size", "Text Size", &g::grenadeHelperTextSize, 10.0f, 24.0f, "%.0f");
                ui::widgets::ColorRow("circle", "Circle Color", g::grenadeHelperCircleColor, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoInputs);
                ui::widgets::ColorRow("active", "Active Color", g::grenadeHelperActiveColor, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoInputs);
                ui::widgets::ColorRow("aim", "Aim Line Color", g::grenadeHelperAimColor, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoInputs);
                ui::widgets::ColorRow("text", "Text Color", g::grenadeHelperTextColor, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoInputs);
                RenderHotkeyRow("toggle_key", "Visibility Key", CaptureAction::Toggle, &g::grenadeHelperToggleKey);
                RenderHotkeyRow("add_key", "Add Lineup Key", CaptureAction::Add, &g::grenadeHelperAddKey);
                RenderHotkeyRow("delete_key", "Delete Lineup Key", CaptureAction::Delete, &g::grenadeHelperDeleteKey);
                ImGui::PopID();
                ImGui::EndTable();
            }
            ImGui::Separator();
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextDisabled(
                "%s",
                app::localization::Format(
                    "Hotkeys: {} Show/hide | {} Add lineup | {} Remove nearest",
                    key_names::ToDisplayName(g::grenadeHelperToggleKey),
                    key_names::ToDisplayName(g::grenadeHelperAddKey),
                    key_names::ToDisplayName(g::grenadeHelperDeleteKey)).c_str());
            ImGui::PopTextWrapPos();
        }

        void RenderSpotList(ui::IStatusSink& statusSink)
        {
            EnsureLoaded();
            ui::widgets::SectionTitle("Grenade Lineups");
            if (!loaded_) {
                ImGui::TextDisabled("%s", KEVQ_TR("Grenade database unavailable."));
                return;
            }
            ImGui::TextDisabled("%s", app::localization::Format(
                "{} maps | {} spots | {} aim points",
                maps_.size(), SpotCount(), AimCount()).c_str());
            ImGui::Separator();
            if (currentMap_ != lineupListObservedMap_) {
                lineupListObservedMap_ = currentMap_;
                if (FindMap(currentMap_))
                    lineupListMap_ = currentMap_;
            }
            if (!FindMap(lineupListMap_) && !maps_.empty())
                lineupListMap_ = maps_.front().map;

            const std::string selectedMapLabel = lineupListMap_.empty()
                ? KEVQ_TR("Current Map")
                : LocalizedMapName(lineupListMap_);
            const float availableWidth = ImGui::GetContentRegionAvail().x;
            const float mapWidth = std::clamp(availableWidth * 0.38f, 150.0f, 260.0f);
            ImGui::SetNextItemWidth(mapWidth);
            if (ImGui::BeginCombo("##grenade_lineup_map", selectedMapLabel.c_str())) {
                for (const MapSpots& candidate : maps_) {
                    const bool selected = candidate.map == lineupListMap_;
                    const std::string candidateLabel =
                        LocalizedMapName(candidate.map) + "###" + candidate.map;
                    if (ImGui::Selectable(candidateLabel.c_str(), selected))
                        lineupListMap_ = candidate.map;
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputTextWithHint(
                "##grenade_lineup_search",
                KEVQ_TR("Search lineups..."),
                lineupSearchBuffer_.data(),
                lineupSearchBuffer_.size());

            MapSpots* map = FindMap(lineupListMap_);
            if (!map) {
                ImGui::TextDisabled("%s", KEVQ_TR("Grenade database unavailable."));
            } else {
                const std::string_view query(lineupSearchBuffer_.data());
                const auto matchesQuery = [&](const AimPoint& aim) {
                    return query.empty() ||
                        ContainsSearchText(DisplayLabel(aim), query) ||
                        ContainsSearchText(aim.label, query) ||
                        ContainsSearchText(TypeName(aim.type), query) ||
                        ContainsSearchText(LocalizedThrowType(aim.throwType), query);
                };
                size_t matchingAimCount = 0;
                size_t matchingSpotCount = 0;
                for (const GrenadeSpot& spot : map->spots) {
                    const size_t spotMatches = static_cast<size_t>(std::count_if(
                        spot.aimPoints.begin(),
                        spot.aimPoints.end(),
                        matchesQuery));
                    matchingAimCount += spotMatches;
                    matchingSpotCount += spotMatches > 0u ? 1u : 0u;
                }
                ImGui::TextDisabled("%s", app::localization::Format(
                    "{} lineups | {} spots",
                    matchingAimCount,
                    matchingSpotCount).c_str());
                int deleteSpot = -1;
                int deleteAim = -1;
                const float listHeight = std::clamp(
                    ImGui::GetContentRegionAvail().y - 48.0f,
                    180.0f,
                    280.0f);
                if (ImGui::BeginChild(
                        "##grenade_lineup_scroll",
                        ImVec2(0.0f, listHeight),
                        ImGuiChildFlags_Borders,
                        ImGuiWindowFlags_AlwaysVerticalScrollbar)) {
                    for (int spotIndex = 0; spotIndex < static_cast<int>(map->spots.size()); ++spotIndex) {
                        GrenadeSpot& spot = map->spots[spotIndex];
                        const int visibleAimCount = static_cast<int>(std::count_if(
                            spot.aimPoints.begin(),
                            spot.aimPoints.end(),
                            matchesQuery));
                        if (visibleAimCount == 0)
                            continue;
                        ImGui::PushID(spotIndex);
                        const std::string header = app::localization::Format(
                            "Spot {} | {} lineups",
                            spotIndex + 1,
                            visibleAimCount);
                        if (ImGui::CollapsingHeader(header.c_str())) {
                            ImGui::TextDisabled(
                                "%.0f, %.0f, %.0f",
                                spot.position.x,
                                spot.position.y,
                                spot.position.z);
                            if (ImGui::BeginTable(
                                    "##aim_points",
                                    2,
                                    ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerH)) {
                                ImGui::TableSetupColumn("##label", ImGuiTableColumnFlags_WidthStretch);
                                ImGui::TableSetupColumn("##actions", ImGuiTableColumnFlags_WidthFixed, 38.0f);
                                for (int aimIndex = 0; aimIndex < static_cast<int>(spot.aimPoints.size()); ++aimIndex) {
                                    const AimPoint& aim = spot.aimPoints[aimIndex];
                                    if (!matchesQuery(aim))
                                        continue;
                                    ImGui::PushID(aimIndex);
                                    ImGui::TableNextRow(ImGuiTableRowFlags_None, 42.0f);
                                    ImGui::TableSetColumnIndex(0);
                                    ImGui::TextColored(
                                        ImGui::ColorConvertU32ToFloat4(TypeColor(aim.type)),
                                        "%s",
                                        TypeName(aim.type));
                                    ImGui::SameLine();
                                    ImGui::TextWrapped("%s", DisplayLabel(aim).c_str());
                                    if (!aim.throwType.empty())
                                        ImGui::TextDisabled("%s", LocalizedThrowType(aim.throwType));
                                    ImGui::TableSetColumnIndex(1);
                                    if (ImGui::SmallButton("..."))
                                        ImGui::OpenPopup("##lineup_actions");
                                    if (ImGui::IsItemHovered())
                                        ImGui::SetItemTooltip("%s", KEVQ_TR("Actions"));
                                    if (ImGui::BeginPopup("##lineup_actions")) {
                                        if (ImGui::MenuItem(KEVQ_TR("Edit"))) {
                                            editMap_ = static_cast<int>(map - maps_.data());
                                            editSpot_ = spotIndex;
                                            editAim_ = aimIndex;
                                            strncpy_s(
                                                editLabelBuffer_.data(),
                                                editLabelBuffer_.size(),
                                                aim.label.c_str(),
                                                _TRUNCATE);
                                            editPopup_ = true;
                                        }
                                        if (ImGui::MenuItem(KEVQ_TR("Delete"))) {
                                            deleteSpot = spotIndex;
                                            deleteAim = aimIndex;
                                        }
                                        ImGui::EndPopup();
                                    }
                                    ImGui::PopID();
                                }
                                ImGui::EndTable();
                            }
                            if (ImGui::SmallButton(KEVQ_TR("Delete Spot")))
                                deleteSpot = spotIndex;
                        }
                        ImGui::PopID();
                    }
                    if (matchingAimCount == 0u)
                        ImGui::TextDisabled("%s", KEVQ_TR("No matching lineups."));
                }
                ImGui::EndChild();
                if (deleteSpot >= 0 && deleteSpot < static_cast<int>(map->spots.size())) {
                    GrenadeSpot& spot = map->spots[deleteSpot];
                    if (deleteAim >= 0 && deleteAim < static_cast<int>(spot.aimPoints.size()))
                        spot.aimPoints.erase(spot.aimPoints.begin() + deleteAim);
                    if (deleteAim < 0 || spot.aimPoints.empty())
                        map->spots.erase(map->spots.begin() + deleteSpot);
                    RemoveEmptyMaps();
                    statusSink.SetStatus(Save() ? "Grenade lineup deleted." : "Grenade lineups could not be saved.");
                }
            }
            ImGui::Separator();
            if (ui::widgets::FullButton("Restore Built-in Lineups")) {
                std::error_code ec;
                std::filesystem::remove(UserPath(), ec);
                loaded_ = LoadDefaults();
                statusSink.SetStatus(loaded_ ? "Built-in lineups restored." : "Grenade database unavailable.");
            }
        }

        void RenderPopups(ui::IStatusSink& statusSink)
        {
            RenderAddPopup(statusSink);
            RenderDeletePopup(statusSink);
            RenderEditPopup(statusSink);
        }

    private:
        void EnsureLoaded()
        {
            if (loadAttempted_)
                return;
            loadAttempted_ = true;
            std::string text;
            if (ReadFileText(UserPath(), &text) && ParseDatabase(text, &maps_)) {
                loaded_ = true;
                return;
            }
            loaded_ = LoadDefaults();
        }

        bool LoadDefaults()
        {
            std::string text;
            std::vector<MapSpots> defaults;
            if (!ReadResourceText(&text) || !ParseDatabase(text, &defaults))
                return false;
            maps_ = std::move(defaults);
            return true;
        }

        std::filesystem::path UserPath() const
        {
            const auto directory = app::paths::GetConfigDirectory();
            return directory.empty() ? std::filesystem::path{} : directory / "grenade_helper.json";
        }

        bool Save() const
        {
            const auto path = UserPath();
            if (path.empty())
                return false;
            nlohmann::ordered_json root;
            root["version"] = 1;
            root["maps"] = nlohmann::ordered_json::array();
            for (const MapSpots& map : maps_) {
                nlohmann::ordered_json mapJson;
                mapJson["map"] = map.map;
                mapJson["spots"] = nlohmann::ordered_json::array();
                for (const GrenadeSpot& spot : map.spots) {
                    nlohmann::ordered_json spotJson;
                    spotJson["x"] = spot.position.x;
                    spotJson["y"] = spot.position.y;
                    spotJson["z"] = spot.position.z;
                    spotJson["aim_points"] = nlohmann::ordered_json::array();
                    for (const AimPoint& aim : spot.aimPoints) {
                        spotJson["aim_points"].push_back({
                            {"label", aim.label}, {"type", TypeKey(aim.type)},
                            {"pitch", aim.pitch}, {"yaw", aim.yaw},
                            {"throw_type", aim.throwType}
                        });
                    }
                    mapJson["spots"].push_back(std::move(spotJson));
                }
                root["maps"].push_back(std::move(mapJson));
            }
            std::error_code ec;
            std::filesystem::create_directories(path.parent_path(), ec);
            if (ec)
                return false;
            auto temp = path;
            temp += ".tmp";
            {
                std::ofstream output(temp, std::ios::binary | std::ios::trunc);
                if (!output.is_open())
                    return false;
                output << root.dump(2) << '\n';
                if (!output.good()) {
                    output.close();
                    std::filesystem::remove(temp, ec);
                    return false;
                }
            }
            if (!app::platform::ReplaceFileWithTemp(temp, path, ec)) {
                std::filesystem::remove(temp, ec);
                return false;
            }
            return true;
        }

        MapSpots* FindMap(std::string_view key)
        {
            const auto it = std::find_if(maps_.begin(), maps_.end(), [&](const MapSpots& map) { return map.map == key; });
            return it == maps_.end() ? nullptr : &*it;
        }

        const MapSpots* FindMap(std::string_view key) const
        {
            const auto it = std::find_if(maps_.begin(), maps_.end(), [&](const MapSpots& map) { return map.map == key; });
            return it == maps_.end() ? nullptr : &*it;
        }

        MapSpots& GetOrCreateMap(const std::string& key)
        {
            if (MapSpots* existing = FindMap(key))
                return *existing;
            maps_.push_back({key, {}});
            return maps_.back();
        }

        size_t SpotCount() const
        {
            size_t count = 0;
            for (const MapSpots& map : maps_)
                count += map.spots.size();
            return count;
        }

        size_t AimCount() const
        {
            size_t count = 0;
            for (const MapSpots& map : maps_)
                for (const GrenadeSpot& spot : map.spots)
                    count += spot.aimPoints.size();
            return count;
        }

        void RemoveEmptyMaps()
        {
            maps_.erase(std::remove_if(maps_.begin(), maps_.end(), [](const MapSpots& map) {
                return map.spots.empty();
            }), maps_.end());
        }

        bool AimPassesFilter(const AimPoint& aim) const
        {
            switch (aim.type) {
            case GrenadeType::Smoke: return g::grenadeHelperSmoke;
            case GrenadeType::Molotov: return g::grenadeHelperMolotov;
            case GrenadeType::He: return g::grenadeHelperHe;
            case GrenadeType::Flash: return g::grenadeHelperFlash;
            }
            return false;
        }

        bool SpotPassesFilter(const GrenadeSpot& spot) const
        {
            return std::any_of(spot.aimPoints.begin(), spot.aimPoints.end(), [&](const AimPoint& aim) {
                return AimPassesFilter(aim);
            });
        }

        bool IsPlayerInSpot(const GrenadeSpot& spot, const Vector3& player) const
        {
            const float dx = player.x - spot.position.x;
            const float dy = player.y - spot.position.y;
            const float radius = std::clamp(g::grenadeHelperRadius, 4.0f, 64.0f);
            return dx * dx + dy * dy <= radius * radius &&
                   std::fabs(player.z - spot.position.z) < 128.0f;
        }

        void DrawSpot(
            ImDrawList* drawList,
            const GrenadeSpot& spot,
            bool active,
            const view_matrix_t& matrix,
            float screenWidth,
            float screenHeight,
            ImFont* font,
            float fontSize) const
        {
            constexpr int kSegments = 32;
            std::array<ImVec2, kSegments> points = {};
            const float radius = std::clamp(g::grenadeHelperRadius, 4.0f, 64.0f);
            for (int i = 0; i < kSegments; ++i) {
                const float angle = static_cast<float>(i) / static_cast<float>(kSegments) * 2.0f * std::numbers::pi_v<float>;
                const Vector3 point(
                    spot.position.x + radius * std::cos(angle),
                    spot.position.y + radius * std::sin(angle),
                    spot.position.z);
                if (!Project(point, matrix, screenWidth, screenHeight, &points[i], true))
                    return;
            }
            const ImU32 circleColor = active ? ToColor(g::grenadeHelperActiveColor) : ToColor(g::grenadeHelperCircleColor);
            drawList->AddPolyline(points.data(), kSegments, circleColor, ImDrawFlags_Closed,
                std::clamp(g::grenadeHelperThickness, 0.5f, 5.0f));
            if (active)
                return;

            ImVec2 ground = {};
            ImVec2 labelAnchor = {};
            if (!Project(spot.position, matrix, screenWidth, screenHeight, &ground, true) ||
                !Project(spot.position + Vector3(0.0f, 0.0f, 30.0f), matrix, screenWidth, screenHeight, &labelAnchor, true)) {
                return;
            }
            drawList->AddLine(ground, labelAnchor, ToColor(g::grenadeHelperCircleColor, 0.65f), 1.0f);
            float y = labelAnchor.y - 4.0f;
            for (auto it = spot.aimPoints.rbegin(); it != spot.aimPoints.rend(); ++it) {
                if (!AimPassesFilter(*it))
                    continue;
                const std::string& label = DisplayLabel(*it);
                const ImVec2 size = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, label.c_str());
                y -= size.y + 1.0f;
                const ImVec2 position(labelAnchor.x - size.x * 0.5f, y);
                drawList->AddText(font, fontSize, ImVec2(position.x + 1.0f, position.y + 1.0f), IM_COL32(0, 0, 0, 175), label.c_str());
                drawList->AddText(font, fontSize, position, TypeColor(it->type), label.c_str());
            }
        }

        void DrawAim(
            ImDrawList* drawList,
            const AimPoint& aim,
            const Vector3& localPosition,
            const view_matrix_t& matrix,
            float screenWidth,
            float screenHeight,
            ImFont* font,
            float fontSize) const
        {
            const float pitch = aim.pitch * std::numbers::pi_v<float> / 180.0f;
            const float yaw = aim.yaw * std::numbers::pi_v<float> / 180.0f;
            const float cosPitch = std::cos(pitch);
            const Vector3 direction(cosPitch * std::cos(yaw), cosPitch * std::sin(yaw), -std::sin(pitch));
            const Vector3 eye = localPosition + Vector3(0.0f, 0.0f, 64.0f);
            ImVec2 aimScreen = {};
            if (!Project(eye + direction * 8000.0f, matrix, screenWidth, screenHeight, &aimScreen, false))
                return;
            constexpr float margin = 16.0f;
            aimScreen.x = std::clamp(aimScreen.x, margin, screenWidth - margin);
            aimScreen.y = std::clamp(aimScreen.y, margin, screenHeight - margin);
            const ImVec2 center(screenWidth * 0.5f, screenHeight * 0.5f);
            const ImU32 lineColor = ToColor(g::grenadeHelperAimColor);
            drawList->AddLine(center, aimScreen, lineColor, 1.0f);
            drawList->AddCircleFilled(aimScreen, 3.5f, TypeColor(aim.type), 16);

            const float side = aimScreen.x < center.x ? -1.0f : 1.0f;
            const float vertical = aimScreen.y < center.y ? 1.0f : -1.0f;
            const ImVec2 elbow(
                aimScreen.x + side * 14.1421f,
                aimScreen.y - vertical * 14.1421f);
            const ImVec2 end(elbow.x + side * 60.0f, elbow.y);
            drawList->AddLine(aimScreen, elbow, lineColor, 1.25f);
            drawList->AddLine(elbow, end, lineColor, 1.25f);

            const std::string& label = DisplayLabel(aim);
            const ImVec2 labelSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, label.c_str());
            const char* localizedThrowType = LocalizedThrowType(aim.throwType);
            const ImVec2 throwSize = aim.throwType.empty()
                ? ImVec2(0.0f, 0.0f)
                : font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, localizedThrowType);
            const float width = std::max(labelSize.x, throwSize.x);
            const float height = labelSize.y + (aim.throwType.empty() ? 0.0f : throwSize.y + 2.0f);
            float x = side > 0.0f ? end.x + 4.0f : end.x - width - 4.0f;
            float y = end.y - height * 0.5f;
            x = std::clamp(x, 2.0f, screenWidth - width - 2.0f);
            y = std::clamp(y, 2.0f, screenHeight - height - 2.0f);
            const ImU32 textColor = ToColor(g::grenadeHelperTextColor);
            drawList->AddText(font, fontSize, ImVec2(x + 1.0f, y + 1.0f), IM_COL32(0, 0, 0, 185), label.c_str());
            drawList->AddText(font, fontSize, ImVec2(x, y), textColor, label.c_str());
            if (!aim.throwType.empty()) {
                y += labelSize.y + 2.0f;
                drawList->AddText(font, fontSize, ImVec2(x + 1.0f, y + 1.0f), IM_COL32(0, 0, 0, 185), localizedThrowType);
                drawList->AddText(font, fontSize, ImVec2(x, y), ToColor(g::grenadeHelperTextColor, 0.72f), localizedThrowType);
            }
        }

        void HandleHotkeys()
        {
            if (!g::grenadeHelperEnabled || captureAction_ != CaptureAction::None || ImGui::GetIO().WantTextInput)
                return;
            if (g::grenadeHelperToggleKey >= 0x08 && g::grenadeHelperToggleKey <= 0xFE &&
                ConsumeHotkeyPress(
                    g::grenadeHelperToggleKey,
                    trackedToggleKey_,
                    toggleKeyWasDown_))
                g::grenadeHelperVisible = !g::grenadeHelperVisible;
            if (g::grenadeHelperAddKey >= 0x08 && g::grenadeHelperAddKey <= 0xFE &&
                ConsumeHotkeyPress(
                    g::grenadeHelperAddKey,
                    trackedAddKey_,
                    addKeyWasDown_) &&
                !addPopup_ && !deletePopup_ && !currentMap_.empty()) {
                pendingPosition_ = currentPosition_;
                pendingPitch_ = currentAngles_.x;
                pendingYaw_ = currentAngles_.y;
                labelBuffer_[0] = '\0';
                throwBuffer_[0] = '\0';
                importBuffer_[0] = '\0';
                pendingType_ = 0;
                addPopup_ = true;
                g::menuOpen = true;
            }
            if (g::grenadeHelperDeleteKey >= 0x08 && g::grenadeHelperDeleteKey <= 0xFE &&
                ConsumeHotkeyPress(
                    g::grenadeHelperDeleteKey,
                    trackedDeleteKey_,
                    deleteKeyWasDown_) &&
                !addPopup_ && !deletePopup_) {
                FindNearestActiveSpot(&deleteMap_, &deleteSpot_);
                if (deleteSpot_ >= 0) {
                    deletePopup_ = true;
                    g::menuOpen = true;
                }
            }
        }

        static bool ConsumeHotkeyPress(
            int key,
            int& trackedKey,
            bool& wasDown)
        {
            const bool down = app::input::IsControlKeyDown(key);
            if (trackedKey != key) {
                trackedKey = key;
                wasDown = down;
                return false;
            }
            const bool pressed = down && !wasDown;
            wasDown = down;
            return pressed;
        }

        void FindNearestActiveSpot(int* mapIndex, int* spotIndex) const
        {
            *mapIndex = -1;
            *spotIndex = -1;
            float bestDistanceSq = (std::numeric_limits<float>::max)();
            for (int mi = 0; mi < static_cast<int>(maps_.size()); ++mi) {
                if (maps_[mi].map != currentMap_)
                    continue;
                for (int si = 0; si < static_cast<int>(maps_[mi].spots.size()); ++si) {
                    const GrenadeSpot& spot = maps_[mi].spots[si];
                    if (!IsPlayerInSpot(spot, currentPosition_))
                        continue;
                    const Vector3 delta = spot.position - currentPosition_;
                    const float distanceSq = delta.x * delta.x + delta.y * delta.y + delta.z * delta.z;
                    if (distanceSq < bestDistanceSq) {
                        bestDistanceSq = distanceSq;
                        *mapIndex = mi;
                        *spotIndex = si;
                    }
                }
            }
        }

        void BeginCapture(CaptureAction action)
        {
            captureAction_ = action;
            for (int vk = 0; vk < 256; ++vk)
                keySnapshot_[vk] = app::input::IsControlKeyDown(vk) ? 1u : 0u;
        }

        void UpdateKeyCapture(ui::IStatusSink& statusSink)
        {
            if (captureAction_ == CaptureAction::None)
                return;
            for (int vk = 0x08; vk <= 0xFE; ++vk) {
                const bool down = app::input::IsControlKeyDown(vk);
                const bool wasDown = keySnapshot_[vk] != 0;
                keySnapshot_[vk] = down ? 1u : 0u;
                if (!down || wasDown)
                    continue;
                if (vk == VK_ESCAPE) {
                    captureAction_ = CaptureAction::None;
                    statusSink.SetStatus("Key capture canceled.");
                    return;
                }
                if (vk == VK_LBUTTON || vk == VK_RBUTTON || vk == VK_MBUTTON ||
                    vk == VK_LWIN || vk == VK_RWIN || vk == VK_SHIFT ||
                    vk == VK_CONTROL || vk == VK_MENU) {
                    continue;
                }
                int* target = captureAction_ == CaptureAction::Toggle ? &g::grenadeHelperToggleKey
                    : captureAction_ == CaptureAction::Add ? &g::grenadeHelperAddKey
                    : &g::grenadeHelperDeleteKey;
                *target = vk;
                captureAction_ = CaptureAction::None;
                statusSink.SetStatus("Grenade helper key updated.");
                return;
            }
        }

        void RenderHotkeyRow(const char* id, const char* label, CaptureAction action, int* key)
        {
            const float width = std::max(260.0f, ImGui::GetContentRegionAvail().x - 2.0f);
            const bool capturing = captureAction_ == action;
            const auto row = ui::widgets::BeginControlRow(id, label, width, 42.0f, capturing, 225);
            const std::string keyName = capturing ? KEVQ_TR("Press a key...") : key_names::ToDisplayName(*key);
            ImGui::SetCursorScreenPos(ImVec2(row.max.x - 112.0f, row.min.y + 7.0f));
            if (ImGui::Button(keyName.c_str(), ImVec2(100.0f, 28.0f)))
                BeginCapture(action);
            ui::widgets::EndControlRow(row);
        }

        static bool ParseSetPosAngles(
            const char* text,
            Vector3* position,
            float* pitch,
            float* yaw)
        {
            if (!text || !position || !pitch || !yaw)
                return false;
            std::string normalized(text);
            std::replace(normalized.begin(), normalized.end(), ';', '\n');
            std::istringstream input(normalized);
            std::string line;
            bool parsed = false;
            while (std::getline(input, line)) {
                float a = 0.0f, b = 0.0f, c = 0.0f;
                if (sscanf_s(line.c_str(), " setpos %f %f %f", &a, &b, &c) == 3 &&
                    IsFinite(a) && IsFinite(b) && IsFinite(c)) {
                    *position = Vector3(a, b, c - 64.0f);
                    parsed = true;
                } else if (sscanf_s(line.c_str(), " setang %f %f %f", &a, &b, &c) == 3 &&
                           IsFinite(a) && IsFinite(b)) {
                    *pitch = std::clamp(a, -89.0f, 89.0f);
                    *yaw = std::remainder(b, 360.0f);
                    parsed = true;
                }
            }
            return parsed;
        }

        void RenderAddPopup(ui::IStatusSink& statusSink)
        {
            if (!addPopup_)
                return;
            const std::string title = std::string(KEVQ_TR("Add Grenade Lineup")) + "###grenade_add_popup";
            ImGui::OpenPopup(title.c_str());
            ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
            ImGui::SetNextWindowSize(ImVec2(460.0f, 0.0f), ImGuiCond_Appearing);
            if (!ImGui::BeginPopupModal(title.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
                return;
            ImGui::Text("%s", app::localization::Format("Map: {}", currentMap_).c_str());
            ImGui::Text("%s", app::localization::Format(
                "Position: {:.2f} {:.2f} {:.2f}", pendingPosition_.x, pendingPosition_.y, pendingPosition_.z).c_str());
            ImGui::Separator();
            ImGui::TextUnformatted(KEVQ_TR("Import setpos/setang"));
            ImGui::InputTextMultiline("##grenade_import", importBuffer_.data(), importBuffer_.size(), ImVec2(-76.0f, 52.0f));
            ImGui::SameLine();
            if (ImGui::Button(KEVQ_TR("Parse"), ImVec2(66.0f, 52.0f))) {
                if (!ParseSetPosAngles(importBuffer_.data(), &pendingPosition_, &pendingPitch_, &pendingYaw_))
                    statusSink.SetStatus("No valid setpos/setang command found.");
            }
            ImGui::TextUnformatted(KEVQ_TR("Lineup Label"));
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputText("##grenade_label", labelBuffer_.data(), labelBuffer_.size());
            ImGui::TextUnformatted(KEVQ_TR("Grenade Type"));
            ImGui::RadioButton(KEVQ_TR("Smoke"), &pendingType_, 0); ImGui::SameLine();
            ImGui::RadioButton(KEVQ_TR("Molotov"), &pendingType_, 1); ImGui::SameLine();
            ImGui::RadioButton(KEVQ_TR("HE Grenade"), &pendingType_, 2); ImGui::SameLine();
            ImGui::RadioButton(KEVQ_TR("Flashbang"), &pendingType_, 3);
            ImGui::TextUnformatted(KEVQ_TR("Throw Type"));
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputText("##grenade_throw", throwBuffer_.data(), throwBuffer_.size());
            const bool canSave = labelBuffer_[0] != '\0' && !currentMap_.empty() && IsFiniteVec(pendingPosition_);
            ImGui::BeginDisabled(!canSave);
            if (ImGui::Button(KEVQ_TR("Save"), ImVec2(120.0f, 0.0f))) {
                AimPoint aim;
                aim.label = labelBuffer_.data();
                aim.type = static_cast<GrenadeType>(std::clamp(pendingType_, 0, 3));
                aim.pitch = pendingPitch_;
                aim.yaw = pendingYaw_;
                aim.throwType = throwBuffer_.data();
                MapSpots& map = GetOrCreateMap(currentMap_);
                GrenadeSpot* target = nullptr;
                for (GrenadeSpot& spot : map.spots) {
                    if ((spot.position - pendingPosition_).Length() < 5.0f) {
                        target = &spot;
                        break;
                    }
                }
                if (!target) {
                    map.spots.push_back({pendingPosition_, {}});
                    target = &map.spots.back();
                }
                target->aimPoints.push_back(std::move(aim));
                const bool saved = Save();
                addPopup_ = false;
                ImGui::CloseCurrentPopup();
                statusSink.SetStatus(saved ? "Grenade lineup saved." : "Grenade lineups could not be saved.");
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button(KEVQ_TR("Cancel"), ImVec2(120.0f, 0.0f))) {
                addPopup_ = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        void RenderDeletePopup(ui::IStatusSink& statusSink)
        {
            if (!deletePopup_)
                return;
            const std::string title = std::string(KEVQ_TR("Delete Grenade Spot")) + "###grenade_delete_popup";
            ImGui::OpenPopup(title.c_str());
            ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
            if (!ImGui::BeginPopupModal(title.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
                return;
            const bool valid = deleteMap_ >= 0 && deleteMap_ < static_cast<int>(maps_.size()) &&
                deleteSpot_ >= 0 && deleteSpot_ < static_cast<int>(maps_[deleteMap_].spots.size());
            if (!valid) {
                deletePopup_ = false;
                ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
                return;
            }
            ImGui::TextUnformatted(KEVQ_TR("Delete this spot and all of its aim points?"));
            for (const AimPoint& aim : maps_[deleteMap_].spots[deleteSpot_].aimPoints)
                ImGui::BulletText("%s (%s)", DisplayLabel(aim).c_str(), TypeName(aim.type));
            if (ImGui::Button(KEVQ_TR("Delete"), ImVec2(120.0f, 0.0f))) {
                maps_[deleteMap_].spots.erase(maps_[deleteMap_].spots.begin() + deleteSpot_);
                RemoveEmptyMaps();
                const bool saved = Save();
                deletePopup_ = false;
                deleteMap_ = -1;
                deleteSpot_ = -1;
                ImGui::CloseCurrentPopup();
                statusSink.SetStatus(saved ? "Grenade lineup deleted." : "Grenade lineups could not be saved.");
            }
            ImGui::SameLine();
            if (ImGui::Button(KEVQ_TR("Cancel"), ImVec2(120.0f, 0.0f))) {
                deletePopup_ = false;
                deleteMap_ = -1;
                deleteSpot_ = -1;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        void RenderEditPopup(ui::IStatusSink& statusSink)
        {
            if (!editPopup_)
                return;
            const std::string title = std::string(KEVQ_TR("Edit Grenade Lineup")) + "###grenade_edit_popup";
            ImGui::OpenPopup(title.c_str());
            ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
            ImGui::SetNextWindowSize(ImVec2(420.0f, 0.0f), ImGuiCond_Appearing);
            if (!ImGui::BeginPopupModal(title.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
                return;

            const bool valid =
                editMap_ >= 0 && editMap_ < static_cast<int>(maps_.size()) &&
                editSpot_ >= 0 && editSpot_ < static_cast<int>(maps_[editMap_].spots.size()) &&
                editAim_ >= 0 && editAim_ < static_cast<int>(maps_[editMap_].spots[editSpot_].aimPoints.size());
            if (!valid) {
                editPopup_ = false;
                ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
                return;
            }

            ImGui::TextUnformatted(KEVQ_TR("Lineup Label"));
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputText("##grenade_edit_label", editLabelBuffer_.data(), editLabelBuffer_.size());
            const std::string editedLabel = TrimLineupLabel(editLabelBuffer_.data());
            const bool canSave = !editedLabel.empty();
            ImGui::BeginDisabled(!canSave);
            if (ImGui::Button(KEVQ_TR("Save"), ImVec2(120.0f, 0.0f))) {
                AimPoint& aim = maps_[editMap_].spots[editSpot_].aimPoints[editAim_];
                const std::string previousLabel = aim.label;
                aim.label = editedLabel;
                const bool saved = Save();
                if (!saved)
                    aim.label = previousLabel;
                editPopup_ = false;
                ImGui::CloseCurrentPopup();
                statusSink.SetStatus(saved ? "Grenade lineup label updated." : "Grenade lineups could not be saved.");
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button(KEVQ_TR("Cancel"), ImVec2(120.0f, 0.0f))) {
                editPopup_ = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        bool loadAttempted_ = false;
        bool loaded_ = false;
        std::vector<MapSpots> maps_;
        std::string currentMap_;
        std::string lineupListMap_;
        std::string lineupListObservedMap_;
        std::array<char, 128> lineupSearchBuffer_ = {};
        Vector3 currentPosition_ = {};
        Vector3 currentAngles_ = {};
        bool addPopup_ = false;
        bool deletePopup_ = false;
        bool editPopup_ = false;
        Vector3 pendingPosition_ = {};
        float pendingPitch_ = 0.0f;
        float pendingYaw_ = 0.0f;
        int pendingType_ = 0;
        std::array<char, 128> labelBuffer_ = {};
        std::array<char, 64> throwBuffer_ = {};
        std::array<char, 512> importBuffer_ = {};
        int deleteMap_ = -1;
        int deleteSpot_ = -1;
        int editMap_ = -1;
        int editSpot_ = -1;
        int editAim_ = -1;
        std::array<char, 128> editLabelBuffer_ = {};
        CaptureAction captureAction_ = CaptureAction::None;
        std::array<uint8_t, 256> keySnapshot_ = {};
        int trackedToggleKey_ = 0;
        int trackedAddKey_ = 0;
        int trackedDeleteKey_ = 0;
        bool toggleKeyWasDown_ = false;
        bool addKeyWasDown_ = false;
        bool deleteKeyWasDown_ = false;
    };

    GrenadeHelperService& Service()
    {
        static GrenadeHelperService service;
        return service;
    }
}

void world::grenade_helper::DrawOverlay(
    const view_matrix_t& viewMatrix,
    const Vector3& localPosition,
    const Vector3& viewAngles,
    const char* mapKey,
    float screenWidth,
    float screenHeight)
{
    Service().Draw(viewMatrix, localPosition, viewAngles, mapKey, screenWidth, screenHeight);
}

void world::grenade_helper::RenderSettings(ui::IStatusSink& statusSink)
{
    Service().RenderSettings(statusSink);
}

void world::grenade_helper::RenderSpotList(ui::IStatusSink& statusSink)
{
    Service().RenderSpotList(statusSink);
}

void world::grenade_helper::RenderPopups(ui::IStatusSink& statusSink)
{
    Service().RenderPopups(statusSink);
}
