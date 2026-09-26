#pragma once

#include "Features/ESP/esp.h"
#include "Features/ESP/DataReader/player_flag_policy.h"
#include "Features/ESP/Render/player_bars.h"
#include "app/Localization/localization.h"
#include <cstdio>
#include <limits>

namespace esp::render {
    inline ImU32 LabelColor(const float* c) {
        return ImGui::ColorConvertFloat4ToU32(ImVec4(c[0],c[1],c[2],c[3]));
    }

    inline void LabelText(ImDrawList& draw, ImFont* font, float size, ImVec2 p, ImU32 color, const char* text) {
        p.x=std::round(p.x); p.y=std::round(p.y);
        draw.AddText(font,size,ImVec2(p.x+1,p.y+1),IM_COL32_BLACK,text);
        draw.AddText(font,size,p,color,text);
    }

    inline std::string FitName(ImFont* font, float size, const char* text, float width) {
        if (font->CalcTextSizeA(size,FLT_MAX,0,text).x <= width) return text;
        const float ellipsis = font->CalcTextSizeA(size,FLT_MAX,0,"...").x;
        const char* end = text;
        font->CalcTextSizeA(size,std::max(1.0f,width-ellipsis),0,text,nullptr,&end);
        return std::string(text,end)+"...";
    }

    inline void DrawPlayerName(ImDrawList& draw, ImFont* font, const PlayerData& p,
        ImVec2 boxMin, ImVec2 boxMax, ImVec2 clipMin, ImVec2 clipMax,
        const app::state::EspSettings& settings, BarLayout& layout) {
        if (!settings.name || !p.name[0] || !font) return;
        const auto& options = settings.presentation;
        const float fs = settings.nameFontSize > 4 ? std::clamp(settings.nameFontSize,5.0f,24.0f) : ImGui::GetFontSize();
        const float maxWidth = std::isfinite(options.nameMaxWidth) ? std::clamp(options.nameMaxWidth,50.0f,300.0f) : 150;
        const std::string text = FitName(font,fs,p.name,std::min(maxWidth,std::max(1.0f,clipMax.x-clipMin.x-4)));
        const ImVec2 size = font->CalcTextSizeA(fs,FLT_MAX,0,text.c_str());
        if (size.x+4 > clipMax.x-clipMin.x || size.y+4 > clipMax.y-clipMin.y) return;
        const int side = std::clamp(options.nameSide,0,3);
        ImVec2 pos((boxMin.x+boxMax.x-size.x)*0.5f,layout.top-size.y-4);
        if (side == 1) pos.y=layout.bottom+3;
        if (side == 2) pos=ImVec2(layout.left-size.x-4,boxMin.y);
        if (side == 3) pos=ImVec2(layout.right+4,boxMin.y);
        pos.x=std::clamp(pos.x,clipMin.x+2,clipMax.x-size.x-2);
        pos.y=std::clamp(pos.y,clipMin.y+2,clipMax.y-size.y-2);
        LabelText(draw,font,fs,pos,LabelColor(settings.nameColor),text.c_str());
        if (side == 0) layout.top=std::min(layout.top,pos.y);
        if (side == 1) layout.bottom=std::max(layout.bottom,pos.y+size.y);
        if (side == 2) layout.left=std::min(layout.left,pos.x);
        if (side == 3) layout.right=std::max(layout.right,pos.x+size.x);
    }

    inline void DrawPlayerFlags(ImDrawList& draw, ImFont* font, ImFont* icons,
        const PlayerData& p, const Vector3& playerPosition, const Vector3& localPosition,
        float defuseLeft, uint64_t nowUs, ImVec2 boxMin, ImVec2 clipMin, ImVec2 clipMax,
        const app::state::EspSettings& settings, const BarLayout& layout,
        float defuseMargin=std::numeric_limits<float>::quiet_NaN(), int distanceOverride=-1) {
        if ((!settings.flags && !settings.distance) || !font) return;
        const auto& options=settings.presentation;
        struct Entry { bool active=false; ui::icons::Icon icon=ui::icons::Icon::Flag; ImU32 color=IM_COL32_WHITE;
            float size=0; bool numeric=false; char text[48]{}; };
        std::array<Entry,9> entries{};
        const auto add=[&](int id, bool active, ui::icons::Icon icon, const float* color, float size,
            const char* text, bool numeric=false) {
            auto& entry=entries[id]; entry.active=active; entry.icon=icon;
            entry.color=LabelColor(color); entry.size=size>4 ? std::clamp(size,5.0f,24.0f) : ImGui::GetFontSize();
            entry.numeric=numeric; std::snprintf(entry.text,sizeof(entry.text),"%s",app::localization::Get(text));
        };
        using I=ui::icons::Icon;
        const bool reloadFresh = p.weaponPresentationUpdatedUs && nowUs >= p.weaponPresentationUpdatedUs &&
            nowUs-p.weaponPresentationUpdatedUs <= 250000u;
        add(0,settings.flagDefusing && p.defusing && esp::data::IsPlayerFlagFresh(p.defusingUpdatedUs,nowUs),
            I::HourglassSplit,settings.flagDefusingColor,settings.flagDefusingSize,"Defusing");
        if (defuseLeft>0 && defuseLeft<30) { std::snprintf(entries[0].text,48,"Defuse %.1fs",defuseLeft); entries[0].numeric=true; }
        if (defuseLeft>0 && defuseLeft<30 && std::isfinite(defuseMargin) && std::fabs(defuseMargin)<120)
            std::snprintf(entries[0].text,48,"Defuse %.1fs (%+.1fs)",defuseLeft,defuseMargin);
        add(1,options.flagReload && reloadFresh && p.isReloading,I::ArrowRepeat,settings.flagColor,0,"Reloading");
        const bool flashFresh=esp::data::RemainingBlindSeconds(p.flashDuration,p.flashUpdatedUs,nowUs)>0.0f;
        add(2,settings.flagBlind && flashFresh && p.flashed,I::EyeSlash,settings.flagBlindColor,settings.flagBlindSize,"Blind");
        add(3,settings.flagScoped && p.scoped && esp::data::IsPlayerFlagFresh(p.scopedUpdatedUs,nowUs),
            I::Crosshair,settings.flagScopedColor,settings.flagScopedSize,"Scoped");
        add(4,options.flagBomb && p.hasBomb,I::BoxSeam,settings.bombColor,0,"C4");
        add(6,settings.flagKit && p.hasDefuser && p.team==3,I::Tools,settings.flagKitColor,settings.flagKitSize,"Kit");
        add(7,settings.flagMoney && p.moneyKnown,I::CashStack,settings.flagMoneyColor,settings.flagMoneySize,"",true);
        if (options.compactMoney && p.money>=1000) std::snprintf(entries[7].text,48,"$%.1fk",p.money/1000.0);
        else std::snprintf(entries[7].text,48,"$%d",p.money);
        const float meters=(playerPosition-localPosition).Length()/39.37f;
        add(8,settings.distance && std::isfinite(meters) && meters<10000,I::Rulers,settings.distanceColor,settings.distanceSize,"",true);
        const char* elevation=options.distanceHeight && std::fabs(playerPosition.z-localPosition.z)>80 ?
            (playerPosition.z>localPosition.z ? " +" : " -") : "";
        std::snprintf(entries[8].text,48,"%.0fm%s",distanceOverride>=0 ? static_cast<double>(distanceOverride) : meters,elevation);
        float y=std::max(boxMin.y,clipMin.y+2);
        int count=0, hidden=0;
        const int style=std::clamp(options.flagsStyle,0,2);
        const auto drawEntry=[&](const Entry& e) {
            const bool showText=style!=1 || e.numeric || !icons;
            const bool showIcon=style!=0 && icons;
            const ImVec2 textSize=showText ? font->CalcTextSizeA(e.size,FLT_MAX,0,e.text) : ImVec2();
            const float h=std::max(textSize.y,e.size), iconWidth=showIcon ? e.size+3 : 0;
            const float width=textSize.x+iconWidth;
            if (width+4>clipMax.x-clipMin.x || y+h>clipMax.y-2) return;
            float x=options.flagsSide==0 ? layout.left-width-5 : layout.right+5;
            x=std::clamp(x,clipMin.x+2,clipMax.x-width-2);
            if (showIcon) {
                if (!ui::icons::DrawCentered(&draw,icons,e.icon,ImVec2(x,y),e.size,e.size-1,e.color))
                    draw.AddCircle(ImVec2(x+e.size*0.5f,y+e.size*0.5f),e.size*0.25f,e.color,12,1.2f);
            }
            if (showText) LabelText(draw,font,e.size,ImVec2(x+iconWidth,y),e.color,e.text);
            y+=h+2;
        };
        if (settings.flags) for (int id:NormalizeFlagOrder(options.flagsOrder)) {
            if (!entries[id].active) continue;
            if (count++<std::clamp(options.flagsLimit,1,7)) drawEntry(entries[id]); else ++hidden;
        }
        if (hidden) {
            Entry more; more.size=ImGui::GetFontSize(); more.numeric=true;
            std::snprintf(more.text,sizeof(more.text),"+%d",hidden); drawEntry(more);
        }
        if (entries[8].active) drawEntry(entries[8]);
    }
}
