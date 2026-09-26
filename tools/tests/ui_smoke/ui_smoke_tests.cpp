#include "Features/ESP/Render/player_labels.h"
#include "Features/ESP/Render/skeleton_projection.h"
#include <imgui.h>
#include <imgui_internal.h>

#include "app/Localization/localization.h"
#include "app/UI/MenuShell/ui_icons.h"
#include "Features/ESP/UI/esp_sections.h"
#include "Features/ESP/UI/esp_tab.h"
#include "Features/ESP/esp.h"
#include "Features/ESP/Render/weapon_icon_atlas.h"
#include "Features/ESP/Render/bar_labels.h"
#include "Features/ESP/Render/draw_policy.h"
#include "Features/ESP/Render/visual_style_policy.h"
#include "Features/Radar/UI/radar_sections.h"
#include "Features/Target/UI/target_tab.h"
#include "Features/Target/physics_bvh.h"
#include "Features/Target/target.h"
#include "Features/World/UI/world_tab.h"
#include "Features/World/grenade_helper.h"
#include "app/Core/globals.h"
#include "app/Input/input_device.h"
#include "app/Input/primary_keyboard.h"
#include "app/UI/MenuShell/menu_state.h"
#include "app/UI/MenuShell/tab_page.h"
#include "app/UI/MenuShell/version_notice.h"

#include <Windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <backends/imgui_impl_dx11.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <string>

#include "../../../src/Features/ESP/Helpers/draw_corner_box.inl"
#include "../../../src/app/UI/MenuShell/ui_style.cpp"

ui::MenuController::MenuController() = default;
void ui::MenuController::Render() {}
void ui::MenuController::SetStatus(const std::string&) {}

namespace ui::tabs::testing { void OpenTargetSettings(const char* id); }
namespace ui::tabs::testing { void OpenTargetDiagnostics(bool open); }
namespace ui::tabs::testing { void OpenTargetTuning(bool open); }
namespace ui::tabs::testing { void OpenTargetPage(int page); }
namespace ui::tabs::testing { void FollowTargetWeapon(bool follow); }
namespace ui::tabs::testing { bool TargetTooltipRow(const char* id, ImVec4& rectangle, bool& disabled); }
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

app::input::KeyState app::input::ReadActivationKeyState(int) { return {}; }
app::input::KeyState app::input::ReadPrimaryKeyState(int) { return {}; }

bool app::input::IsControlKeyDown(int)
{
    return false;
}

app::input::PrimaryKeyboardStatus app::input::GetPrimaryKeyboardStatus()
{
    return {};
}

bool targetMenuPausedFixture = false;
bool targetGeometryReadyFixture = true;
bool targetWeaponAvailableFixture = true;

target::RuntimeStatus target::GetRuntimeStatus()
{
    RuntimeStatus status;
    status.phase = RuntimePhase::WaitingForKey;
    status.weaponId = targetWeaponAvailableFixture ? 7 : 0;
    status.weaponProfile = targetWeaponAvailableFixture ? 1 : -1;
    status.pausedByMenu = targetMenuPausedFixture;
    status.aimSelection = {10, 1, 2, 3, 4, 0};
    status.triggerSelection = {10, 1, 2, 3, 0, 4};
    status.shot = {1, 100, 35, true, 2};
    status.fire.reason = target::FireBlockReason::Hitchance;
    status.fire.hitchancePercent = 78.1f;
    status.fire.centeredHitchancePercent = 79.0f;
    status.fire.requiredHitchancePercent = 80.0f;
    status.fire.requiredDamage = 30;
    status.geometry = {true, targetGeometryReadyFixture, !targetGeometryReadyFixture,
        !targetGeometryReadyFixture, !targetGeometryReadyFixture, targetGeometryReadyFixture, targetGeometryReadyFixture};
    return status;
}

target::physics::Stats target::physics::GetStats()
{
    Stats stats;
    stats.state = targetGeometryReadyFixture ? BuildState::Ready : BuildState::Building;
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

    void TestHeldGrenadeFilter()
    {
        using namespace world::grenade_helper;
        for (uint16_t item = 0; item < 100; ++item) {
            CHECK(MatchesHeldGrenade(GrenadeType::Flash, item) == (item == 43));
            CHECK(MatchesHeldGrenade(GrenadeType::He, item) == (item == 44));
            CHECK(MatchesHeldGrenade(GrenadeType::Smoke, item) == (item == 45));
            CHECK(MatchesHeldGrenade(GrenadeType::Molotov, item) == (item == 46 || item == 48));
        }
        CHECK(FreshHeldItem(43, true, 1, 0x10000, 100000, 100001) == 43);
        CHECK(FreshHeldItem(43, false, 1, 0x10000, 100000, 100001) == 0);
        CHECK(FreshHeldItem(43, true, 0, 0x10000, 100000, 100001) == 0);
        CHECK(FreshHeldItem(43, true, 0xFFFFFFFFu, 0x10000, 100000, 100001) == 0);
        CHECK(FreshHeldItem(43, true, 1, 0, 100000, 100001) == 0);
        CHECK(FreshHeldItem(43, true, 1, 0x10000, 0, 100001) == 0);
        CHECK(FreshHeldItem(43, true, 1, 0x10000, 100000, 99999) == 0);
        CHECK(FreshHeldItem(43, true, 1, 0x10000, 100000, 200001) == 0);
    }

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
        auto* drawList=ImGui::GetWindowDrawList();
        esp::PlayerData p;
        std::snprintf(p.name,sizeof(p.name),"%s","Test player");
        p.flashed=p.scoped=p.defusing=p.hasDefuser=true;
        p.money=4200; p.moneyKnown=true; p.team=3; p.flashUpdatedUs=1000000;
        p.flashDuration=1.0f; p.scopedUpdatedUs=p.defusingUpdatedUs=1000000;
        esp::render::BarLayout layout{100,220,80,140};
        esp::render::DrawPlayerName(*drawList,ImGui::GetFont(),p,ImVec2(80,100),ImVec2(140,220),
            ImVec2(0,0),ImGui::GetIO().DisplaySize,g::espSettings,layout);
        esp::render::DrawPlayerFlags(*drawList,ImGui::GetFont(),g::fontUiIcons,p,Vector3(100,100,100),
            Vector3(),5,1000000,ImVec2(80,100),ImVec2(0,0),ImGui::GetIO().DisplaySize,g::espSettings,layout);
    }

    void TestFlagExpiryRendering()
    {
        auto* draw = ImGui::GetBackgroundDrawList();
        app::state::EspSettings settings{};
        settings.flags=true; settings.distance=false;
        settings.flagBlind=true; settings.flagScoped=false; settings.flagDefusing=false;
        settings.flagKit=false; settings.flagMoney=false;
        settings.presentation.flagReload=false; settings.presentation.flagBomb=false;
        esp::PlayerData p;
        p.flashed=true; p.flashDuration=0.015625f; p.flashUpdatedUs=1000000;
        const esp::render::BarLayout layout{100,220,80,140};
        const auto visibleAt = [&](uint64_t time) {
            const int before=draw->VtxBuffer.Size;
            esp::render::DrawPlayerFlags(*draw,ImGui::GetFont(),g::fontUiIcons,p,{}, {},-1,time,
                ImVec2(80,100),ImVec2(0,0),ImGui::GetIO().DisplaySize,settings,layout);
            return draw->VtxBuffer.Size>before;
        };
        CHECK(visibleAt(1000000));
        CHECK(visibleAt(1015624));
        CHECK(!visibleAt(1015625));
        p.flashDuration=5.0f;
        CHECK(!visibleAt(1050001));
        settings.flagBlind=false; settings.flagScoped=true;
        p.scoped=true; p.scopedUpdatedUs=1000000;
        CHECK(visibleAt(1000000));
        CHECK(!visibleAt(1050001));
        settings.flagScoped=false; settings.flagDefusing=true;
        p.defusing=true; p.defusingUpdatedUs=1000000;
        CHECK(visibleAt(1000000));
        p.defusing=false;
        CHECK(!visibleAt(1000001));
    }

    void TestPlayerFlagReadCommitRender()
    {
        using namespace esp::data;
        auto* draw = ImGui::GetBackgroundDrawList();
        app::state::EspSettings settings{};
        settings.flags = true;
        settings.distance = settings.flagBlind = settings.flagScoped = settings.flagDefusing = false;
        settings.flagKit = settings.flagMoney = false;
        settings.presentation.flagReload = settings.presentation.flagBomb = false;
        esp::PlayerData p;
        p.valid = true;
        p.pawn = 0x10000;
        p.health = 100;
        PlayerFlagFilterState scope, defuse;
        BlindFlashState flash;
        UpdatePlayerFlagFilter(scope, true, IsBinaryPlayerFlagReadComplete(true, 1, 1), true, 1000000);
        UpdatePlayerFlagFilter(defuse, true, IsBinaryPlayerFlagReadComplete(true, 1, 1), true, 1000000);
        const bool flashRead = IsBlindFlashReadComplete(true, true, sizeof(float), sizeof(float), 105, 5, 100);
        CHECK(flashRead);
        flash.Update(true, flashRead ? EvaluateBlindFlashSample(105, 5, 100) : BlindFlashSample{}, 1000000, 1002000);
        CommitPlayerFlags(p, scope, defuse, flash, 1004000);
        CHECK(p.scoped && p.defusing && p.flashed);
        CHECK(p.flashUpdatedUs == 1000000 && p.flashDuration == 5.0f);
        const esp::render::BarLayout layout{100, 220, 80, 140};
        const auto rendered = [&](uint64_t time) {
            const int before = draw->VtxBuffer.Size;
            esp::render::DrawPlayerFlags(*draw, ImGui::GetFont(), g::fontUiIcons, p, {}, {}, -1, time,
                ImVec2(80, 100), ImVec2(), ImGui::GetIO().DisplaySize, settings, layout);
            return draw->VtxBuffer.Size > before;
        };
        for (int flag = 0; flag < 3; ++flag) {
            settings.flagScoped = flag == 0;
            settings.flagDefusing = flag == 1;
            settings.flagBlind = flag == 2;
            CHECK(rendered(1008000));
        }
        settings.flagBlind = false;
        settings.flagScoped = true;
        UpdatePlayerFlagFilter(scope, true, true, false, 1009000);
        CommitPlayerFlags(p, scope, defuse, flash, 1010000);
        CHECK(!rendered(1010000));
        CHECK(p.flashed && p.defusing);
        UpdatePlayerFlagFilter(scope, true, true, true, 1011000);
        UpdatePlayerFlagFilter(scope, true, false, false, 1015000);
        flash.Update(true, {}, 0, 1015000);
        CommitPlayerFlags(p, scope, defuse, flash, 1016000);
        CHECK(rendered(1016000));
        CHECK(p.scopedUpdatedUs == 1011000 && p.flashUpdatedUs == 1000000);
        settings.flagBlind = true;
        settings.flagScoped = false;
        CHECK(rendered(1016000));
        CHECK(!rendered(1050001));
        flash.Update(true, EvaluateBlindFlashSample(105, 5, 105), 1020000, 1020000);
        CommitPlayerFlags(p, scope, defuse, flash, 1021000);
        CHECK(!rendered(1021000));
        CHECK(p.scoped && p.defusing);
        p.health = 0;
        CommitPlayerFlags(p, scope, defuse, flash, 1022000);
        CHECK(!p.scoped && !p.defusing && !p.flashed);
        p.health = 100;
        p.pawn = 0x20000;
        CommitPlayerFlags(p, {}, {}, {}, 1023000);
        CHECK(!p.scoped && !p.defusing && !p.flashed);
    }

void TestHeadContour()
    {
        auto* draw=ImGui::GetBackgroundDrawList();
        esp::PlayerData p;
        const auto display=ImGui::GetIO().DisplaySize;
        view_matrix_t matrix{};
        matrix[0][0]=matrix[1][1]=0.01f; matrix[3][3]=1;
        int before=draw->VtxBuffer.Size;
        esp::render::DrawHeadContour(*draw,p,{},Vector3(),1000000,matrix,
            display.x,display.y,1,1,IM_COL32_WHITE);
        CHECK(draw->VtxBuffer.Size>before);
        p.hasHitboxes=true; p.hitboxCount=1; p.hitboxesUpdatedAtUs=p.bonesUpdatedAtUs=1000000;
        p.hitboxes[0].valid=true; p.hitboxes[0].hitgroup=1;
        p.hitboxes[0].start={0,-2,0}; p.hitboxes[0].end={0,2,0}; p.hitboxes[0].radius=4;
        before=draw->VtxBuffer.Size;
        esp::render::DrawHeadContour(*draw,p,{},Vector3(),1000000,matrix,
            display.x,display.y,1,1,IM_COL32_WHITE);
        CHECK(draw->VtxBuffer.Size>before);
        for(int i=before;i<draw->VtxBuffer.Size;++i) {
            CHECK(std::isfinite(draw->VtxBuffer[i].pos.x));
            CHECK(std::isfinite(draw->VtxBuffer[i].pos.y));
            CHECK(std::fabs(draw->VtxBuffer[i].pos.x-display.x*0.5f)<display.x*0.05f+3);
        }
        p.hitboxes[0].start={100000,100000,100000};
        before=draw->VtxBuffer.Size;
        esp::render::DrawHeadContour(*draw,p,{},Vector3(),1000000,matrix,
            display.x,display.y,1,1,IM_COL32_WHITE);
        CHECK(draw->VtxBuffer.Size>before);
        for(int i=before;i<draw->VtxBuffer.Size;++i)
            CHECK(std::fabs(draw->VtxBuffer[i].pos.x-display.x*0.5f)<display.x*0.04f+3);
    }

    void TestBarValueLayout()
    {
        using namespace esp::render;
        CHECK(g::fontBarValues != nullptr);
        CHECK(g::fontBarValues->LegacySize == kBarValueFontSize);
        for (int i = 0; i < 10; ++i)
            CHECK(g::fontBarValues->IsGlyphInFont(static_cast<ImWchar>('0' + i)));
        CHECK(!MakeBarValueLabel(nullptr, 74, IM_COL32_WHITE).active);
        CHECK(!MakeBarValueLabel(g::fontBarValues, -1, IM_COL32_WHITE).active);
        ImDrawList draw(ImGui::GetDrawListSharedData());
        auto settings = g::espSettings;
        settings.health = settings.armor = settings.healthText = settings.armorText = true;
        settings.presentation.healthValueMode = settings.presentation.armorValueMode = 0;
        for (const int hp : {0, 9, 74, 100}) for (const int ap : {0, 9, 45, 100})
        for (const float width : {20.0f, 40.0f, 640.0f, 1920.0f})
        for (const float x : {-20.5f, 0.0f, 22.2f, width * 0.5f, width - 1.0f})
        for (const float y : {0.0f, 15.5f, 125.25f, 1079.5f})
        for (int side = 0; side < 4; ++side) {
            draw._ResetForNewFrame();
            draw.PushClipRect(ImVec2(0,0), ImVec2(width,1080));
            draw.PushTexture(g::fontBarValues->OwnerAtlas->TexRef);
            settings.presentation.healthSide = side;
            settings.presentation.armorSide = (side + ap % 3) % 4;
            std::array<BarValueLabel,2> labels;
            const auto layout = DrawPlayerBars(draw, g::fontBarValues, g::fontUiIcons,
                ImVec2(x,y), ImVec2(x+25,y+60), ImVec2(0,0), ImVec2(width,1080),
                hp, ap, 0.8f, settings, &labels);
            CHECK(std::isfinite(layout.top) && std::isfinite(layout.left));
            CHECK(!BarLabelsOverlap(labels[0], labels[1]));
            for (const auto& label : labels) if (label.active) {
                CHECK(label.bgMin.x >= 2 && label.bgMax.x <= width-2);
                CHECK(label.bgMin.y >= 2 && label.bgMax.y <= 1078);
            }
        }
    }

    void DrawRuntimeBarsFixture(float x,float y,float height,int hp,int ap)
    {
        auto* drawList=ImGui::GetBackgroundDrawList();
        esp::PlayerData p;
        p.health=hp; p.armor=ap;
        std::snprintf(p.name,sizeof(p.name),"%s","Player");
        const ImVec2 lo(x,y),hi(x+height*0.5f,y+height);
        const int before=drawList->VtxBuffer.Size;
        auto layout=esp::render::DrawPlayerBars(*drawList,g::fontBarValues,g::fontUiIcons,
            lo,hi,ImVec2(0,0),ImGui::GetIO().DisplaySize,hp,ap,
            std::min(1.0f,hp/100.0f+0.15f),g::espSettings);
        CHECK(std::isfinite(layout.top) && std::isfinite(layout.bottom));
        CHECK(layout.left<=lo.x && layout.right>=hi.x);
        if(g::espHealth || g::espArmor) CHECK(drawList->VtxBuffer.Size>before);
        esp::render::DrawPlayerName(*drawList,ImGui::GetFont(),p,lo,hi,
            ImVec2(0,0),ImGui::GetIO().DisplaySize,g::espSettings,layout);
        const int verticesBeforeBox=drawList->VtxBuffer.Size;
        DrawStyledBox(drawList,x,y,height*0.5f,height,IM_COL32_WHITE,IM_COL32_BLACK,0,25,1);
        CHECK(drawList->VtxBuffer.Size>verticesBeforeBox);
    }

    bool SaveBarPreview(const std::filesystem::path& path, const std::function<void()>& renderFrame = {})
    {
        using Microsoft::WRL::ComPtr;
        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;
        if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
            D3D11_SDK_VERSION, &device, nullptr, &context)))
            return false;
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = static_cast<UINT>(ImGui::GetIO().DisplaySize.x);
        desc.Height = static_cast<UINT>(ImGui::GetIO().DisplaySize.y);
        desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET;
        ComPtr<ID3D11Texture2D> texture;
        ComPtr<ID3D11RenderTargetView> view;
        if (FAILED(device->CreateTexture2D(&desc, nullptr, &texture)) ||
            FAILED(device->CreateRenderTargetView(texture.Get(), nullptr, &view)))
            return false;
        desc.BindFlags = 0;
        desc.Usage = D3D11_USAGE_STAGING;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> staging;
        if (FAILED(device->CreateTexture2D(&desc, nullptr, &staging)))
            return false;
        if (!ImGui_ImplDX11_Init(device.Get(), context.Get()))
            return false;
        const bool objectsReady = ImGui_ImplDX11_CreateDeviceObjects();
        if (objectsReady) {
            const float black[4] = {0, 0, 0, 1};
            if (renderFrame) {
                for (int frame=0;frame<2;++frame) {
                    ImGui_ImplDX11_NewFrame();
                    ImGui::NewFrame();
                    renderFrame();
                    ImGui::Render();
                    context->ClearRenderTargetView(view.Get(),black);
                    context->OMSetRenderTargets(1,view.GetAddressOf(),nullptr);
                    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
                }
            } else {
                auto* atlasTexture = ImGui::GetIO().Fonts->TexData;
                atlasTexture->SetTexID(ImTextureID_Invalid);
                atlasTexture->SetStatus(ImTextureStatus_WantCreate);
                ImGui_ImplDX11_UpdateTexture(atlasTexture);
                context->ClearRenderTargetView(view.Get(),black);
                context->OMSetRenderTargets(1,view.GetAddressOf(),nullptr);
                ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
            }
        }
        ImGui_ImplDX11_Shutdown();
        if (!objectsReady)
            return false;
        context->CopyResource(staging.Get(), texture.Get());
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        if (FAILED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
            return false;
        BITMAPFILEHEADER file = {};
        BITMAPINFOHEADER info = {};
        file.bfType = 0x4D42;
        file.bfOffBits = sizeof(file) + sizeof(info);
        file.bfSize = file.bfOffBits + desc.Width * desc.Height * 4;
        info.biSize = sizeof(info);
        info.biWidth = static_cast<LONG>(desc.Width);
        info.biHeight = -static_cast<LONG>(desc.Height);
        info.biPlanes = 1;
        info.biBitCount = 32;
        std::ofstream output(path, std::ios::binary);
        output.write(reinterpret_cast<const char*>(&file), sizeof(file));
        output.write(reinterpret_cast<const char*>(&info), sizeof(info));
        for (UINT y = 0; y < desc.Height; ++y)
            output.write(static_cast<const char*>(mapped.pData) + y * mapped.RowPitch, desc.Width * 4);
        const bool saved = output.good();
        context->Unmap(staging.Get(), 0);
        return saved;
    }

    ImFont* LoadUiIconFont(ImGuiIO& io);

    void RunFlagSettingsPreview(const std::filesystem::path& path)
    {
        ImGui::CreateContext();
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = ImVec2(1280, 720);
        g::fontDefault = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 16.0f);
        g::fontUiSemibold = g::fontUiTitle = g::fontDefault;
        g::fontUiIcons = LoadUiIconFont(io);
        io.FontDefault = g::fontDefault;
        ui::ApplyStyle();
        g::espFlags = true;
        ui::tabs::esp_sections::testing::OpenSettings("flags");
        CHECK(SaveBarPreview(path, [] {
            ImGui::SetNextWindowPos(ImVec2(40, 540));
            ImGui::SetNextWindowSize(ImVec2(1200, 150));
            ImGui::Begin("ESP", nullptr, ImGuiWindowFlags_NoSavedSettings);
            ui::tabs::esp_sections::RenderOptionsGrid();
            ImGui::End();
        }));
        ImGui::DestroyContext();
        g::fontState = {};
    }

    void RunBarPreview(const std::filesystem::path& path)
    {
        ImGui::CreateContext();
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = ImVec2(1920, 1080);
        io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 16.0f);
        g::fontBarValues = esp::render::LoadBarValueFont(*io.Fonts, "C:\\Windows\\Fonts\\segoeuib.ttf");
        g::fontUiIcons=LoadUiIconFont(io);
        g::espHealth = g::espHealthText = g::espArmor = g::espArmorText = true;
        g::espFlags = g::espName = true;
        const auto renderFrame = [&] {
            g::espSettings.presentation={};
            TestBarValueLayout();
            DrawRuntimeBarsFixture(80.5f, 90.25f, 120, 74, 45);
            DrawRuntimeBarsFixture(210.5f, 90.25f, 90, 9, 99);
            DrawRuntimeBarsFixture(330.5f, 90.25f, 60, 99, 1);
            DrawRuntimeBarsFixture(460.5f, 90.25f, 40, 74, 100);
            DrawRuntimeBarsFixture(580.5f, 90.25f, 80, 100, 45);
            DrawRuntimeBarsFixture(2.5f, 310.25f, 60, 74, 45);
            DrawRuntimeBarsFixture(1840.5f, 90.25f, 80, 74, 45);
            auto& options=g::espSettings.presentation;
            options.healthValueMode=options.armorValueMode=0;
            for(int side=0;side<4;++side) {
                options.healthSide=side;
                options.armorSide=(side+1)%4;
                options.healthWidth=3; options.armorWidth=3;
                DrawRuntimeBarsFixture(170.0f+side*360.0f,420,140,74,45);
                options.armorStyle=1;
                DrawRuntimeBarsFixture(170.0f+side*360.0f,760,90,99,100);
                options.armorStyle=0;
            }
        };
        CHECK(SaveBarPreview(path,renderFrame));
        ImGui::DestroyContext();
        g::fontState = {};
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

    void RunTargetPreview(const std::filesystem::path& path, int page, bool trigger)
    {
        ImGui::CreateContext();
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = ImVec2(1024, 720);
        g::fontDefault = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 16.0f);
        g::fontUiSemibold = g::fontUiTitle = g::fontDefault;
        g::fontUiIcons = LoadUiIconFont(io);
        io.FontDefault = g::fontDefault;
        io.Fonts->Build();
        io.Fonts->SetTexID(static_cast<ImTextureID>(1));
        ui::ApplyStyle();
        g::targetEnabled = g::targetAimbotEnabled = g::targetTriggerbotEnabled = true;
        g::targetAimBone = g::targetTriggerAimBone = 5;
        g::targetAimRecoilControl = true;
        g::targetWeaponProfiles[1].aimMotionStyle = 2;
        g::targetWeaponProfiles[1].aimSoftAssist = true;
        ui::tabs::testing::OpenTargetSettings(trigger ? "target_triggerbot" : "target_aimbot");
        ui::tabs::testing::OpenTargetPage(std::clamp(page, 0, 2));
        ui::tabs::testing::FollowTargetWeapon(true);
        ui::tabs::testing::OpenTargetTuning(false);
        ui::tabs::testing::OpenTargetDiagnostics(false);
        for (int frame = 0; frame < 3; ++frame) {
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(90, 120));
            ImGui::SetNextWindowSize(ImVec2(844, 480));
            ImGui::Begin("Target", nullptr, ImGuiWindowFlags_NoSavedSettings);
            StatusSink status;
            ui::MenuState state;
            ui::tabs::TargetTab{}.Render(state, status);
            ImGui::End();
            ImGui::Render();
        }
        CHECK(SaveBarPreview(path));
        ImGui::DestroyContext();
        g::fontState = {};
    }

    std::filesystem::path FindCjkFont()
    {
        wchar_t configured[MAX_PATH] = {};
        const DWORD configuredLength = GetEnvironmentVariableW(L"KEVQ_TEST_CJK_FONT", configured, MAX_PATH);
        if (configuredLength > 0 && configuredLength < MAX_PATH && std::filesystem::is_regular_file(configured))
            return configured;
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

    void RunTargetControlRegression()
    {
        const auto savedTarget = g::targetSettings;
        const auto savedLanguage = app::localization::GetLanguage();
        app::localization::SetLanguage(app::localization::Language::English, false);
        ImGui::CreateContext();
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = ImVec2(1920, 1080);
        io.DeltaTime = 1.0f / 60.0f;
        io.Fonts->AddFontDefault();
        io.Fonts->Build();
        io.Fonts->SetTexID(static_cast<ImTextureID>(1));
        g::targetEnabled = true;
        targetWeaponAvailableFixture = false;
        ui::tabs::testing::FollowTargetWeapon(true);
        ui::tabs::testing::OpenTargetDiagnostics(false);
        ui::tabs::testing::OpenTargetSettings("target_aimbot");
        ui::tabs::testing::OpenTargetPage(2);
        for (auto& profile : g::targetWeaponProfiles)
            profile.aimDamageCheck = false;
        const auto frame = [&] {
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_Always);
            ImGui::Begin("Target interaction regression");
            ui::MenuState state;
            StatusSink sink;
            ui::tabs::TargetTab{}.Render(state, sink);
            ImGui::End();
            ImGui::Render();
            ++g_framesTested;
        };
        const auto row = [&](const char* id, bool expectedDisabled) {
            ImVec4 rectangle{};
            bool disabled = !expectedDisabled;
            CHECK(ui::tabs::testing::TargetTooltipRow(id, rectangle, disabled));
            CHECK(disabled == expectedDisabled);
            CHECK(rectangle.z > rectangle.x && rectangle.w > rectangle.y);
            return rectangle;
        };
        const auto click = [&](const ImVec4& rectangle) {
            io.AddMousePosEvent(rectangle.z - 33.0f, (rectangle.y + rectangle.w) * 0.5f);
            frame();
            io.AddMouseButtonEvent(0, true);
            frame();
            io.AddMouseButtonEvent(0, false);
            frame();
        };
        const auto hasTooltip = [&] {
            for (const auto* window : ImGui::GetCurrentContext()->Windows)
                if (window->Active && (window->Flags & ImGuiWindowFlags_Tooltip) != 0)
                    return true;
            return false;
        };
        frame();
        frame();
        const ImVec4 visibleRow = row("aim_visible", false);
        const bool previousVisible = g::targetAimVisibleOnly;
        click(visibleRow);
        CHECK(g::targetAimVisibleOnly != previousVisible);
        click(row("aim_damage_check", true));
        CHECK(!g::targetWeaponProfiles[1].aimDamageCheck);
        io.AddMousePosEvent(visibleRow.x + 24.0f, (visibleRow.y + visibleRow.w) * 0.5f);
        for (int index = 0; index < 40; ++index)
            frame();
        CHECK(hasTooltip());
        io.AddMousePosEvent(0.0f, 0.0f);
        frame();
        CHECK(!hasTooltip());
        ui::tabs::testing::FollowTargetWeapon(false);
        frame();
        click(row("aim_damage_check", false));
        CHECK(g::targetWeaponProfiles[1].aimDamageCheck);
        ui::tabs::testing::FollowTargetWeapon(true);
        ui::tabs::testing::OpenTargetSettings("target_triggerbot");
        ui::tabs::testing::OpenTargetPage(0);
        frame();
        frame();
        const bool previousRepeat = g::targetTriggerAutoShot;
        click(row("trigger_auto_shot", false));
        CHECK(g::targetTriggerAutoShot != previousRepeat);
        row("trigger_force_center", true);
        ui::tabs::testing::OpenTargetSettings("");
        ui::tabs::testing::FollowTargetWeapon(false);
        targetWeaponAvailableFixture = true;
        ImGui::DestroyContext();
        g::fontState = {};
        g::targetSettings = savedTarget;
        app::localization::SetLanguage(savedLanguage, false);
    }

    void RunSmallMenuRegression()
    {
        const auto savedEsp = g::espSettings;
        const bool savedTarget = g::targetEnabled;
        g::espEnabled = g::targetEnabled = true;
        g::espPreviewOpen = false;
        for (const float scale : {1.0f, 1.25f, 1.5f}) {
            ImGui::CreateContext();
            auto& io = ImGui::GetIO();
            io.IniFilename = nullptr;
            io.DisplaySize = ImVec2(1920, 1080);
            io.ConfigErrorRecoveryEnableAssert = false;
            io.ConfigErrorRecoveryEnableDebugLog = false;
            io.ConfigErrorRecoveryEnableTooltip = false;
            ImGui::GetCurrentContext()->ErrorCallback = [](ImGuiContext*, void*, const char* error) {
                std::cerr << "ImGui regression: " << error << '\n';
                CHECK(false);
            };
            io.Fonts->AddFontDefault();
            io.Fonts->Build();
            io.Fonts->SetTexID(static_cast<ImTextureID>(1));
            ImGui::GetStyle().ScaleAllSizes(scale);
            ImGui::GetStyle().WindowMinSize = ImVec2(1, 1);
            for (const ImVec2 size : {ImVec2(900, 700), ImVec2(320, 120), ImVec2(140, 70),
                    ImVec2(40, 25), ImVec2(8, 8), ImVec2(560, 620), ImVec2(1040, 760)}) {
                for (int page = 0; page < 2; ++page) {
                    for (int frame = 0; frame < 3; ++frame) {
                        ImGui::NewFrame();
                        ImGui::SetNextWindowSize(size, ImGuiCond_Always);
                        ImGui::Begin("Small menu");
                        ui::MenuState state;
                        StatusSink sink;
                        if (page == 0) {
                            ui::tabs::EspTab tab;
                            tab.Render(state, sink);
                        } else {
                            ui::tabs::testing::OpenTargetDiagnostics(frame != 0);
                            ui::tabs::TargetTab tab;
                            tab.Render(state, sink);
                        }
                        ImGui::End();
                        ImGui::Render();
                        ++g_framesTested;
                        CHECK(ImGui::GetCurrentContext()->CurrentWindowStack.empty());
                    }
                }
            }
            ImGui::DestroyContext();
            g::fontState = {};
        }
        g::espSettings = savedEsp;
        g::targetEnabled = savedTarget;
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
        g::fontBarValues = esp::render::LoadBarValueFont(*io.Fonts, "C:\\Windows\\Fonts\\segoeuib.ttf");
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

        ui::ApplyStyle();
        ImGui::GetStyle().ScaleAllSizes(dpiScale);
        ImGui::NewFrame();
        TestBarValueLayout();
        TestHeadContour();
        for (const int style : {0, 1, 2}) {
            auto* draw = ImGui::GetBackgroundDrawList();
            for (const float thickness : {0.5f, 1.0f, 1.8f, 4.0f}) {
                const int before = draw->VtxBuffer.Size;
                DrawStyledBox(draw, 20, 20, 40, 80, IM_COL32_WHITE, IM_COL32_BLACK,
                    style, 25, thickness);
                CHECK(draw->VtxBuffer.Size > before);
            }
        }
        ImGui::Render();
        ++g_framesTested;

        const bool savedEspBox = g::espBox;
        const bool savedEspHealth = g::espHealth;
        const bool savedEspWorld = g::espWorld;

        const bool savedTargetEnabled = g::targetEnabled;
        app::updates::Status releaseStatus;
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
            if (page == 5) {
                ui::RenderVersionNotice(releaseStatus, false);
                ui::RenderVersionNotice(releaseStatus, true);
            } else if (page == 3) {
                ui::RenderEspPreview();
            } else if (page == 4) {
                auto* drawList = ImGui::GetWindowDrawList();
            const int before = drawList->VtxBuffer.Size;
                DrawRuntimeFlagsFixture();
                if (g::espFlags || g::espName || g::espDistance) CHECK(drawList->VtxBuffer.Size > before);
                else CHECK(drawList->VtxBuffer.Size == before);
                TestFlagExpiryRendering();
                TestPlayerFlagReadCommitRender();
            } else if (page == 1) {
                ui::MenuState menuState;
                ui::tabs::WorldTab worldTab;
                worldTab.Render(menuState, status);
            } else if (page == 2) {
                ui::tabs::testing::OpenTargetDiagnostics(true);
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
            const int modes = std::strcmp(settingsId, "target_triggerbot") == 0 ? 4 : 3;
            for (int mode = 0; mode < modes; ++mode) {
                ui::tabs::testing::OpenTargetTuning((mode & 1) != 0);
                g::targetAimBone = (mode & 1) != 0 ? 5 : 0;
                g::targetTriggerAimBone = (mode & 1) != 0 ? 5 : 0;
                targetMenuPausedFixture = (mode & 1) != 0;
                targetGeometryReadyFixture = (mode & 1) != 0;
                targetWeaponAvailableFixture = mode != 0;
                ui::tabs::testing::FollowTargetWeapon((mode & 1) == 0);
                g::targetFovPerWeapon = (mode & 1) != 0;
                g::targetTriggerAimAssist = (mode & 1) != 0;
                for (auto& profile : g::targetWeaponProfiles) {
                    profile.hitchanceEnabled = (mode & 1) != 0;
                    profile.seedWindowEnabled = (mode & 2) != 0;
                    profile.aimAdaptiveSmoothing = (mode & 1) != 0;
                    profile.aimWindMouse = (mode & 1) != 0;
                    profile.aimMotionStyle = mode % 3;
                    profile.triggerForceCenter = (mode & 1) != 0;
                    profile.aimSoftAssist = (mode & 1) != 0;
                    profile.aimDamageCheck = (mode & 1) != 0;
                    profile.triggerAdaptiveSmoothing = (mode & 2) != 0;
                }
                const int pages = std::strcmp(settingsId, "target_fov") == 0 ? 1 : 3;
                for (int page = 0; page < pages; ++page) {
                    ui::tabs::testing::OpenTargetPage(page);
                    renderAndValidateFrame(2);
                    renderAndValidateFrame(2);
                }
            }
        }
        ui::tabs::testing::OpenTargetSettings("");
        targetMenuPausedFixture = false;
        targetGeometryReadyFixture = true;
        targetWeaponAvailableFixture = true;
        ui::tabs::testing::FollowTargetWeapon(false);
        for (int state = 0; state <= static_cast<int>(app::updates::State::InvalidRelease); ++state) {
            releaseStatus.state = static_cast<app::updates::State>(state);
            releaseStatus.updateAvailable = state == static_cast<int>(app::updates::State::Available);
            releaseStatus.latestTag = "v1.10.123";
            releaseStatus.releaseUrl = "https://github.com/cs2-dma/CS2-DMA/releases/tag/v1.10.123";
            releaseStatus.httpStatus = 200;
            releaseStatus.canCheck = false;
            releaseStatus.checking = false;
            renderAndValidateFrame(5);
            releaseStatus.checking = true;
            renderAndValidateFrame(5);
        }
        const auto savedEspSettings = g::espSettings;
        g::espEnabled = true;
        for (const char* id : {"box", "skeleton", "health", "teammates", "armor", "flags",
                "vis", "weapon", "bomb", "snap", "arrows"}) {
            ui::tabs::esp_sections::testing::OpenSettings(id);
            for (int style = 0; style < 3; ++style) {
                auto& options=g::espSettings.presentation;
                options.healthSide=style; options.armorSide=(style+1)%4;
                options.healthWidth=1.0f+style*2.0f; options.armorWidth=8.0f;
                options.healthValueMode=style; options.armorValueMode=(style+1)%3;
                options.armorStyle=style%2; options.nameSide=style;
                options.flagsStyle=style; options.flagsSide=style%2; options.flagsLimit=style+1;
                options.visibilityBox=style!=0; options.visibilitySkeleton=style==0;
                options.visibilityArrows=style==1;
                options.snapOrigin=style; options.snapEndpoint=style%2;
                options.snapOpacity=0.5f; options.snapThickness=4.0f;
                options.snapNearest=style==2;
                g::espBoxStyle = style;
                g::espHealthColorMode = style;
                g::espArmorColorMode = style;
                g::espBombTime = style != 0;
                g::espSkeletonHeadCircle = style != 0;
                g::espSkeletonHeadScale = style == 2 ? 2.0f : 0.5f;
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
                g::espSkeletonHeadCircle = enabled;
                renderAndValidateFrame(3);
                renderAndValidateFrame(3);
                renderAndValidateFrame(4);
            }
        }
        for(int side=0;side<4;++side) {
            auto& options=g::espSettings.presentation;
            options.healthSide=side; options.armorSide=side;
            options.nameSide=side; options.armorStyle=side%2;
            options.healthValueMode=options.armorValueMode=0;
            options.healthWidth=options.armorWidth=8;
            g::espHealth=g::espArmor=g::espName=g::espFlags=true;
            renderAndValidateFrame(3);
            renderAndValidateFrame(4);
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
        g::fontState = {};
    }
}

int main(int argc, char** argv)
{
    if (argc == 3 && std::strcmp(argv[1], "--flags-preview") == 0) {
        RunFlagSettingsPreview(argv[2]);
        return g_failedChecks == 0 ? 0 : 1;
    }
    if (argc == 5 && std::strcmp(argv[1], "--target-preview") == 0) {
        RunTargetPreview(argv[2], std::stoi(argv[3]), std::strcmp(argv[4], "trigger") == 0);
        return g_failedChecks == 0 ? 0 : 1;
    }
    if (argc == 3 && std::strcmp(argv[1], "--bar-preview") == 0) {
        RunBarPreview(argv[2]);
        return g_failedChecks == 0 ? 0 : 1;
    }
    using app::localization::Language;
    RunTargetControlRegression();
    RunSmallMenuRegression();
    TestHeldGrenadeFilter();
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
