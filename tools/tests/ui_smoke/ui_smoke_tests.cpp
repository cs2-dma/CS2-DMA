#include <imgui.h>

#include "app/Localization/localization.h"
#include "app/UI/MenuShell/ui_icons.h"
#include "Features/ESP/UI/esp_sections.h"
#include "Features/ESP/esp.h"
#include "Features/ESP/Render/weapon_icon_atlas.h"
#include "Features/Radar/UI/radar_sections.h"
#include "Features/Target/UI/target_tab.h"
#include "Features/Target/physics_bvh.h"
#include "Features/Target/target.h"
#include "Features/World/UI/world_tab.h"
#include "app/Core/globals.h"
#include "app/Input/input_device.h"
#include "app/Input/primary_keyboard.h"
#include "app/UI/MenuShell/menu_state.h"
#include "app/UI/MenuShell/tab_page.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <limits>
#include <string>

namespace ui::tabs::testing { void OpenTargetSettings(const char* id); }
namespace ui::tabs::esp_sections::testing { void OpenSettings(const char* id); }
namespace ui { void RenderEspPreview(); }
// No GPU resources in this headless suite: exercise the missing-atlas fallback.
bool esp::render::weapon_icons::CalculateDrawSize(uint16_t, float, ImVec2*) noexcept { return false; }
bool esp::render::weapon_icons::Draw(ImDrawList*, uint16_t, const ImVec2&, float, ImU32, bool) noexcept { return false; }

app::input::DeviceStatus app::input::GetDeviceStatus()
{
    DeviceStatus status;
    status.selected = DeviceKind::Makcu;
    status.state = ConnectionState::Connected;
    status.port = "COM_TEST";
    status.physicalButtonsAvailable = true;
    return status;
}

bool app::input::IsHardwareKeyDown(int)
{
    return false;
}

bool app::input::IsActivationKeyDown(int)
{
    return false;
}

bool app::input::IsControlKeyDown(int)
{
    return false;
}

app::input::PrimaryKeyboardStatus app::input::GetPrimaryKeyboardStatus()
{
    return {};
}

target::RuntimeStatus target::GetRuntimeStatus()
{
    RuntimeStatus status;
    status.phase = RuntimePhase::WaitingForKey;
    status.aimSelection = {10, 1, 2, 3, 4, 0};
    status.triggerSelection = {10, 1, 2, 3, 0, 4};
    status.shot = {1, 100, 35, true, 2};
    status.fire.reason = target::FireBlockReason::Hitchance;
    status.fire.hitchancePercent = 78.1f;
    status.fire.centeredHitchancePercent = 79.0f;
    status.fire.requiredHitchancePercent = 80.0f;
    status.fire.requiredDamage = 30;
    return status;
}

target::physics::Stats target::physics::GetStats()
{
    Stats stats;
    stats.state = BuildState::Ready;
    stats.triangles = 1024;
    stats.buildTimeUs = 12500;
    return stats;
}

namespace
{
    int g_failedChecks = 0;
    int g_framesTested = 0;

    void Check(bool condition, const char* expression, int line)
    {
        if (condition)
            return;
        ++g_failedChecks;
        std::cerr << "ui_smoke_tests.cpp:" << line << ": CHECK failed: " << expression << '\n';
    }

#define CHECK(expression) Check((expression), #expression, __LINE__)

    class StatusSink final : public ui::IStatusSink {
    public:
        void SetStatus(const std::string& text) override
        {
            lastStatus = text;
        }

        std::string lastStatus;
    };

    void DrawRuntimeFlagsFixture()
    {
        auto* drawList = ImGui::GetWindowDrawList();
        esp::PlayerData p;
        std::snprintf(p.name, sizeof(p.name), "%s", "Test player");
        p.flashed = p.scoped = p.defusing = p.hasDefuser = true;
        p.money = 4200;
        const float boxLeft = 80, boxWidth = 60, boxTop = 100;
        const ImVec2 screenHead(110, 100);
        const Vector3 renderPlayerPos{100, 100, 100}, renderLocalPos{};
        const ImU32 distanceCol = IM_COL32_WHITE, nameCol = IM_COL32_WHITE;
        const auto GetEspNameFont = [] { return ImGui::GetFont(); };
        const auto ColorToImU32 = [](const float* c) {
            return ImGui::ColorConvertFloat4ToU32(ImVec4(c[0], c[1], c[2], c[3]));
        };
        const auto DrawTextShadow = [](ImDrawList* dl, ImFont* font, float size, ImVec2 pos, ImU32 color, const char* text) {
            dl->AddText(font ? font : ImGui::GetFont(), size > 0 ? size : ImGui::GetFontSize(), pos, color, text);
        };
#include "../../../src/Features/ESP/Render/player_name.inl"
#include "../../../src/Features/ESP/Render/player_flags.inl"
    }

    ImFont* LoadUiIconFont(ImGuiIO& io)
    {
        const HMODULE module = GetModuleHandleW(nullptr);
        const HRSRC resource =
            module ? FindResourceW(module, ui::icons::kFontResourceName, RT_RCDATA) : nullptr;
        const DWORD resourceSize = resource ? SizeofResource(module, resource) : 0;
        const HGLOBAL loaded = resource ? LoadResource(module, resource) : nullptr;
        void* const data = loaded ? LockResource(loaded) : nullptr;
        if (!data ||
            resourceSize == 0 ||
            resourceSize > static_cast<DWORD>(std::numeric_limits<int>::max())) {
            return nullptr;
        }

        ImFontConfig config = {};
        config.FontDataOwnedByAtlas = false;
        config.OversampleH = 2;
        config.OversampleV = 2;
        config.PixelSnapH = true;
        return io.Fonts->AddFontFromMemoryTTF(
            data,
            static_cast<int>(resourceSize),
            20.0f,
            &config,
            ui::icons::kGlyphRanges);
    }

    std::filesystem::path FindCjkFont()
    {
        wchar_t windowsDirectory[MAX_PATH] = {};
        const UINT length =
            GetWindowsDirectoryW(windowsDirectory, MAX_PATH);
        if (length == 0 || length >= MAX_PATH)
            return {};

        const std::filesystem::path fonts =
            std::filesystem::path(windowsDirectory) / L"Fonts";
        constexpr const wchar_t* candidates[] = {
            L"msyh.ttc",
            L"msyhl.ttc",
            L"simsun.ttc"
        };
        for (const wchar_t* fileName : candidates) {
            const std::filesystem::path candidate = fonts / fileName;
            if (std::filesystem::exists(candidate))
                return candidate;
        }
        return {};
    }

    void RunLayout(float dpiScale, float width, float height)
    {
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = ImVec2(width, height);
        io.DeltaTime = 1.0f / 60.0f;

        const std::filesystem::path uiFont =
            std::filesystem::path("C:\\Windows\\Fonts\\segoeui.ttf");
        CHECK(std::filesystem::exists(uiFont));
        if (std::filesystem::exists(uiFont)) {
            g::fontDefault = io.Fonts->AddFontFromFileTTF(
                uiFont.string().c_str(),
                16.0f * dpiScale);
        } else {
            ImFontConfig fontConfig = {};
            fontConfig.SizePixels = 16.0f * dpiScale;
            g::fontDefault = io.Fonts->AddFontDefault(&fontConfig);
        }

        ImVector<ImWchar> cjkGlyphRanges;
        ImFontGlyphRangesBuilder cjkGlyphBuilder;
        const std::string catalogGlyphText =
            app::localization::CollectCatalogGlyphText();
        cjkGlyphBuilder.AddText(catalogGlyphText.c_str());
        cjkGlyphBuilder.BuildRanges(&cjkGlyphRanges);
        const std::filesystem::path cjkFont = FindCjkFont();
        CHECK(!cjkFont.empty());
        if (!cjkFont.empty()) {
            ImFontConfig cjkConfig = {};
            cjkConfig.MergeMode = true;
            cjkConfig.DstFont = g::fontDefault;
            cjkConfig.OversampleH = 1;
            cjkConfig.OversampleV = 1;
            cjkConfig.PixelSnapH = true;
            io.Fonts->AddFontFromFileTTF(
                cjkFont.string().c_str(),
                16.0f * dpiScale,
                &cjkConfig,
                cjkGlyphRanges.Data);
        }

        g::fontUiSemibold = g::fontDefault;
        g::fontUiTitle = g::fontDefault;
        g::fontUiIcons = LoadUiIconFont(io);
        io.FontDefault = g::fontDefault;
        CHECK(g::fontUiIcons != nullptr);
        unsigned char* pixels = nullptr;
        int atlasWidth = 0;
        int atlasHeight = 0;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &atlasWidth, &atlasHeight);
        CHECK(pixels != nullptr);
        CHECK(atlasWidth > 0);
        CHECK(atlasHeight > 0);
        io.Fonts->SetTexID(static_cast<ImTextureID>(1));
        CHECK(g::fontDefault->IsGlyphInFont(0x8BBE));
        CHECK(g::fontDefault->IsGlyphInFont(0x7F6E));
        if (g::fontUiIcons) {
            for (const ui::icons::Icon icon : ui::icons::kAllIcons)
                CHECK(g::fontUiIcons->IsGlyphInFont(ui::icons::Codepoint(icon)));
        }

        ImGui::StyleColorsDark();
        ImGui::GetStyle().ScaleAllSizes(dpiScale);

        const bool savedEspBox = g::espBox;
        const bool savedEspHealth = g::espHealth;
        const bool savedEspWorld = g::espWorld;

        const bool savedTargetEnabled = g::targetEnabled;
        const auto renderAndValidateFrame = [&](int page) {
            ++g_framesTested;
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(width, height), ImGuiCond_Always);
            ImGui::Begin(
                "UI smoke test",
                nullptr,
                ImGuiWindowFlags_NoSavedSettings |
                    ImGuiWindowFlags_NoMove |
                    ImGuiWindowFlags_NoResize |
                    ImGuiWindowFlags_NoCollapse);

            StatusSink status;
            if (page == 3) {
                ui::RenderEspPreview();
            } else if (page == 4) {
                auto* drawList = ImGui::GetWindowDrawList();
                const int before = drawList->VtxBuffer.Size;
                DrawRuntimeFlagsFixture();
                if (g::espFlags) CHECK(drawList->VtxBuffer.Size > before);
                else CHECK(drawList->VtxBuffer.Size == before);
            } else if (page == 1) {
                ui::MenuState menuState;
                ui::tabs::WorldTab worldTab;
                worldTab.Render(menuState, status);
            } else if (page == 2) {
                ui::MenuState menuState;
                ui::tabs::TargetTab targetTab;
                g::targetEnabled = true;
                targetTab.Render(menuState, status);
            } else {
                ui::tabs::esp_sections::RenderCoreSection();
                ui::tabs::esp_sections::RenderOptionsGrid();
                ImGui::Separator();
                ui::tabs::radar_sections::RenderDisplaySection();
                ui::tabs::radar_sections::RenderColorsSection();
                ui::tabs::radar_sections::RenderCalibrationSection(status);
            }

            ImGui::End();
            ImGui::Render();

            ImDrawData* drawData = ImGui::GetDrawData();
            CHECK(drawData != nullptr);
            CHECK(drawData && drawData->CmdLists.Size > 0);
            CHECK(drawData && drawData->TotalVtxCount > 0);
            CHECK(drawData && drawData->TotalIdxCount > 0);
            if (drawData) {
                ImVec2 minVertex(
                    std::numeric_limits<float>::max(),
                    std::numeric_limits<float>::max());
                ImVec2 maxVertex(
                    std::numeric_limits<float>::lowest(),
                    std::numeric_limits<float>::lowest());
                const float tolerance = 2.0f * dpiScale;
                for (int listIndex = 0; listIndex < drawData->CmdLists.Size; ++listIndex) {
                    const ImDrawList* list = drawData->CmdLists[listIndex];
                    for (const ImDrawVert& vertex : list->VtxBuffer) {
                        CHECK(std::isfinite(vertex.pos.x));
                        CHECK(std::isfinite(vertex.pos.y));
                        if (!std::isfinite(vertex.pos.x) || !std::isfinite(vertex.pos.y))
                            continue;
                        minVertex.x = std::min(minVertex.x, vertex.pos.x);
                        minVertex.y = std::min(minVertex.y, vertex.pos.y);
                        maxVertex.x = std::max(maxVertex.x, vertex.pos.x);
                        maxVertex.y = std::max(maxVertex.y, vertex.pos.y);
                    }
                    for (const ImDrawCmd& command : list->CmdBuffer) {
                        CHECK(std::isfinite(command.ClipRect.x));
                        CHECK(std::isfinite(command.ClipRect.y));
                        CHECK(std::isfinite(command.ClipRect.z));
                        CHECK(std::isfinite(command.ClipRect.w));
                        CHECK(command.ClipRect.x >= -tolerance);
                        CHECK(command.ClipRect.y >= -tolerance);
                        CHECK(command.ClipRect.z <= width + tolerance);
                        CHECK(command.ClipRect.w <= height + tolerance);
                    }
                }

                CHECK(minVertex.x >= -tolerance);
                CHECK(minVertex.y >= -tolerance);
                CHECK(maxVertex.x <= width + tolerance);
            }
        };

        for (int frame = 0; frame < 5; ++frame) {
            if (frame == 1 || frame == 3) {
                g::espBox = !g::espBox;
                g::espHealth = !g::espHealth;
                g::espWorld = !g::espWorld;
            }
            io.MousePos =
                (frame % 2 == 0)
                    ? ImVec2(-10000.0f, -10000.0f)
                    : ImVec2(32.0f * dpiScale, 48.0f * dpiScale);
            renderAndValidateFrame(frame == 2 || frame == 4 ? 1 : frame == 3 ? 2 : 0);
        }

        const bool savedFovPerWeapon = g::targetFovPerWeapon;
        const bool savedAimAssist = g::targetTriggerAimAssist;
        const auto savedWeaponProfiles = g::targetWeaponProfiles;
        for (const char* settingsId : {"target_fov", "target_aimbot", "target_triggerbot"}) {
            ui::tabs::testing::OpenTargetSettings(settingsId);
            const int modes = std::strcmp(settingsId, "target_triggerbot") == 0 ? 4 : 2;
            for (int mode = 0; mode < modes; ++mode) {
                g::targetFovPerWeapon = (mode & 1) != 0;
                g::targetTriggerAimAssist = (mode & 1) != 0;
                for (auto& profile : g::targetWeaponProfiles) {
                    profile.hitchanceEnabled = (mode & 1) != 0;
                    profile.seedWindowEnabled = (mode & 2) != 0;
                }
                // Auto-size settles after the first frame; validate both.
                renderAndValidateFrame(2);
                renderAndValidateFrame(2);
            }
        }
        ui::tabs::testing::OpenTargetSettings("");
        const auto savedEspSettings = g::espSettings;
        g::espEnabled = true;
        for (const char* id : {"box", "skeleton", "health", "teammates", "armor", "flags",
                "vis", "weapon", "bomb", "snap", "arrows"}) {
            ui::tabs::esp_sections::testing::OpenSettings(id);
            for (int style = 0; style < 3; ++style) {
                g::espBoxStyle = style;
                g::espHealthColorMode = style;
                g::espArmorColorMode = style;
                g::espBombTime = style != 0;
                renderAndValidateFrame(0);
                renderAndValidateFrame(0);
            }
        }
        ui::tabs::esp_sections::testing::OpenSettings("");
        g::espPreviewOpen = true;
        for (bool visibility : {false, true}) {
            g::espVisibilityColoring = visibility;
            for (bool enabled : {false, true}) {
                g::espFlags = enabled; g::espWeapon = enabled;
                g::espName = g::espDistance = g::espWeaponAmmo = true;
                g::espFlagBlind = g::espFlagScoped = g::espFlagDefusing = g::espFlagKit = g::espFlagMoney = true;
                g::espFlagBlindSize = 24; g::espFlagScopedSize = 20;
                g::espOffscreenArrows = true; g::espOffscreenSize = 36;
                renderAndValidateFrame(3);
                renderAndValidateFrame(3);
                renderAndValidateFrame(4);
            }
        }
        g::espPreviewOpen = false;
        g::espEnabled = false;
        renderAndValidateFrame(0);
        g::espSettings = savedEspSettings;
        g::targetFovPerWeapon = savedFovPerWeapon;
        g::targetTriggerAimAssist = savedAimAssist;
        g::targetWeaponProfiles = savedWeaponProfiles;
        g::espBox = savedEspBox;
        g::espHealth = savedEspHealth;
        g::espWorld = savedEspWorld;
        g::targetEnabled = savedTargetEnabled;
        ImGui::DestroyContext();
    }
}

int main()
{
    using app::localization::Language;
    const Language originalLanguage = app::localization::GetLanguage();

    constexpr std::array<float, 3> kDpiScales = { 1.0f, 1.25f, 1.5f };
    constexpr std::array<Language, 2> kLanguages = {
        Language::English,
        Language::SimplifiedChinese
    };
    for (const Language language : kLanguages) {
        app::localization::SetLanguage(language, false);
        if (language == Language::English)
        {
            CHECK(std::strcmp(app::localization::Get("Settings"), "Settings") == 0);
            CHECK(std::strcmp(
                app::localization::Get("Grenade map: de_mirage"),
                "Mirage") == 0);
        }
        else {
            CHECK(std::strcmp(
                app::localization::Get("Settings"),
                "\xE8\xAE\xBE\xE7\xBD\xAE") == 0);
            CHECK(std::strcmp(
                app::localization::Get("Jump throw"),
                "\xE8\xB7\xB3\xE6\x8A\x95") == 0);
            CHECK(std::strcmp(
                app::localization::Get("Cycle %.2f"),
                "\xE5\x91\xA8\xE6\x9C\x9F %.2f") == 0);
            CHECK(std::strcmp(
                app::localization::Get("Grenade callout: A Site"),
                "\x41\xE5\x8C\x85\xE7\x82\xB9") == 0);
            CHECK(app::localization::Format(
                "Lineup: {} from {}",
                "\x41\xE5\x8C\x85\xE7\x82\xB9",
                "\x54\xE5\x87\xBA\xE7\x94\x9F\xE7\x82\xB9") ==
                "\xE4\xBB\x8E\x54\xE5\x87\xBA\xE7\x94\x9F\xE7\x82\xB9"
                "\xE6\x8A\x95\xE5\x90\x91\x41\xE5\x8C\x85\xE7\x82\xB9");
        }

        for (const float scale : kDpiScales) {
            RunLayout(scale, 560.0f * scale, 620.0f * scale);
            RunLayout(scale, 760.0f * scale, 620.0f * scale);
            RunLayout(scale, 1040.0f * scale, 760.0f * scale);
        }
    }
    app::localization::SetLanguage(originalLanguage, false);

    if (g_failedChecks != 0) {
        std::cerr << g_failedChecks << " UI smoke check(s) failed.\n";
        return 1;
    }

    std::cout << "UI smoke checks passed: " << g_framesTested << " frames, all ESP/Target settings, preview, runtime flag masters, three ESP styles, accuracy toggles, two languages and 9 viewport/DPI combinations.\n";
    return 0;
}
