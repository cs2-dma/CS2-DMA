#include "app/Core/globals.h"
#include "app/Localization/localization.h"
#include "Features/ESP/esp.h"
#include "Features/ESP/weapon_catalog.h"
#include "Features/ESP/Render/weapon_icon_atlas.h"
#include "Features/ESP/Render/draw_policy.h"
#include "Features/ESP/Render/bar_labels.h"
#include "Features/ESP/Render/player_labels.h"

#include <algorithm>
#include <charconv>
#include <utility>

#include <imgui.h>

#include "../Helpers/draw_corner_box.inl"

namespace ui {
void RenderEspPreview();
}

namespace
{
    ImU32 Col4(const float* c)
    {
        return IM_COL32(
            static_cast<int>(c[0] * 255),
            static_cast<int>(c[1] * 255),
            static_cast<int>(c[2] * 255),
            static_cast<int>(c[3] * 255));
    }

    void TextShadow(ImDrawList* dl, const ImVec2& pos, ImU32 color, const char* text)
    {
        dl->AddText(ImVec2(pos.x + 1, pos.y + 1), IM_COL32(0, 0, 0, 220), text);
        dl->AddText(pos, color, text);
    }

    void TextShadowFont(ImDrawList* dl, ImFont* font, float size,
                        const ImVec2& pos, ImU32 color, const char* text)
    {
        if (!font) { TextShadow(dl, pos, color, text); return; }
        const int a = static_cast<int>((color >> IM_COL32_A_SHIFT) & 0xFFu);
        const int sa = a < 220 ? (220 * a / 255) : 220;
        dl->AddText(font, size, ImVec2(pos.x + 1, pos.y + 1), IM_COL32(0, 0, 0, sa), text);
        dl->AddText(font, size, pos, color, text);
    }

    struct Vec2 { float x, y; };

    Vec2 BonePos2D(int boneId, float cx, float top, float scale)
    {
        float s = scale;
        switch (boneId) {
        case esp::HEAD:           return { cx,          top + 10*s  };
        case esp::NECK:           return { cx,          top + 22*s  };
        case esp::CHEST:          return { cx,          top + 38*s  };
        case esp::SPINE2:         return { cx,          top + 54*s  };
        case esp::SPINE1:         return { cx,          top + 70*s  };
        case esp::PELVIS:         return { cx,          top + 90*s  };
        case esp::SHOULDER_L:     return { cx + 18*s,   top + 26*s  };
        case esp::ELBOW_L:        return { cx + 32*s,   top + 52*s  };
        case esp::HAND_L:         return { cx + 34*s,   top + 70*s  };
        case esp::SHOULDER_R:     return { cx - 18*s,   top + 26*s  };
        case esp::ELBOW_R:        return { cx - 32*s,   top + 52*s  };
        case esp::HAND_R:         return { cx - 34*s,   top + 70*s  };
        case esp::HIP_L:          return { cx + 10*s,   top + 95*s  };
        case esp::KNEE_L:         return { cx + 13*s,   top + 125*s };
        case esp::FOOT_HEEL_L:    return { cx + 14*s,   top + 146*s };
        case esp::FOOT_TOES_L_T:
        case esp::FOOT_TOES_L_CT: return { cx + 16*s,   top + 154*s };
        case esp::HIP_R:          return { cx - 10*s,   top + 95*s  };
        case esp::KNEE_R:         return { cx - 13*s,   top + 125*s };
        case esp::FOOT_HEEL_R:    return { cx - 14*s,   top + 146*s };
        case esp::FOOT_TOES_R_T:
        case esp::FOOT_TOES_R_CT: return { cx - 16*s,   top + 154*s };
        default: return { cx,          top + 50*s  };
        }
    }

    struct BonePair { int from, to; };
    static const BonePair kPairs[] = {
        {esp::PELVIS,     esp::SPINE1},
        {esp::SPINE1,     esp::SPINE2},
        {esp::SPINE2,     esp::CHEST},
        {esp::CHEST,      esp::NECK},
        {esp::NECK,       esp::HEAD},
        {esp::NECK,       esp::SHOULDER_L},
        {esp::SHOULDER_L, esp::ELBOW_L},
        {esp::ELBOW_L,    esp::HAND_L},
        {esp::NECK,       esp::SHOULDER_R},
        {esp::SHOULDER_R, esp::ELBOW_R},
        {esp::ELBOW_R,    esp::HAND_R},
        {esp::PELVIS,     esp::HIP_L},
        {esp::HIP_L,      esp::KNEE_L},
        {esp::KNEE_L,     esp::FOOT_HEEL_L},
        {esp::FOOT_HEEL_L, esp::FOOT_TOES_L_CT},
        {esp::PELVIS,     esp::HIP_R},
        {esp::HIP_R,      esp::KNEE_R},
        {esp::KNEE_R,     esp::FOOT_HEEL_R},
        {esp::FOOT_HEEL_R, esp::FOOT_TOES_R_CT},
    };
    static const int kJoints[] = {
        esp::PELVIS,
        esp::SPINE1,
        esp::SPINE2,
        esp::CHEST,
        esp::NECK,
        esp::HEAD,
        esp::SHOULDER_L,
        esp::ELBOW_L,
        esp::HAND_L,
        esp::SHOULDER_R,
        esp::ELBOW_R,
        esp::HAND_R,
        esp::HIP_L,
        esp::KNEE_L,
        esp::FOOT_HEEL_L,
        esp::FOOT_TOES_L_CT,
        esp::HIP_R,
        esp::KNEE_R,
        esp::FOOT_HEEL_R,
        esp::FOOT_TOES_R_CT
    };

    
    void DrawPlayerSilhouette(ImDrawList* dl, float cx, float boxTop,
                              float boxW, float boxH, float boneScale,
                              ImU32 entityCol, bool showBottomLabels,
                              float areaBottom, bool useVisColors = true)
    {
        const float boxLeft = cx - boxW * 0.5f;
        const int mockHp = 72;
        const int mockArmor = 45;
        
        
        if (g::espSnaplines && showBottomLabels) {
            const auto& options=g::espSettings.presentation;
            const int origin=options.snapOrigin<0 ? (g::espSnaplineFromTop ? 0 : 1) : options.snapOrigin;
            float fromY=origin==0 ? boxTop-30 : origin==1 ? areaBottom : (boxTop+areaBottom)*0.5f;
            const float thickness=std::clamp(options.snapThickness,0.5f,4.0f);
            const float opacity=std::clamp(options.snapOpacity,0.0f,1.0f);
            const ImVec2 to(cx,boxTop+boxH*(options.snapEndpoint==1 ? 0.5f : 1.0f));
            const auto* color=g::espSnaplineColor;
            dl->AddLine(ImVec2(cx,fromY),to,IM_COL32(0,0,0,static_cast<int>(140*opacity)),thickness+1.5f);
            dl->AddLine(ImVec2(cx,fromY),to,ImGui::ColorConvertFloat4ToU32(
                ImVec4(color[0],color[1],color[2],color[3]*opacity)),thickness);
        }

        
        if (g::espBox)
            DrawStyledBox(
                dl,
                boxLeft,
                boxTop,
                boxW,
                boxH,
                g::espSettings.presentation.visibilityBox ? entityCol : Col4(g::espBoxColor),
                IM_COL32(0, 0, 0, 220),
                g::espBoxStyle,
                g::espBoxCornerPercent,
                std::clamp(g::espBoxThickness, 0.5f, 4.0f));

        
        auto layout=esp::render::DrawPlayerBars(*dl,g::fontBarValues ? g::fontBarValues : ImGui::GetFont(),
            g::fontUiIcons,ImVec2(boxLeft,boxTop),ImVec2(boxLeft+boxW,boxTop+boxH),
            dl->GetClipRectMin(),dl->GetClipRectMax(),mockHp,mockArmor,0.85f,g::espSettings);
        esp::PlayerData mock;
        mock.health=mockHp; mock.armor=mockArmor; mock.hasHelmet=true; mock.hasHelmetValid=true;
        mock.helmetUpdatedAtUs=1000000; mock.weaponPresentationUpdatedUs=1000000;
        mock.money=4200; mock.moneyKnown=true; mock.flashed=true; mock.scoped=true; mock.flashUpdatedUs=1000000;
        mock.flashDuration=1.0f; mock.scopedUpdatedUs=1000000; mock.defusingUpdatedUs=1000000;
        mock.defusing=true; mock.hasDefuser=true; mock.hasBomb=true; mock.team=3;
        std::snprintf(mock.name,sizeof(mock.name),"%s","KevQ");
        
        if (g::espSkeleton) {
            ImU32 skelCol = (g::espVisibilityColoring && useVisColors && g::espSettings.presentation.visibilitySkeleton) ? entityCol : Col4(g::espSkeletonColor);
            const float skeletonThickness = std::clamp(g::espSkeletonThickness, 0.5f, 4.0f);
            const float skeletonOutlineThickness = skeletonThickness + 1.4f;
            if (g::espSkeletonHeadCircle) {
                const Vec2 head = BonePos2D(esp::HEAD, cx, boxTop, boneScale);
                const float radius = boxH*(3.5f/72.0f)*std::clamp(g::espSkeletonHeadScale,0.5f,2.0f);
                if (radius > 0.0f) {
                    dl->AddCircle(ImVec2(head.x, head.y), radius, IM_COL32(0, 0, 0, 200), 32, skeletonOutlineThickness);
                    dl->AddCircle(ImVec2(head.x, head.y), radius, skelCol, 32, skeletonThickness);
                }
            }
            for (auto& p : kPairs) {
                Vec2 a = BonePos2D(p.from, cx, boxTop, boneScale);
                Vec2 b = BonePos2D(p.to, cx, boxTop, boneScale);
                dl->AddLine(ImVec2(a.x, a.y), ImVec2(b.x, b.y),
                            IM_COL32(0, 0, 0, 200), skeletonOutlineThickness);
            }
            for (auto& p : kPairs) {
                Vec2 a = BonePos2D(p.from, cx, boxTop, boneScale);
                Vec2 b = BonePos2D(p.to, cx, boxTop, boneScale);
                dl->AddLine(ImVec2(a.x, a.y), ImVec2(b.x, b.y), skelCol, skeletonThickness);
            }
            if (g::espSkeletonDots) {
                for (int j : kJoints) {
                    Vec2 bp = BonePos2D(j, cx, boxTop, boneScale);
                    dl->AddCircleFilled(ImVec2(bp.x, bp.y), 2.2f,
                                        IM_COL32(0, 0, 0, 200), 8);
                    dl->AddCircleFilled(ImVec2(bp.x, bp.y), 1.4f, skelCol, 8);
                }
            }
        }

        
        esp::render::DrawPlayerName(*dl,g::fontEspName ? g::fontEspName : ImGui::GetFont(),
            mock,ImVec2(boxLeft,boxTop),ImVec2(boxLeft+boxW,boxTop+boxH),
            dl->GetClipRectMin(),dl->GetClipRectMax(),g::espSettings,layout);
        
        {
            float bottomY = layout.bottom + 4;
            if (g::espWeapon) {
                
                if (g::espWeaponIcon) {
                    const float iconSize = g::espWeaponIconSize;
                    ImVec2 atlasSize = {};
                    if (esp::render::weapon_icons::CalculateDrawSize(7, iconSize, &atlasSize) &&
                        esp::render::weapon_icons::Draw(
                            dl,
                            7,
                            ImVec2(cx - atlasSize.x * 0.5f, bottomY),
                            iconSize,
                            Col4(g::espWeaponIconColor))) {
                        bottomY += atlasSize.y + 1.0f;
                    } else if (g::fontWeaponIcons) {
                        const char* icon = esp::weapons::WeaponIconFromItemId(7);
                        ImVec2 its = g::fontWeaponIcons->CalcTextSizeA(iconSize, FLT_MAX, 0.0f, icon);
                        TextShadowFont(dl, g::fontWeaponIcons, iconSize,
                                       ImVec2(cx - its.x * 0.5f, bottomY),
                                       Col4(g::espWeaponIconColor), icon);
                        bottomY += its.y + 1.0f;
                    }
                }
                
                if (g::espWeaponText) {
                    const char* weapon = "AK-47";
                    ImFont* tf = g::fontOverlayText ? g::fontOverlayText : ImGui::GetFont();
                    float tfs = g::espWeaponTextSize > 0.0f ? g::espWeaponTextSize : ImGui::GetFontSize();
                    ImVec2 ts = tf->CalcTextSizeA(tfs, FLT_MAX, 0.0f, weapon);
                    TextShadowFont(dl, tf, tfs, ImVec2(cx - ts.x * 0.5f, bottomY),
                                   Col4(g::espWeaponTextColor), weapon);
                    bottomY += ts.y + 2;
                }
            }
            if (g::espWeapon && g::espWeaponAmmo) {
                const char* ammo = "25 | 5 mags";
                ImFont* af = g::fontOverlayText ? g::fontOverlayText : ImGui::GetFont();
                float afs = g::espWeaponAmmoSize > 0.0f ? g::espWeaponAmmoSize : ImGui::GetFontSize();
                ImVec2 ts = af->CalcTextSizeA(afs, FLT_MAX, 0.0f, ammo);
                TextShadowFont(dl, af, afs, ImVec2(cx - ts.x * 0.5f, bottomY),
                               Col4(g::espWeaponAmmoColor), ammo);
            }
        }


        
        esp::render::DrawPlayerFlags(*dl,g::fontOverlayText ? g::fontOverlayText : ImGui::GetFont(),
            g::fontUiIcons,mock,Vector3(1653.5f,0,0),Vector3(),4.5f,1000000,
            ImVec2(boxLeft,boxTop),dl->GetClipRectMin(),dl->GetClipRectMax(),g::espSettings,layout);
    }
}

void ui::RenderEspPreview()
{
    if (!g::espPreviewOpen || !g::espEnabled)
        return;

    const bool triMode = g::espVisibilityColoring;
    const float defaultW = triMode ? 560.0f : 280.0f;

    ImGui::SetNextWindowSize(ImVec2(defaultW, 380), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(
        ImVec2(triMode ? 480.0f : 240.0f, 320),
        ImVec2(triMode ? 780.0f : 400.0f, 520));

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 12.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.025f, 0.035f, 0.052f, 0.98f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.14f, 0.22f, 0.34f, 0.72f));
    ImGui::PushStyleColor(ImGuiCol_TitleBg, ImVec4(0.035f, 0.047f, 0.065f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.045f, 0.065f, 0.095f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_TitleBgCollapsed, ImVec4(0.035f, 0.047f, 0.065f, 1.0f));

    if (!ImGui::Begin(KEVQ_TR("ESP Preview"), &g::espPreviewOpen,
                      ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar)) {
        ImGui::End();
        ImGui::PopStyleColor(5);
        ImGui::PopStyleVar(3);
        return;
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 winPos = ImGui::GetCursorScreenPos();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float pw = avail.x;
    const float ph = avail.y;

    const ImVec2 panelMax(winPos.x + pw, winPos.y + ph);
    dl->AddRectFilled(winPos, panelMax, IM_COL32(5, 11, 20, 248), 10.0f);
    dl->AddRectFilled(
        ImVec2(winPos.x + 1.0f, winPos.y + 1.0f),
        ImVec2(panelMax.x - 1.0f, winPos.y + 34.0f),
        IM_COL32(12, 27, 46, 92),
        9.0f,
        ImDrawFlags_RoundCornersTop);
    dl->AddRect(winPos, panelMax, IM_COL32(43, 77, 124, 125), 10.0f, 0.75f, 0);

    if (triMode) {
        
        const float thirdW = pw / 3.0f;
        const float boxH = ph * 0.36f;
        const float boxW = boxH * 0.48f;
        const float boneScale = boxH / 160.0f;
        const float boxTop = winPos.y + ph * 0.16f;

        
        float div1 = winPos.x + thirdW;
        float div2 = winPos.x + thirdW * 2.0f;
        dl->AddLine(ImVec2(div1, winPos.y + 4), ImVec2(div1, winPos.y + ph - 4),
                    IM_COL32(50, 50, 50, 180), 1.0f);
        dl->AddLine(ImVec2(div2, winPos.y + 4), ImVec2(div2, winPos.y + ph - 4),
                    IM_COL32(50, 50, 50, 180), 1.0f);

        
        float cx1 = winPos.x + thirdW * 0.5f;
        ImU32 defCol = Col4(g::espBoxColor);
        DrawPlayerSilhouette(dl, cx1, boxTop, boxW, boxH, boneScale,
                             defCol, true, winPos.y + ph - 22, false);

        
        float cx2 = winPos.x + thirdW * 1.5f;
        ImU32 visCol = Col4(g::espVisibleColor);
        DrawPlayerSilhouette(dl, cx2, boxTop, boxW, boxH, boneScale,
                             visCol, true, winPos.y + ph - 22);

        
        float cx3 = winPos.x + thirdW * 2.5f;
        ImU32 hidCol = Col4(g::espHiddenColor);
        DrawPlayerSilhouette(dl, cx3, boxTop, boxW, boxH, boneScale,
                             hidCol, true, winPos.y + ph - 22);

        
    const char* defLabel = KEVQ_TR("Default");
    const char* visLabel = KEVQ_TR("Visible");
    const char* hidLabel = KEVQ_TR("Hidden");
        ImVec2 dlSize = ImGui::CalcTextSize(defLabel);
        ImVec2 vlSize = ImGui::CalcTextSize(visLabel);
        ImVec2 hlSize = ImGui::CalcTextSize(hidLabel);
        float labelY = winPos.y + ph - dlSize.y - 6;

        TextShadow(dl, ImVec2(cx1 - dlSize.x * 0.5f, labelY),
                   IM_COL32(180, 180, 180, 255), defLabel);
        TextShadow(dl, ImVec2(cx2 - vlSize.x * 0.5f, labelY), visCol, visLabel);
        TextShadow(dl, ImVec2(cx3 - hlSize.x * 0.5f, labelY), hidCol, hidLabel);
    } else {
        
        const float boxH = ph * 0.44f;
        const float boxW = boxH * 0.48f;
        const float boneScale = boxH / 160.0f;
        const float cx = winPos.x + pw * 0.5f;
        const float boxTop = winPos.y + ph * 0.18f;

        ImU32 entityCol = Col4(g::espBoxColor);

        DrawPlayerSilhouette(dl, cx, boxTop, boxW, boxH, boneScale,
                             entityCol, true, winPos.y + ph);

        
        if (g::espOffscreenArrows) {
            ImU32 arrowCol = Col4(g::espOffscreenColor);
            const float sz = std::clamp(g::espOffscreenSize, 6.0f, 36.0f);
            float ax = winPos.x + sz + 6;
            float ay = winPos.y + ph * 0.5f;
            ImVec2 p1(ax, ay - sz);
            ImVec2 p2(ax + sz * 1.2f, ay);
            ImVec2 p3(ax, ay + sz);
            dl->AddTriangleFilled(p1, p2, p3, arrowCol);
            dl->AddTriangle(p1, p2, p3, IM_COL32(0, 0, 0, 200), 1.5f);
        }
    }

    ImGui::Dummy(avail);
    ImGui::End();
    ImGui::PopStyleColor(5);
    ImGui::PopStyleVar(3);
}
