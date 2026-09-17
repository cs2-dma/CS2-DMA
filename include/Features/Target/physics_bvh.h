#pragma once

#include "Game/Schema/structs.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace target::physics
{
    enum class BuildState : uint8_t
    {
        Idle,
        Queued,
        Building,
        Ready,
        Failed,
    };

    enum class ShapeKind : uint8_t
    {
        Mesh,
        Hull,
        Other,
    };

    struct SurfaceInfo
    {
        float penetration = 0.0f;
        uint16_t surfaceType = 0;
        uint8_t globalIndex = 0xFF;
        ShapeKind kind = ShapeKind::Other;
    };

    struct TraceResult
    {
        bool hit = false;
        float fraction = 1.0f;
        float distance = 0.0f;
        Vector3 endPosition = {};
        Vector3 normal = {};
        SurfaceInfo surface = {};
        int triangleIndex = -1;
    };

    struct TraceHit
    {
        float distance = 0.0f;
        float fraction = 0.0f;
        Vector3 position = {};
        Vector3 normal = {};
        SurfaceInfo surface = {};
        int triangleIndex = -1;
        bool entering = true;
    };

    struct PenetrationSegment
    {
        float enterDistance = 0.0f;
        float exitDistance = 0.0f;
        Vector3 enterPosition = {};
        Vector3 exitPosition = {};
        SurfaceInfo enterSurface = {};
        SurfaceInfo exitSurface = {};
        float thickness = 0.0f;
        float minimumPenetrationModifier = 0.0f;
    };

    struct Stats
    {
        BuildState state = BuildState::Idle;
        uint64_t generation = 0;
        uint64_t buildTimeUs = 0;
        uint64_t lastReadyAgeUs = 0;
        size_t triangles = 0;
        size_t nodes = 0;
        size_t bytesRead = 0;
        char mapKey[64] = {};
        char detail[160] = {};
    };

    void RequestForMap(const char* mapKey);
    bool TraceRay(
        const char* mapKey,
        const Vector3& start,
        const Vector3& end,
        TraceResult* result = nullptr);
    bool IsLineVisible(
        const char* mapKey,
        const Vector3& start,
        const Vector3& end,
        float endpointTolerance = 1.5f);
    bool TracePenetrationSegments(
        const char* mapKey,
        const Vector3& start,
        const Vector3& end,
        std::vector<PenetrationSegment>& segments);
    Stats GetStats();
    void Shutdown();
}
