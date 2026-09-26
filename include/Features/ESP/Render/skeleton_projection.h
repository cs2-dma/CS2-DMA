#pragma once

#include "Features/ESP/esp.h"
#include "Features/ESP/Render/presentation_policy.h"
#include <imgui.h>
#include <vector>
#include <numbers>

namespace esp::render {
    inline void DrawHeadContour(ImDrawList& draw, const PlayerData& player, const Vector3& offset,
        const Vector3& fallbackHead, uint64_t now, const view_matrix_t& matrix,
        float width, float height, float scale, float thickness, ImU32 color) {
        Vector3 ends[2]={fallbackHead,fallbackHead};
        float radius=3.5f;
        const uint64_t poseDelta=player.hitboxesUpdatedAtUs>player.bonesUpdatedAtUs
            ? player.hitboxesUpdatedAtUs-player.bonesUpdatedAtUs : player.bonesUpdatedAtUs-player.hitboxesUpdatedAtUs;
        if (player.hasHitboxes && player.bonesUpdatedAtUs && poseDelta<=50000u &&
            player.hitboxesUpdatedAtUs && now>=player.hitboxesUpdatedAtUs && now-player.hitboxesUpdatedAtUs<=150000u) {
            for (int i=0;i<std::min<int>(player.hitboxCount,kMaximumPlayerHitboxes);++i) {
                const auto& capsule=player.hitboxes[i];
                if (capsule.valid && capsule.hitgroup==1 && IsFiniteVec(capsule.start) &&
                    IsFiniteVec(capsule.end) && capsule.radius>0 && capsule.radius<16 &&
                    ((capsule.start+capsule.end)*0.5f+offset-fallbackHead).Length()<24) {
                    ends[0]=capsule.start+offset; ends[1]=capsule.end+offset; radius=capsule.radius; break;
                }
            }
        }
        radius*=std::isfinite(scale) ? std::clamp(scale,0.5f,2.0f) : 1;
        std::array<ImVec2,96> points{};
        int count=0;
        for (const Vector3& center:ends) for (int axis=0;axis<3;++axis) for (int n=0;n<16;++n) {
            const float angle=n*(2*std::numbers::pi_v<float>/16);
            const float a=std::cos(angle)*radius,b=std::sin(angle)*radius;
            const Vector3 delta=axis==0 ? Vector3{0,a,b} : axis==1 ? Vector3{a,0,b} : Vector3{a,b,0};
            const Vector3 world=center+delta;
            const float w=matrix[3][0]*world.x+matrix[3][1]*world.y+matrix[3][2]*world.z+matrix[3][3];
            if (w<0.01f || !std::isfinite(w)) continue;
            const ScreenPos p=WorldToScreen(world,matrix,width,height);
            if (!std::isfinite(p.x) || !std::isfinite(p.y) || (p.x==0 && p.y==0 && !p.onScreen)) continue;
            points[count++]=ImVec2(p.x,p.y);
        }
        if (count<3) return;
        std::sort(points.begin(),points.begin()+count,[](ImVec2 a,ImVec2 b) { return a.x!=b.x ? a.x<b.x : a.y<b.y; });
        std::array<ImVec2,192> hull{};
        const auto cross=[](ImVec2 a,ImVec2 b,ImVec2 c) { return (b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x); };
        int size=0;
        for (int i=0;i<count;++i) {
            while(size>=2 && cross(hull[size-2],hull[size-1],points[i])<=0) --size;
            hull[size++]=points[i];
        }
        const int lower=size+1;
        for (int i=count-2;i>=0;--i) {
            while(size>=lower && cross(hull[size-2],hull[size-1],points[i])<=0) --size;
            hull[size++]=points[i];
        }
        if (size<4) return;
        draw.AddPolyline(hull.data(),size-1,IM_COL32(0,0,0,180),thickness+1.4f,ImDrawFlags_Closed);
        draw.AddPolyline(hull.data(),size-1,color,thickness,ImDrawFlags_Closed);
    }
}
