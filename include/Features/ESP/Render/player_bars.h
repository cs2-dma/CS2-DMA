#pragma once

#include "app/Core/app_state.h"
#include "app/UI/MenuShell/ui_icons.h"
#include "Features/ESP/Render/bar_labels.h"
#include "Features/ESP/Render/visual_style_policy.h"
#include "Features/ESP/Render/presentation_policy.h"
#include <cstdio>

namespace esp::render {
    struct BarLayout {
        float top, bottom, left, right;
    };

    inline BarLayout DrawPlayerBars(ImDrawList& draw, ImFont* font, ImFont* icons,
        ImVec2 boxMin, ImVec2 boxMax, ImVec2 clipMin, ImVec2 clipMax,
        int health, int armor, float trail,
        const app::state::EspSettings& settings,
        std::array<BarValueLabel, 2>* visibleLabels = nullptr) {
        if (visibleLabels) *visibleLabels = {};
        const auto& options = settings.presentation;
        BarLayout layout{boxMin.y, boxMax.y, boxMin.x, boxMax.x};
        std::array<float, 4> offsets = {5,5,5,5};
        std::array<BarValueLabel, 2> labels;
        std::array<ImVec2, 2> origins{};
        std::array<BarValueLabel, 2> occupied{};
        int occupiedCount=0;
        const auto reserve=[&](ImVec2 lo, ImVec2 hi) {
            if (occupiedCount>=static_cast<int>(occupied.size())) return;
            auto& bounds=occupied[occupiedCount++];
            bounds.active=true; bounds.bgMin=lo; bounds.bgMax=hi;
        };
        const auto color = [](BarColor c) {
            return IM_COL32(static_cast<int>(c.r*255), static_cast<int>(c.g*255),
                static_cast<int>(c.b*255), static_cast<int>(c.a*255));
        };
        const auto expand = [&](ImVec2 lo, ImVec2 hi) {
            layout.top = std::min(layout.top, lo.y);
            layout.bottom = std::max(layout.bottom, hi.y);
            layout.left = std::min(layout.left, lo.x);
            layout.right = std::max(layout.right, hi.x);
        };
        for (int index = 0; index < 2; ++index) {
            const bool isArmor = index == 1;
            if (isArmor ? !settings.armor : !settings.health) continue;
            const int value = isArmor ? armor : health;
            const int side = std::clamp(isArmor ? options.armorSide : options.healthSide, 0, 3);
            const bool compact = isArmor && options.armorStyle == 1;
            float width = isArmor ? options.armorWidth : options.healthWidth;
            width = std::isfinite(width) ? std::clamp(width, 1.0f, 8.0f) : 2.0f;
            const float fraction = std::clamp(static_cast<float>(value)/100.0f, 0.0f, 1.0f);
            const int mode = isArmor ? settings.armorColorMode : settings.healthColorMode;
            const float* primary = isArmor ? settings.armorColor : settings.healthColor;
            const float* low = isArmor ? settings.armorLowColor : settings.healthLowColor;
            const ImU32 accent = color(isArmor ? ResolveArmorBarColor(mode, fraction, primary, low)
                : ResolveBarColor(mode, fraction, primary, low));
            ImVec2 lo = boxMin, hi = boxMax;
            if (side == 0) { hi.x = boxMin.x-offsets[0]; lo.x = hi.x-width; }
            if (side == 1) { lo.x = boxMax.x+offsets[1]; hi.x = lo.x+width; }
            if (side == 2) { hi.y = boxMin.y-offsets[2]; lo.y = hi.y-width; }
            if (side == 3) { lo.y = boxMax.y+offsets[3]; hi.y = lo.y+width; }
            if (compact) {
                const bool show = font && ShowBarValue(options.armorValueMode, armor, settings.armorText);
                char number[16]{};
                if (show) std::snprintf(number, sizeof(number), "%d", armor);
                const float textWidth = show ? font->CalcTextSizeA(12, FLT_MAX, 0, number).x : 0;
                const float compactWidth=16+textWidth;
                lo = side == 0 ? ImVec2(boxMin.x-offsets[0]-compactWidth, boxMin.y) :
                    side == 1 ? ImVec2(boxMax.x+offsets[1], boxMin.y) :
                    side == 2 ? ImVec2(boxMin.x, boxMin.y-offsets[2]-18) : ImVec2(boxMin.x, boxMax.y+offsets[3]);
                hi = ImVec2(lo.x+compactWidth, lo.y+16);
                if (!ui::icons::DrawCentered(&draw, icons, ui::icons::Icon::Shield, lo, 15, 13, accent)) {
                    const ImVec2 shield[] = {{lo.x+2,lo.y+2},{lo.x+12,lo.y+2},{lo.x+11,lo.y+10},{lo.x+7,lo.y+14},{lo.x+3,lo.y+10}};
                    draw.AddPolyline(shield,5,accent,1.4f,ImDrawFlags_Closed);
                }
                if (show) {
                    draw.AddText(font, 12, ImVec2(std::round(lo.x+17), std::round(lo.y+2)), IM_COL32_BLACK, number);
                    draw.AddText(font, 12, ImVec2(std::round(lo.x+16), std::round(lo.y+1)), IM_COL32_WHITE, number);
                }
                offsets[side] += side < 2 ? hi.x-lo.x+4 : 20;
                reserve(lo,hi);
                expand(lo, hi);
                continue;
            }
            offsets[side] += width+4;
            expand(ImVec2(lo.x-1,lo.y-1), ImVec2(hi.x+1,hi.y+1));
            draw.AddRectFilled(ImVec2(lo.x-1,lo.y-1), ImVec2(hi.x+1,hi.y+1), IM_COL32(0,0,0,220), 1);
            draw.AddRectFilled(lo, hi, IM_COL32(24,24,24,230));
            const auto fill = [&](float amount, ImU32 start, ImU32 end) {
                if (amount <= 0) return;
                ImVec2 a=lo, b=hi;
                if (side < 2) a.y = hi.y-(hi.y-lo.y)*amount;
                else b.x = lo.x+(hi.x-lo.x)*amount;
                draw.AddRectFilledMultiColor(a,b,start,side < 2 ? start : end,end,side < 2 ? end : start);
            };
            if (!isArmor && options.healthTrail && trail > fraction)
                fill(trail, IM_COL32(245,190,85,170), IM_COL32(245,190,85,170));
            fill(fraction, mode == 2 ? color(ReadBarColor(primary)) : accent,
                mode == 2 ? color(ReadBarColor(low)) : accent);
            if (ShowBarValue(isArmor ? options.armorValueMode : options.healthValueMode,
                value, isArmor ? settings.armorText : settings.healthText)) {
                auto& label = labels[index];
                label = MakeBarValueLabel(font, value, accent);
                const float w = label.bgMax.x, h = label.bgMax.y;
                if (side == 0) origins[index] = ImVec2(isArmor ? lo.x-w-3 : hi.x+3, boxMin.y-h-3);
                if (side == 1) origins[index] = ImVec2(isArmor ? hi.x+3 : lo.x-w-3, boxMin.y-h-3);
                if (side == 2) origins[index] = ImVec2(lo.x, lo.y-h-3);
                if (side == 3) origins[index] = ImVec2(lo.x, hi.y+3);
            }
        }
        for (int index=0; index<2; ++index) {
            auto& label = labels[index];
            if (!label.active) continue;
            const ImVec2 size = label.bgMax;
            if (clipMax.x-clipMin.x < size.x+4 || clipMax.y-clipMin.y < size.y+4) { label.active = false; continue; }
            label.bgMin = ImVec2(std::clamp(std::round(origins[index].x),clipMin.x+2,clipMax.x-size.x-2),
                std::clamp(std::round(origins[index].y),clipMin.y+2,clipMax.y-size.y-2));
            label.bgMax = ImVec2(label.bgMin.x+size.x,label.bgMin.y+size.y);
            for (int pass=0;pass<4;++pass) {
                const BarValueLabel* blocker=nullptr;
                if (index && BarLabelsOverlap(labels[0],label)) blocker=&labels[0];
                for (const auto& bounds:occupied)
                    if (BarLabelsOverlap(bounds,label)) blocker=&bounds;
                if (!blocker) break;
                const float above=blocker->bgMin.y-size.y-2;
                label.bgMin.y=above>=clipMin.y+2 ? above : blocker->bgMax.y+2;
                label.bgMax.y=label.bgMin.y+size.y;
            }
            bool blocked = index && BarLabelsOverlap(labels[0], label);
            for (const auto& bounds : occupied) blocked = blocked || BarLabelsOverlap(bounds, label);
            if (blocked || label.bgMax.y > clipMax.y-2 || label.bgMin.y < clipMin.y+2) {
                label.active = false;
                continue;
            }
            if (visibleLabels) (*visibleLabels)[index] = label;
            expand(label.bgMin,label.bgMax);
            DrawBarValueLabel(draw,font,label);
        }
        return layout;
    }
}
