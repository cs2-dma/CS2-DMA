#pragma once

#include "Game/Schema/structs.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

namespace esp::render {
    struct DistanceReadout {
        uintptr_t pawn=0;
        uint32_t handle=0;
        uint64_t scene=0;
        int meters=-1;
        int Update(uintptr_t nextPawn, uint32_t nextHandle, uint64_t epoch, float distance) {
            if (!std::isfinite(distance) || distance<0 || distance>10000) return -1;
            if (pawn!=nextPawn || handle!=nextHandle || scene!=epoch || meters<0 ||
                std::fabs(distance-static_cast<float>(meters))>0.75f)
                meters=static_cast<int>(std::round(distance));
            pawn=nextPawn; handle=nextHandle; scene=epoch;
            return meters;
        }
    };
    inline std::string FormatAmmo(int clip, int reserve, bool unitsKnown, bool asMagazines, uint16_t weapon) {
        if (clip < 0 || clip > 1000) return {};
        char text[64]{};
        if (!unitsKnown || reserve < 0 || reserve > 10000) std::snprintf(text,sizeof(text),"%d",clip);
        else {
            const bool shells=weapon==25 || weapon==27 || weapon==29 || weapon==35;
            const char* unit=asMagazines ? "mags" : shells ? "shells" : "rounds";
            std::snprintf(text,sizeof(text),"%d | %d %s",clip,reserve,unit);
        }
        return text;
    }
    inline int ValueMode(int mode, bool legacy) {
        return mode < 0 ? (legacy ? 1 : 2) : std::clamp(mode, 0, 2);
    }

    inline bool ShowBarValue(int mode, int value, bool legacy) {
        return value >= 0 && (ValueMode(mode, legacy) == 0 ||
            (ValueMode(mode, legacy) == 1 && value != 100));
    }

    inline int VisibilityState(bool visible, uint64_t sample, uint64_t now) {
        return sample && now >= sample && now - sample <= 150000u ? (visible ? 1 : 0) : 2;
    }

    struct HealthTrail {
        uint64_t identity = 0;
        uint64_t scene = 0;
        uint64_t lastUs = 0;
        uint64_t changedUs = 0;
        int value = 0;
        float peak = 0;

        float Update(uint64_t key, uint64_t epoch, int health, uint64_t now) {
            if (key == 0 || key != identity || epoch != scene || now < lastUs ||
                now - lastUs > 500000u || health <= 0 || health > value) {
                identity = key; scene = epoch; value = health;
                peak = static_cast<float>(health); changedUs = now;
            } else if (health < value) {
                peak = std::max(peak, static_cast<float>(value));
                value = health; changedUs = now;
            }
            const float dt = static_cast<float>(now - std::min(now, lastUs)) / 1000000.0f;
            if (now >= changedUs && now - changedUs > 90000u)
                peak = std::max(static_cast<float>(health), peak - 220.0f * std::min(dt, 0.05f));
            lastUs = now;
            return std::clamp(peak / 100.0f, 0.0f, 1.0f);
        }
    };

    inline bool ClipProjectedSegment(const Vector3& a, const Vector3& b,
        const view_matrix_t& matrix, float width, float height, ScreenPos& pa, ScreenPos& pb) {
        if (!IsFiniteVec(a) || !IsFiniteVec(b) || !std::isfinite(width) || !std::isfinite(height) ||
            width <= 1 || height <= 1) return false;
        const auto clip = [&](const Vector3& p) {
            return std::array<double, 3>{
                matrix[0][0]*p.x + matrix[0][1]*p.y + matrix[0][2]*p.z + matrix[0][3],
                matrix[1][0]*p.x + matrix[1][1]*p.y + matrix[1][2]*p.z + matrix[1][3],
                matrix[3][0]*p.x + matrix[3][1]*p.y + matrix[3][2]*p.z + matrix[3][3]};
        };
        const auto ca = clip(a), cb = clip(b);
        for (int n = 0; n < 3; ++n) if (!std::isfinite(ca[n]) || !std::isfinite(cb[n])) return false;
        const std::array<double, 5> da = {ca[2] - 0.001, ca[2]+ca[0], ca[2]-ca[0], ca[2]+ca[1], ca[2]-ca[1]};
        const std::array<double, 5> db = {cb[2] - 0.001, cb[2]+cb[0], cb[2]-cb[0], cb[2]+cb[1], cb[2]-cb[1]};
        double lo = 0, hi = 1;
        for (int n = 0; n < 5; ++n) {
            if (da[n] < 0 && db[n] < 0) return false;
            if ((da[n] < 0) != (db[n] < 0)) {
                const double t = da[n] / (da[n] - db[n]);
                if (da[n] < 0) lo = std::max(lo, t); else hi = std::min(hi, t);
            }
        }
        if (lo > hi) return false;
        const auto project = [&](double t) {
            const double w = ca[2] + (cb[2]-ca[2])*t;
            return ScreenPos{static_cast<float>((1 + (ca[0]+(cb[0]-ca[0])*t)/w)*width*0.5),
                static_cast<float>((1 - (ca[1]+(cb[1]-ca[1])*t)/w)*height*0.5), true};
        };
        pa = project(lo); pb = project(hi);
        return std::isfinite(pa.x) && std::isfinite(pa.y) && std::isfinite(pb.x) && std::isfinite(pb.y);
    }

    inline Vector3 PoseRenderOffset(const Vector3& anchor, bool valid,
        const Vector3& core, const Vector3& render, uint64_t poseUs, uint64_t coreUs) {
        if (!valid || !IsFiniteVec(anchor) || !poseUs || !coreUs) return render - core;
        const uint64_t delta = poseUs > coreUs ? poseUs-coreUs : coreUs-poseUs;
        const Vector3 shift = render - anchor;
        return delta <= 150000u && shift.Length() < 128.0f ? shift : render - core;
    }

    inline std::array<int, 8> NormalizeFlagOrder(std::array<int, 8> input) {
        std::array<int, 8> result{};
        std::array<bool, 8> used{};
        int count = 0;
        used[5] = true;
        for (int id : input) if (id >= 0 && id < 8 && !used[id]) { result[count++] = id; used[id] = true; }
        for (int id = 0; id < 8; ++id) if (!used[id]) result[count++] = id;
        result[7] = 5;
        return result;
    }
}
