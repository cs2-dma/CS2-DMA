#include "Features/Target/physics_bvh.h"

#include <Windows.h>
#include <DMALibrary/Memory/Memory.h>

#include "Game/Offsets/runtime_resolver_policy.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <numeric>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

namespace
{
    using target::physics::BuildState;
    using target::physics::ShapeKind;
    using target::physics::Stats;
    using target::physics::SurfaceInfo;
    using target::physics::TraceResult;
    using target::physics::TraceHit;
    using target::physics::PenetrationSegment;

    constexpr size_t kMaximumSectionBytes = 192u * 1024u * 1024u;
    constexpr size_t kMaximumBulkReadBytes = 512u * 1024u * 1024u;
    constexpr size_t kMaximumTriangles = 2'000'000u;
    constexpr int kMaximumBodies = 1'000'000;
    constexpr int kMaximumLeafTriangles = 8;
    constexpr int kMaximumBvhDepth = 48;
    constexpr uint64_t kFailedBuildRetryUs = 15'000'000u;

    uint64_t NowUs()
    {
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
    }

    bool IsGamePointer(uintptr_t value)
    {
        return value >= 0x10000ull && value < 0x000F000000000000ull;
    }

    bool IsFinitePosition(const Vector3& value)
    {
        return IsFiniteVec(value) &&
               std::fabs(value.x) < 10'000'000.0f &&
               std::fabs(value.y) < 10'000'000.0f &&
               std::fabs(value.z) < 10'000'000.0f;
    }

    struct BuildContext
    {
        size_t bytesRead = 0;
        std::function<bool()> canceled;

        bool IsCanceled() const
        {
            return canceled && canceled();
        }

        bool Read(uintptr_t address, void* output, size_t size, bool cached = false)
        {
            if (!IsGamePointer(address) || !output || size == 0 ||
                size > kMaximumBulkReadBytes || IsCanceled()) {
                return false;
            }
            const bool ok = cached
                ? mem.ReadCached(address, output, size)
                : mem.Read(address, output, size);
            if (ok)
                bytesRead += size;
            return ok;
        }

        template <typename T>
        bool Read(uintptr_t address, T& output, bool cached = false)
        {
            return Read(address, &output, sizeof(T), cached);
        }
    };

    struct RemoteSection
    {
        std::string name;
        uintptr_t address = 0;
        uint32_t rva = 0;
        uint32_t characteristics = 0;
        std::vector<uint8_t> bytes;
    };

    struct RemoteModule
    {
        uintptr_t base = 0;
        std::vector<RemoteSection> sections;
    };

    bool ReadSectionBytes(
        BuildContext& context,
        uintptr_t address,
        size_t size,
        std::vector<uint8_t>& output)
    {
        if (size == 0 || size > kMaximumSectionBytes)
            return false;
        output.assign(size, 0);
        constexpr size_t kChunk = 1024u * 1024u;
        size_t offset = 0;
        while (offset < size) {
            if (context.IsCanceled())
                return false;
            const size_t count = (std::min)(kChunk, size - offset);
            if (!context.Read(address + offset, output.data() + offset, count, true))
                return false;
            offset += count;
            std::this_thread::yield();
        }
        return true;
    }

    bool ReadRemoteModule(
        BuildContext& context,
        const char* moduleName,
        RemoteModule& output,
        std::string& error)
    {
        output = {};
        output.base = mem.GetModuleBase(moduleName);
        if (!IsGamePointer(output.base)) {
            error = std::string(moduleName) + " base unavailable";
            return false;
        }

        IMAGE_DOS_HEADER dos = {};
        if (!context.Read(output.base, dos, true) || dos.e_magic != IMAGE_DOS_SIGNATURE ||
            dos.e_lfanew <= 0 || dos.e_lfanew > 0x100000) {
            error = std::string(moduleName) + " DOS header invalid";
            return false;
        }

        IMAGE_NT_HEADERS64 nt = {};
        if (!context.Read(output.base + static_cast<uintptr_t>(dos.e_lfanew), nt, true) ||
            nt.Signature != IMAGE_NT_SIGNATURE || nt.FileHeader.NumberOfSections == 0 ||
            nt.FileHeader.NumberOfSections > 96) {
            error = std::string(moduleName) + " PE header invalid";
            return false;
        }

        const uintptr_t sectionTable = output.base +
            static_cast<uintptr_t>(dos.e_lfanew) +
            offsetof(IMAGE_NT_HEADERS64, OptionalHeader) +
            nt.FileHeader.SizeOfOptionalHeader;
        std::vector<IMAGE_SECTION_HEADER> headers(nt.FileHeader.NumberOfSections);
        if (!context.Read(
                sectionTable,
                headers.data(),
                headers.size() * sizeof(IMAGE_SECTION_HEADER),
                true)) {
            error = std::string(moduleName) + " section table unreadable";
            return false;
        }

        for (const IMAGE_SECTION_HEADER& header : headers) {
            const uint32_t size = (std::max)(
                header.Misc.VirtualSize,
                header.SizeOfRawData);
            if (size == 0 || size > kMaximumSectionBytes ||
                (header.Characteristics & IMAGE_SCN_MEM_READ) == 0) {
                continue;
            }
            RemoteSection section;
            char name[9] = {};
            std::memcpy(name, header.Name, 8);
            section.name = name;
            section.rva = header.VirtualAddress;
            section.address = output.base + header.VirtualAddress;
            section.characteristics = header.Characteristics;
            if (!ReadSectionBytes(context, section.address, size, section.bytes))
                continue;
            output.sections.push_back(std::move(section));
        }
        if (output.sections.empty()) {
            error = std::string(moduleName) + " readable sections unavailable";
            return false;
        }
        return true;
    }

    uintptr_t FindPatternAddress(
        const RemoteModule& module,
        std::string_view patternText)
    {
        const auto pattern =
            runtime_offsets::resolver_policy::CompilePattern(patternText);
        if (pattern.empty())
            return 0;
        for (const RemoteSection& section : module.sections) {
            if ((section.characteristics & IMAGE_SCN_MEM_EXECUTE) == 0)
                continue;
            const auto matches =
                runtime_offsets::resolver_policy::FindPatternMatches(
                    section.bytes,
                    pattern);
            if (matches.matchCount == 1 && matches.firstOffset)
                return section.address + *matches.firstOffset;
        }
        return 0;
    }

    bool CopyModuleBytes(
        const RemoteModule& module,
        uintptr_t address,
        void* output,
        size_t size)
    {
        if (!output || size == 0)
            return false;
        for (const RemoteSection& section : module.sections) {
            if (address < section.address)
                continue;
            const size_t offset = static_cast<size_t>(address - section.address);
            if (offset <= section.bytes.size() &&
                size <= section.bytes.size() - offset) {
                std::memcpy(output, section.bytes.data() + offset, size);
                return true;
            }
        }
        return false;
    }

    uintptr_t ResolveRip(
        const RemoteModule& module,
        uintptr_t instruction,
        size_t displacementOffset = 3,
        size_t instructionLength = 7)
    {
        int32_t displacement = 0;
        if (!CopyModuleBytes(
                module,
                instruction + displacementOffset,
                &displacement,
                sizeof(displacement))) {
            return 0;
        }
        return instruction + instructionLength + displacement;
    }

    uintptr_t FindQword(
        const RemoteModule& module,
        uintptr_t value,
        uint32_t requiredCharacteristics)
    {
        for (const RemoteSection& section : module.sections) {
            if ((section.characteristics & requiredCharacteristics) !=
                requiredCharacteristics) {
                continue;
            }
            for (size_t offset = 0;
                 offset + sizeof(uintptr_t) <= section.bytes.size();
                 offset += sizeof(uintptr_t)) {
                uintptr_t candidate = 0;
                std::memcpy(
                    &candidate,
                    section.bytes.data() + offset,
                    sizeof(candidate));
                if (candidate == value)
                    return section.address + offset;
            }
        }
        return 0;
    }

    uintptr_t FindVtable(
        const RemoteModule& module,
        std::string_view className)
    {
        const std::string descriptor =
            ".?AV" + std::string(className) + "@@";
        uintptr_t typeDescriptor = 0;
        for (const RemoteSection& section : module.sections) {
            if (section.bytes.size() <= descriptor.size())
                continue;
            const auto iterator = std::search(
                section.bytes.begin(),
                section.bytes.end(),
                descriptor.begin(),
                descriptor.end());
            if (iterator == section.bytes.end())
                continue;
            const size_t offset = static_cast<size_t>(
                iterator - section.bytes.begin());
            if (offset < 0x10)
                continue;
            typeDescriptor = section.address + offset - 0x10;
            break;
        }
        if (!typeDescriptor || typeDescriptor < module.base)
            return 0;

        const uint32_t descriptorRva = static_cast<uint32_t>(
            typeDescriptor - module.base);
        uintptr_t completeObjectLocator = 0;
        for (const RemoteSection& section : module.sections) {
            if (section.name.find(".rdata") == std::string::npos)
                continue;
            for (size_t offset = 0;
                 offset + 0x30 <= section.bytes.size();
                 offset += sizeof(uintptr_t)) {
                uint32_t candidateRva = 0;
                std::memcpy(
                    &candidateRva,
                    section.bytes.data() + offset + 12,
                    sizeof(candidateRva));
                if (candidateRva == descriptorRva) {
                    completeObjectLocator = section.address + offset;
                    break;
                }
            }
            if (completeObjectLocator)
                break;
        }
        if (!completeObjectLocator)
            return 0;
        const uintptr_t reference = FindQword(
            module,
            completeObjectLocator,
            IMAGE_SCN_MEM_READ);
        return reference ? reference + sizeof(uintptr_t) : 0;
    }

    struct Quaternion
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float w = 1.0f;
    };

    struct Matrix3
    {
        float value[3][3] = {};

        static Matrix3 FromQuaternion(Quaternion quaternion)
        {
            const float length = std::sqrt(
                quaternion.x * quaternion.x +
                quaternion.y * quaternion.y +
                quaternion.z * quaternion.z +
                quaternion.w * quaternion.w);
            if (!std::isfinite(length) || length < 0.001f)
                quaternion = {};
            else {
                const float inverse = 1.0f / length;
                quaternion.x *= inverse;
                quaternion.y *= inverse;
                quaternion.z *= inverse;
                quaternion.w *= inverse;
            }

            const float xx = quaternion.x * quaternion.x;
            const float yy = quaternion.y * quaternion.y;
            const float zz = quaternion.z * quaternion.z;
            const float xy = quaternion.x * quaternion.y;
            const float xz = quaternion.x * quaternion.z;
            const float yz = quaternion.y * quaternion.z;
            const float wx = quaternion.w * quaternion.x;
            const float wy = quaternion.w * quaternion.y;
            const float wz = quaternion.w * quaternion.z;
            Matrix3 result;
            result.value[0][0] = 1.0f - 2.0f * (yy + zz);
            result.value[0][1] = 2.0f * (xy - wz);
            result.value[0][2] = 2.0f * (xz + wy);
            result.value[1][0] = 2.0f * (xy + wz);
            result.value[1][1] = 1.0f - 2.0f * (xx + zz);
            result.value[1][2] = 2.0f * (yz - wx);
            result.value[2][0] = 2.0f * (xz - wy);
            result.value[2][1] = 2.0f * (yz + wx);
            result.value[2][2] = 1.0f - 2.0f * (xx + yy);
            return result;
        }

        Vector3 Rotate(const Vector3& input) const
        {
            return {
                value[0][0] * input.x + value[0][1] * input.y +
                    value[0][2] * input.z,
                value[1][0] * input.x + value[1][1] * input.y +
                    value[1][2] * input.z,
                value[2][0] * input.x + value[2][1] * input.y +
                    value[2][2] * input.z,
            };
        }
    };

    struct GlobalSurfaceEntry
    {
        float unknown00 = 0.0f;
        float unknown04 = 0.0f;
        float penetrationModifier = 0.0f;
        float unknown0C = 0.0f;
        float unknown10 = 0.0f;
        uint16_t surfaceType = 0;
        uint16_t padding = 0;
        uint8_t trailing[8] = {};
    };
    static_assert(sizeof(GlobalSurfaceEntry) == 32);

    struct Triangle
    {
        Vector3 v0 = {};
        Vector3 v1 = {};
        Vector3 v2 = {};
        SurfaceInfo surface = {};
    };

    struct Aabb
    {
        float minimum[3] = { 1e12f, 1e12f, 1e12f };
        float maximum[3] = { -1e12f, -1e12f, -1e12f };

        void Expand(const Vector3& point)
        {
            minimum[0] = (std::min)(minimum[0], point.x);
            minimum[1] = (std::min)(minimum[1], point.y);
            minimum[2] = (std::min)(minimum[2], point.z);
            maximum[0] = (std::max)(maximum[0], point.x);
            maximum[1] = (std::max)(maximum[1], point.y);
            maximum[2] = (std::max)(maximum[2], point.z);
        }

        void Expand(const Aabb& other)
        {
            for (int axis = 0; axis < 3; ++axis) {
                minimum[axis] = (std::min)(minimum[axis], other.minimum[axis]);
                maximum[axis] = (std::max)(maximum[axis], other.maximum[axis]);
            }
        }

        int LongestAxis() const
        {
            const float x = maximum[0] - minimum[0];
            const float y = maximum[1] - minimum[1];
            const float z = maximum[2] - minimum[2];
            return x >= y && x >= z ? 0 : (y >= z ? 1 : 2);
        }

        bool IntersectsRay(
            const float origin[3],
            const float inverseDirection[3],
            float maximumDistance) const
        {
            float minimumTime = 0.0f;
            float maximumTime = maximumDistance;
            for (int axis = 0; axis < 3; ++axis) {
                float first =
                    (minimum[axis] - origin[axis]) * inverseDirection[axis];
                float second =
                    (maximum[axis] - origin[axis]) * inverseDirection[axis];
                if (inverseDirection[axis] < 0.0f)
                    std::swap(first, second);
                minimumTime = (std::max)(minimumTime, first);
                maximumTime = (std::min)(maximumTime, second);
                if (maximumTime < minimumTime)
                    return false;
            }
            return true;
        }
    };

    struct BvhNode
    {
        Aabb bounds = {};
        int left = -1;
        int right = -1;
        int triangleStart = 0;
        int triangleCount = 0;
    };

    struct BvhData
    {
        std::string mapKey;
        std::vector<Triangle> triangles;
        std::vector<BvhNode> nodes;
        std::vector<int> indices;
        std::vector<Aabb> triangleBounds;
        std::vector<float> centroids;

        int BuildRecursive(int start, int end, int depth)
        {
            const int nodeIndex = static_cast<int>(nodes.size());
            nodes.emplace_back();
            const int count = end - start;
            Aabb bounds;
            for (int index = start; index < end; ++index)
                bounds.Expand(triangleBounds[indices[index]]);
            nodes[nodeIndex].bounds = bounds;
            if (count <= kMaximumLeafTriangles || depth >= kMaximumBvhDepth) {
                nodes[nodeIndex].triangleStart = start;
                nodes[nodeIndex].triangleCount = count;
                return nodeIndex;
            }

            Aabb centroidBounds;
            for (int index = start; index < end; ++index) {
                const size_t offset = static_cast<size_t>(indices[index]) * 3u;
                centroidBounds.Expand(Vector3{
                    centroids[offset],
                    centroids[offset + 1],
                    centroids[offset + 2]
                });
            }
            const int axis = centroidBounds.LongestAxis();
            const float midpoint =
                (centroidBounds.minimum[axis] + centroidBounds.maximum[axis]) *
                0.5f;
            const auto partition = std::partition(
                indices.begin() + start,
                indices.begin() + end,
                [&](int index) {
                    return centroids[static_cast<size_t>(index) * 3u + axis] <
                           midpoint;
                });
            int split = static_cast<int>(partition - indices.begin());
            if (split == start || split == end)
                split = start + count / 2;
            const int left = BuildRecursive(start, split, depth + 1);
            const int right = BuildRecursive(split, end, depth + 1);
            nodes[nodeIndex].left = left;
            nodes[nodeIndex].right = right;
            return nodeIndex;
        }

        bool BuildAcceleration(std::string& error)
        {
            const int count = static_cast<int>(triangles.size());
            if (count <= 0) {
                error = "physics world produced no triangles";
                return false;
            }
            indices.resize(count);
            std::iota(indices.begin(), indices.end(), 0);
            triangleBounds.resize(count);
            centroids.resize(static_cast<size_t>(count) * 3u);
            for (int index = 0; index < count; ++index) {
                const Triangle& triangle = triangles[index];
                if (!IsFinitePosition(triangle.v0) ||
                    !IsFinitePosition(triangle.v1) ||
                    !IsFinitePosition(triangle.v2)) {
                    error = "physics world contains non-finite geometry";
                    return false;
                }
                Aabb bounds;
                bounds.Expand(triangle.v0);
                bounds.Expand(triangle.v1);
                bounds.Expand(triangle.v2);
                triangleBounds[index] = bounds;
                const size_t offset = static_cast<size_t>(index) * 3u;
                centroids[offset] =
                    (bounds.minimum[0] + bounds.maximum[0]) * 0.5f;
                centroids[offset + 1] =
                    (bounds.minimum[1] + bounds.maximum[1]) * 0.5f;
                centroids[offset + 2] =
                    (bounds.minimum[2] + bounds.maximum[2]) * 0.5f;
            }
            nodes.clear();
            nodes.reserve(static_cast<size_t>(count) * 2u);
            BuildRecursive(0, count, 0);
            if (nodes.empty()) {
                error = "BVH acceleration tree is empty";
                return false;
            }
            return true;
        }

        TraceResult Trace(const Vector3& start, const Vector3& end) const
        {
            TraceResult result;
            result.endPosition = end;
            if (nodes.empty())
                return result;
            const Vector3 delta = end - start;
            const float maximumDistance = delta.Length();
            if (!std::isfinite(maximumDistance) || maximumDistance < 1e-6f)
                return result;
            const float inverseDistance = 1.0f / maximumDistance;
            const float direction[3] = {
                delta.x * inverseDistance,
                delta.y * inverseDistance,
                delta.z * inverseDistance,
            };
            const float origin[3] = { start.x, start.y, start.z };
            const float inverseDirection[3] = {
                std::fabs(direction[0]) > 1e-8f
                    ? 1.0f / direction[0]
                    : (direction[0] >= 0.0f ? 1e12f : -1e12f),
                std::fabs(direction[1]) > 1e-8f
                    ? 1.0f / direction[1]
                    : (direction[1] >= 0.0f ? 1e12f : -1e12f),
                std::fabs(direction[2]) > 1e-8f
                    ? 1.0f / direction[2]
                    : (direction[2] >= 0.0f ? 1e12f : -1e12f),
            };

            float closest = maximumDistance;
            std::array<int, 128> stack = {};
            int stackSize = 1;
            stack[0] = 0;
            while (stackSize > 0) {
                const BvhNode& node = nodes[stack[--stackSize]];
                if (!node.bounds.IntersectsRay(
                        origin,
                        inverseDirection,
                        closest)) {
                    continue;
                }
                if (node.left >= 0) {
                    if (stackSize + 2 <= static_cast<int>(stack.size())) {
                        stack[stackSize++] = node.right;
                        stack[stackSize++] = node.left;
                    }
                    continue;
                }

                for (int offset = node.triangleStart;
                     offset < node.triangleStart + node.triangleCount;
                     ++offset) {
                    const int triangleIndex = indices[offset];
                    const Triangle& triangle = triangles[triangleIndex];
                    const Vector3 edge1 = triangle.v1 - triangle.v0;
                    const Vector3 edge2 = triangle.v2 - triangle.v0;
                    const Vector3 h = {
                        direction[1] * edge2.z - direction[2] * edge2.y,
                        direction[2] * edge2.x - direction[0] * edge2.z,
                        direction[0] * edge2.y - direction[1] * edge2.x,
                    };
                    const float determinant =
                        edge1.x * h.x + edge1.y * h.y + edge1.z * h.z;
                    if (std::fabs(determinant) < 1e-8f)
                        continue;
                    const float inverseDeterminant = 1.0f / determinant;
                    const Vector3 s = start - triangle.v0;
                    const float u = inverseDeterminant *
                        (s.x * h.x + s.y * h.y + s.z * h.z);
                    if (u < 0.0f || u > 1.0f)
                        continue;
                    const Vector3 q = {
                        s.y * edge1.z - s.z * edge1.y,
                        s.z * edge1.x - s.x * edge1.z,
                        s.x * edge1.y - s.y * edge1.x,
                    };
                    const float v = inverseDeterminant *
                        (direction[0] * q.x + direction[1] * q.y +
                         direction[2] * q.z);
                    if (v < 0.0f || u + v > 1.0f)
                        continue;
                    const float distance = inverseDeterminant *
                        (edge2.x * q.x + edge2.y * q.y + edge2.z * q.z);
                    if (distance <= 1e-4f || distance >= closest)
                        continue;

                    closest = distance;
                    result.hit = true;
                    result.distance = distance;
                    result.fraction = distance / maximumDistance;
                    result.triangleIndex = triangleIndex;
                    result.surface = triangle.surface;
                    result.endPosition = start + Vector3(
                        direction[0] * distance,
                        direction[1] * distance,
                        direction[2] * distance);
                    Vector3 normal = {
                        edge1.y * edge2.z - edge1.z * edge2.y,
                        edge1.z * edge2.x - edge1.x * edge2.z,
                        edge1.x * edge2.y - edge1.y * edge2.x,
                    };
                    const float normalLength = normal.Length();
                    if (normalLength > 1e-8f)
                        result.normal = normal * (1.0f / normalLength);
                }
            }
            return result;
        }

        std::vector<TraceHit> TraceAll(
            const Vector3& start,
            const Vector3& end) const
        {
            std::vector<TraceHit> hits;
            if (nodes.empty())
                return hits;
            const Vector3 delta = end - start;
            const float maximumDistance = delta.Length();
            if (!std::isfinite(maximumDistance) || maximumDistance < 1e-6f)
                return hits;
            const float inverseDistance = 1.0f / maximumDistance;
            const float direction[3] = {
                delta.x * inverseDistance,
                delta.y * inverseDistance,
                delta.z * inverseDistance,
            };
            const float origin[3] = {start.x, start.y, start.z};
            const float inverseDirection[3] = {
                std::fabs(direction[0]) > 1e-8f
                    ? 1.0f / direction[0]
                    : (direction[0] >= 0.0f ? 1e12f : -1e12f),
                std::fabs(direction[1]) > 1e-8f
                    ? 1.0f / direction[1]
                    : (direction[1] >= 0.0f ? 1e12f : -1e12f),
                std::fabs(direction[2]) > 1e-8f
                    ? 1.0f / direction[2]
                    : (direction[2] >= 0.0f ? 1e12f : -1e12f),
            };
            std::array<int, 128> stack = {};
            int stackSize = 1;
            stack[0] = 0;
            while (stackSize > 0) {
                const BvhNode& node = nodes[stack[--stackSize]];
                if (!node.bounds.IntersectsRay(
                        origin,
                        inverseDirection,
                        maximumDistance)) {
                    continue;
                }
                if (node.left >= 0) {
                    if (stackSize + 2 <= static_cast<int>(stack.size())) {
                        stack[stackSize++] = node.right;
                        stack[stackSize++] = node.left;
                    }
                    continue;
                }
                for (int offset = node.triangleStart;
                     offset < node.triangleStart + node.triangleCount;
                     ++offset) {
                    const int triangleIndex = indices[offset];
                    const Triangle& triangle = triangles[triangleIndex];
                    const Vector3 edge1 = triangle.v1 - triangle.v0;
                    const Vector3 edge2 = triangle.v2 - triangle.v0;
                    const Vector3 h = {
                        direction[1] * edge2.z - direction[2] * edge2.y,
                        direction[2] * edge2.x - direction[0] * edge2.z,
                        direction[0] * edge2.y - direction[1] * edge2.x,
                    };
                    const float determinant =
                        edge1.x * h.x + edge1.y * h.y + edge1.z * h.z;
                    if (std::fabs(determinant) < 1e-8f)
                        continue;
                    const float inverseDeterminant = 1.0f / determinant;
                    const Vector3 relative = start - triangle.v0;
                    const float u = inverseDeterminant *
                        (relative.x * h.x + relative.y * h.y + relative.z * h.z);
                    if (u < 0.0f || u > 1.0f)
                        continue;
                    const Vector3 q = {
                        relative.y * edge1.z - relative.z * edge1.y,
                        relative.z * edge1.x - relative.x * edge1.z,
                        relative.x * edge1.y - relative.y * edge1.x,
                    };
                    const float v = inverseDeterminant *
                        (direction[0] * q.x + direction[1] * q.y +
                         direction[2] * q.z);
                    if (v < 0.0f || u + v > 1.0f)
                        continue;
                    const float distance = inverseDeterminant *
                        (edge2.x * q.x + edge2.y * q.y + edge2.z * q.z);
                    if (distance <= 1e-4f || distance >= maximumDistance)
                        continue;
                    Vector3 normal = {
                        edge1.y * edge2.z - edge1.z * edge2.y,
                        edge1.z * edge2.x - edge1.x * edge2.z,
                        edge1.x * edge2.y - edge1.y * edge2.x,
                    };
                    const float normalLength = normal.Length();
                    if (normalLength > 1e-8f)
                        normal = normal * (1.0f / normalLength);
                    hits.push_back({
                        distance,
                        distance / maximumDistance,
                        start + Vector3(
                            direction[0] * distance,
                            direction[1] * distance,
                            direction[2] * distance),
                        normal,
                        triangle.surface,
                        triangleIndex,
                        normal.x * direction[0] + normal.y * direction[1] +
                            normal.z * direction[2] < 0.0f,
                    });
                }
            }
            std::sort(
                hits.begin(),
                hits.end(),
                [](const TraceHit& first, const TraceHit& second) {
                    return first.distance < second.distance;
                });
            return hits;
        }
    };

    std::vector<PenetrationSegment> BuildPenetrationSegments(
        std::vector<TraceHit> hits,
        float rayLength)
    {
        std::vector<PenetrationSegment> segments;
        if (hits.empty() || !std::isfinite(rayLength) || rayLength <= 0.0f)
            return segments;
        for (size_t index = 1; index < hits.size(); ++index) {
            TraceHit& previous = hits[index - 1];
            TraceHit& current = hits[index];
            if (!current.entering && previous.entering &&
                (current.fraction - previous.fraction) * rayLength <=
                    (1.0f / 512.0f)) {
                std::swap(previous, current);
            }
        }

        bool previousWasExit = true;
        int enterIndex = -1;
        for (size_t index = 0; index < hits.size(); ++index) {
            const bool isExit = !hits[index].entering;
            if (isExit == previousWasExit)
                continue;
            previousWasExit = isExit;
            if (!isExit) {
                if (enterIndex >= 0 && index > 0) {
                    const TraceHit& enter = hits[enterIndex];
                    const TraceHit& exit = hits[index - 1];
                    const float thickness = exit.distance - enter.distance;
                    if (thickness > 0.0f) {
                        segments.push_back({
                            enter.distance,
                            exit.distance,
                            enter.position,
                            exit.position,
                            enter.surface,
                            exit.surface,
                            thickness,
                            enter.surface.penetration,
                        });
                    }
                }
                enterIndex = static_cast<int>(index);
            }
        }
        if (enterIndex >= 0) {
            const TraceHit& enter = hits[enterIndex];
            const TraceHit& exit = hits.back();
            segments.push_back({
                enter.distance,
                exit.distance,
                enter.position,
                exit.position,
                enter.surface,
                exit.surface,
                std::max(1.0f, exit.distance - enter.distance),
                enter.surface.penetration,
            });
        }
        if (!segments.empty())
            return segments;

        for (size_t index = 0; index + 1 < hits.size(); index += 2) {
            const TraceHit& enter = hits[index];
            const TraceHit& exit = hits[index + 1];
            segments.push_back({
                enter.distance,
                exit.distance,
                enter.position,
                exit.position,
                enter.surface,
                exit.surface,
                std::max(1.0f, exit.distance - enter.distance),
                enter.surface.penetration,
            });
        }
        if ((hits.size() & 1u) != 0u) {
            const TraceHit& hit = hits.back();
            segments.push_back({
                hit.distance,
                hit.distance + 1.0f,
                hit.position,
                hit.position,
                hit.surface,
                hit.surface,
                1.0f,
                hit.surface.penetration,
            });
        }
        return segments;
    }

    struct InnerNode
    {
        float minimum[3];
        uint32_t packed0;
        float maximum[3];
        uint32_t packed1;

        uint32_t Type() const { return packed0 >> 30; }
        uint32_t Payload() const { return packed0 & 0x3FFFFFFFu; }
    };
    static_assert(sizeof(InnerNode) == 32);

    struct HalfEdge
    {
        uint8_t next;
        uint8_t twin;
        uint8_t vertex;
        uint8_t face;
    };

    bool ExtractMesh(
        BuildContext& context,
        uintptr_t bvhPointer,
        uintptr_t vertexPointer,
        uintptr_t trianglePointer,
        uint32_t nodeCount,
        const Matrix3& rotation,
        const float scale[3],
        const float position[3],
        uintptr_t materialArray,
        int materialCount,
        const std::vector<GlobalSurfaceEntry>& surfaces,
        SurfaceInfo defaultSurface,
        std::vector<Triangle>& output)
    {
        if (!IsGamePointer(bvhPointer) || !IsGamePointer(vertexPointer) ||
            !IsGamePointer(trianglePointer) || nodeCount == 0 ||
            nodeCount > 0x1000000u) {
            return false;
        }
        std::vector<InnerNode> nodes(nodeCount);
        if (!context.Read(
                bvhPointer,
                nodes.data(),
                nodes.size() * sizeof(InnerNode))) {
            return false;
        }

        uint32_t minimumTriangle = UINT32_MAX;
        uint32_t maximumTriangle = 0;
        std::vector<std::pair<uint32_t, uint32_t>> ranges;
        std::vector<uint32_t> stack = { 0u };
        stack.reserve(256);
        while (!stack.empty()) {
            const uint32_t cursor = stack.back();
            stack.pop_back();
            if (cursor >= nodeCount)
                continue;
            const InnerNode& node = nodes[cursor];
            const uint32_t payload = node.Payload();
            if (node.Type() == 3) {
                if (payload == 0 || payload >= 0x1000000u)
                    continue;
                ranges.emplace_back(node.packed1, payload);
                minimumTriangle = (std::min)(minimumTriangle, node.packed1);
                maximumTriangle = (std::max)(
                    maximumTriangle,
                    node.packed1 + payload);
                continue;
            }
            if (payload == 0)
                continue;
            if (cursor + payload < nodeCount)
                stack.push_back(cursor + payload);
            if (cursor + 1 < nodeCount)
                stack.push_back(cursor + 1);
        }
        if (ranges.empty() || maximumTriangle <= minimumTriangle)
            return false;
        const uint32_t triangleCount = maximumTriangle - minimumTriangle;
        if (triangleCount > 0x1000000u ||
            output.size() + triangleCount > kMaximumTriangles) {
            return false;
        }

        std::vector<int> indices(static_cast<size_t>(triangleCount) * 3u);
        if (!context.Read(
                trianglePointer + static_cast<uintptr_t>(minimumTriangle) * 12u,
                indices.data(),
                indices.size() * sizeof(int))) {
            return false;
        }
        int maximumVertex = 0;
        for (const int index : indices) {
            if (index < 0)
                return false;
            maximumVertex = (std::max)(maximumVertex, index);
        }
        if (maximumVertex <= 0 || maximumVertex > 0x1000000)
            return false;
        const uint32_t vertexCount = static_cast<uint32_t>(maximumVertex + 1);
        std::vector<float> vertices(static_cast<size_t>(vertexCount) * 3u);
        if (!context.Read(
                vertexPointer,
                vertices.data(),
                vertices.size() * sizeof(float))) {
            return false;
        }

        std::vector<uint8_t> materials;
        const bool hasMaterials = IsGamePointer(materialArray) && materialCount > 0;
        if (hasMaterials) {
            materials.resize(triangleCount);
            if (!context.Read(
                    materialArray + minimumTriangle,
                    materials.data(),
                    materials.size())) {
                materials.clear();
            }
        }

        defaultSurface.kind = ShapeKind::Mesh;
        const size_t before = output.size();
        for (const auto& [start, count] : ranges) {
            for (uint32_t index = 0; index < count; ++index) {
                const uint32_t local = start - minimumTriangle + index;
                if (local >= triangleCount)
                    continue;
                SurfaceInfo surface = defaultSurface;
                if (local < materials.size() && materials[local] < surfaces.size()) {
                    const uint8_t material = materials[local];
                    surface.penetration =
                        surfaces[material].penetrationModifier;
                    surface.surfaceType = surfaces[material].surfaceType;
                    surface.globalIndex = material;
                }
                const size_t base = static_cast<size_t>(local) * 3u;
                const int vertexIndices[3] = {
                    indices[base],
                    indices[base + 1],
                    indices[base + 2],
                };
                if (vertexIndices[0] >= static_cast<int>(vertexCount) ||
                    vertexIndices[1] >= static_cast<int>(vertexCount) ||
                    vertexIndices[2] >= static_cast<int>(vertexCount)) {
                    continue;
                }
                const auto transform = [&](int vertexIndex) {
                    const size_t vertex = static_cast<size_t>(vertexIndex) * 3u;
                    const Vector3 localPosition = rotation.Rotate({
                        vertices[vertex] * scale[0],
                        vertices[vertex + 1] * scale[1],
                        vertices[vertex + 2] * scale[2]
                    });
                    return Vector3(
                        localPosition.x + position[0],
                        localPosition.y + position[1],
                        localPosition.z + position[2]);
                };
                Triangle triangle = {
                    transform(vertexIndices[0]),
                    transform(vertexIndices[1]),
                    transform(vertexIndices[2]),
                    surface,
                };
                const Vector3 first = triangle.v1 - triangle.v0;
                const Vector3 second = triangle.v2 - triangle.v0;
                const Vector3 cross = {
                    first.y * second.z - first.z * second.y,
                    first.z * second.x - first.x * second.z,
                    first.x * second.y - first.y * second.x,
                };
                if (IsFinitePosition(triangle.v0) &&
                    IsFinitePosition(triangle.v1) &&
                    IsFinitePosition(triangle.v2) && cross.Length() > 1e-4f) {
                    output.push_back(triangle);
                }
            }
        }
        return output.size() > before;
    }

    bool ExtractHull(
        BuildContext& context,
        uintptr_t hullData,
        float scale,
        SurfaceInfo surface,
        std::vector<Triangle>& output)
    {
        if (!IsGamePointer(hullData))
            return false;
        std::array<uint8_t, 0x100> header = {};
        if (!context.Read(hullData, header.data(), header.size()))
            return false;
        int vertexCount = 0;
        uintptr_t vertexPointer = 0;
        int edgeCount = 0;
        uintptr_t edgePointer = 0;
        int faceCount = 0;
        uintptr_t facePointer = 0;
        std::memcpy(&vertexCount, header.data() + 0x88, sizeof(vertexCount));
        std::memcpy(&vertexPointer, header.data() + 0x90, sizeof(vertexPointer));
        std::memcpy(&edgeCount, header.data() + 0xA0, sizeof(edgeCount));
        std::memcpy(&edgePointer, header.data() + 0xA8, sizeof(edgePointer));
        std::memcpy(&faceCount, header.data() + 0xB8, sizeof(faceCount));
        std::memcpy(&facePointer, header.data() + 0xC0, sizeof(facePointer));
        const auto sane = [](int count, uintptr_t pointer) {
            return count > 0 && count <= 0xFFFF && IsGamePointer(pointer);
        };
        if (!sane(vertexCount, vertexPointer) || !sane(edgeCount, edgePointer) ||
            !sane(faceCount, facePointer)) {
            return false;
        }
        std::vector<float> vertices(static_cast<size_t>(vertexCount) * 3u);
        std::vector<HalfEdge> edges(edgeCount);
        std::vector<uint8_t> faces(faceCount);
        if (!context.Read(
                vertexPointer,
                vertices.data(),
                vertices.size() * sizeof(float)) ||
            !context.Read(
                edgePointer,
                edges.data(),
                edges.size() * sizeof(HalfEdge)) ||
            !context.Read(facePointer, faces.data(), faces.size())) {
            return false;
        }
        surface.kind = ShapeKind::Hull;
        const size_t before = output.size();
        std::vector<int> faceVertices;
        faceVertices.reserve(16);
        const auto vertex = [&](int index) {
            if (index < 0 || index >= vertexCount)
                return Vector3{};
            const size_t offset = static_cast<size_t>(index) * 3u;
            return Vector3(
                vertices[offset] * scale,
                vertices[offset + 1] * scale,
                vertices[offset + 2] * scale);
        };
        for (int face = 0; face < faceCount; ++face) {
            const int firstEdge = faces[face];
            if (firstEdge < 0 || firstEdge >= edgeCount)
                continue;
            faceVertices.clear();
            int edge = firstEdge;
            for (int safety = 0; edge >= 0 && edge < edgeCount && safety < 64;
                 ++safety) {
                faceVertices.push_back(edges[edge].vertex);
                edge = edges[edge].next;
                if (edge == firstEdge)
                    break;
            }
            if (faceVertices.size() < 3)
                continue;
            const Vector3 first = vertex(faceVertices[0]);
            for (size_t index = 1; index + 1 < faceVertices.size(); ++index) {
                if (output.size() >= kMaximumTriangles)
                    return false;
                output.push_back({
                    first,
                    vertex(faceVertices[index]),
                    vertex(faceVertices[index + 1]),
                    surface,
                });
            }
        }
        return output.size() > before;
    }

    void ProcessShape(
        BuildContext& context,
        uintptr_t shape,
        uintptr_t hullVtable,
        uintptr_t meshVtable,
        const std::vector<GlobalSurfaceEntry>& surfaces,
        std::vector<Triangle>& output)
    {
        uint64_t interactionFlags = 0;
        uintptr_t vtable = 0;
        if (!context.Read(shape + 0x50, interactionFlags) ||
            !context.Read(shape, vtable) ||
            (interactionFlags & 0xFFFFull) == 0 ||
            interactionFlags == 0x40000008ull ||
            interactionFlags == 0x40000030ull) {
            return;
        }
        float penetration = 0.0f;
        context.Read(shape + 0x28, penetration);
        if (!std::isfinite(penetration) || penetration < 0.0f ||
            penetration > 100.0f) {
            penetration = 1.0f;
        }

        if (vtable == hullVtable) {
            uintptr_t hullData = 0;
            float scale = 1.0f;
            if (!context.Read(shape + 0xB8, hullData))
                return;
            context.Read(shape + 0xB0, scale);
            if (!std::isfinite(scale) || scale <= 0.0f || scale > 1000.0f)
                scale = 1.0f;
            SurfaceInfo surface;
            surface.penetration = penetration;
            ExtractHull(context, hullData, scale, surface, output);
            return;
        }
        if (vtable != meshVtable)
            return;

        uintptr_t meshData = 0;
        if (!context.Read(shape + 0xC0, meshData) || !IsGamePointer(meshData))
            return;
        std::array<uint8_t, 0xA0> header = {};
        if (!context.Read(meshData, header.data(), header.size()))
            return;
        int nodeCount = 0;
        int vertexCount = 0;
        int triangleCount = 0;
        uintptr_t bvhPointer = 0;
        uintptr_t vertexPointer = 0;
        uintptr_t trianglePointer = 0;
        int materialCount = 0;
        uintptr_t materialPointer = 0;
        std::memcpy(&nodeCount, header.data() + 0x18, sizeof(nodeCount));
        std::memcpy(&bvhPointer, header.data() + 0x20, sizeof(bvhPointer));
        std::memcpy(&vertexCount, header.data() + 0x30, sizeof(vertexCount));
        std::memcpy(&vertexPointer, header.data() + 0x38, sizeof(vertexPointer));
        std::memcpy(&triangleCount, header.data() + 0x48, sizeof(triangleCount));
        std::memcpy(&trianglePointer, header.data() + 0x50, sizeof(trianglePointer));
        std::memcpy(&materialCount, header.data() + 0x90, sizeof(materialCount));
        std::memcpy(&materialPointer, header.data() + 0x98, sizeof(materialPointer));
        if (nodeCount <= 0 || vertexCount <= 0 || triangleCount <= 0)
            return;

        float scale[3] = {};
        float worldPosition[3] = {};
        Quaternion quaternion;
        if (!context.Read(shape + 0xB0, static_cast<void*>(scale), sizeof(scale)) ||
            !context.Read(
                shape + 0x100,
                static_cast<void*>(worldPosition),
                sizeof(worldPosition)) ||
            !context.Read(shape + 0x130, quaternion)) {
            return;
        }
        for (float& component : scale) {
            if (!std::isfinite(component) || std::fabs(component) > 1000.0f)
                return;
            if (component == 0.0f)
                component = 1.0f;
        }
        for (const float component : worldPosition) {
            if (!std::isfinite(component) || std::fabs(component) > 10'000'000.0f)
                return;
        }
        SurfaceInfo surface;
        surface.penetration = penetration;
        ExtractMesh(
            context,
            bvhPointer,
            vertexPointer,
            trianglePointer,
            static_cast<uint32_t>(nodeCount),
            Matrix3::FromQuaternion(quaternion),
            scale,
            worldPosition,
            materialPointer,
            materialCount,
            surfaces,
            surface,
            output);
    }

    std::vector<GlobalSurfaceEntry> LoadSurfaceTable(
        BuildContext& context,
        uintptr_t manager)
    {
        std::vector<GlobalSurfaceEntry> table;
        uintptr_t array = 0;
        if (!context.Read(manager + 40, array) || !IsGamePointer(array))
            return table;
        int count = 0;
        for (const int offset : { 32, 36, 24, 28, 48 }) {
            int candidate = 0;
            if (context.Read(manager + offset, candidate) && candidate > 0 &&
                candidate < 4096) {
                count = candidate;
                break;
            }
        }
        if (count <= 0) {
            int emptyRun = 0;
            for (int index = 0; index < 1024; ++index) {
                GlobalSurfaceEntry entry;
                if (!context.Read(
                        array + static_cast<uintptr_t>(index) * sizeof(entry),
                        entry)) {
                    break;
                }
                const bool empty = entry.penetrationModifier == 0.0f &&
                    entry.surfaceType == 0 && entry.unknown00 == 0.0f;
                if (empty) {
                    if (++emptyRun > 8)
                        break;
                    continue;
                }
                emptyRun = 0;
                count = index + 1;
            }
        }
        if (count > 0) {
            table.resize(count);
            if (!context.Read(
                    array,
                    table.data(),
                    table.size() * sizeof(GlobalSurfaceEntry))) {
                table.clear();
            }
        }
        return table;
    }

    std::shared_ptr<BvhData> BuildPhysicsBvh(
        BuildContext& context,
        const std::string& mapKey,
        std::string& error)
    {
        RemoteModule client;
        RemoteModule physics;
        if (!ReadRemoteModule(context, "client.dll", client, error) ||
            !ReadRemoteModule(context, "vphysics2.dll", physics, error)) {
            return {};
        }
        const uintptr_t physicsInstruction = FindPatternAddress(
            client,
            "48 8B 0D ?? ?? ?? ?? E8 ?? ?? ?? ?? C7 87 10 1D 00 00 00 00 80 3F");
        const uintptr_t surfaceInstruction = FindPatternAddress(
            client,
            "48 63 41 ?? 48 8B 0D");
        if (!physicsInstruction || !surfaceInstruction) {
            error = "physics signatures unresolved";
            return {};
        }

        uintptr_t holder = 0;
        uintptr_t world = 0;
        const uintptr_t holderAddress = ResolveRip(client, physicsInstruction);
        if (!context.Read(holderAddress, holder) ||
            !context.Read(holder, world) || !IsGamePointer(world)) {
            error = "physics world unavailable";
            return {};
        }
        uintptr_t surfaceManager = 0;
        const uintptr_t surfaceAddress = ResolveRip(
            client,
            surfaceInstruction + 4u);
        if (!context.Read(surfaceAddress, surfaceManager) ||
            !IsGamePointer(surfaceManager)) {
            error = "surface manager unavailable";
            return {};
        }
        const uintptr_t hullVtable = FindVtable(physics, "CRnHullShape");
        const uintptr_t meshVtable = FindVtable(physics, "CRnMeshShape");
        if (!hullVtable || !meshVtable) {
            error = "physics shape RTTI unavailable";
            return {};
        }

        uintptr_t innerWorld = 0;
        uintptr_t bodyArray = 0;
        int bodyCount = 0;
        if (!context.Read(world + 0x30, innerWorld) ||
            !context.Read(innerWorld + 0x110, bodyArray) ||
            !context.Read(bodyArray + 0x268, bodyCount) ||
            !IsGamePointer(bodyArray) || bodyCount <= 0 ||
            bodyCount > kMaximumBodies) {
            error = "physics body array invalid";
            return {};
        }
        const std::vector<GlobalSurfaceEntry> surfaces =
            LoadSurfaceTable(context, surfaceManager);

        auto data = std::make_shared<BvhData>();
        data->mapKey = mapKey;
        data->triangles.reserve(262144);
        for (int bodyIndex = 0; bodyIndex < bodyCount; ++bodyIndex) {
            if (context.IsCanceled()) {
                error = "build superseded";
                return {};
            }
            const uintptr_t body =
                bodyArray + static_cast<uintptr_t>(bodyIndex) * 88u;
            uint32_t type = 0;
            if (!context.Read(body + 0x40, type) || type != 2)
                continue;
            uintptr_t outerNodes = 0;
            if (!context.Read(body + 0x18, outerNodes) ||
                !IsGamePointer(outerNodes)) {
                continue;
            }
            int root = -1;
            if (!context.Read(body, root))
                continue;
            if (root < 0) {
                uintptr_t shape = 0;
                if (context.Read(body + 0x28, shape) && IsGamePointer(shape))
                    ProcessShape(
                        context,
                        shape,
                        hullVtable,
                        meshVtable,
                        surfaces,
                        data->triangles);
                continue;
            }

            int countA = 0;
            int countB = 0;
            context.Read(body + 0x08, countA);
            context.Read(body + 0x10, countB);
            const uint32_t outerCount = (std::max)({
                static_cast<uint32_t>(root + 1),
                countA > 0 ? static_cast<uint32_t>(countA) : 0u,
                countB > 0 ? static_cast<uint32_t>(countB) : 0u,
            });
            if (outerCount == 0 || outerCount > 0x100000u)
                continue;
            std::vector<uint8_t> nodes(static_cast<size_t>(outerCount) * 48u);
            if (!context.Read(outerNodes, nodes.data(), nodes.size()))
                continue;
            std::unordered_set<uintptr_t> seen;
            std::vector<int> stack = { root };
            stack.reserve(128);
            while (!stack.empty()) {
                const int index = stack.back();
                stack.pop_back();
                if (index < 0 || index >= static_cast<int>(outerCount))
                    continue;
                const uint8_t* node =
                    nodes.data() + static_cast<size_t>(index) * 48u;
                int left = -1;
                std::memcpy(&left, node + 12, sizeof(left));
                if (left == -1) {
                    uintptr_t shape = 0;
                    std::memcpy(&shape, node + 0x28, sizeof(shape));
                    if (IsGamePointer(shape) && seen.insert(shape).second)
                        ProcessShape(
                            context,
                            shape,
                            hullVtable,
                            meshVtable,
                            surfaces,
                            data->triangles);
                    continue;
                }
                int right = -1;
                std::memcpy(&right, node + 28, sizeof(right));
                stack.push_back(right);
                stack.push_back(left);
            }
            if ((bodyIndex & 0x0F) == 0)
                std::this_thread::yield();
        }
        if (data->triangles.size() < 64) {
            error = "physics geometry under-resolved";
            return {};
        }
        if (!data->BuildAcceleration(error))
            return {};
        return data;
    }

    class PhysicsBvhService
    {
    public:
        PhysicsBvhService()
            : worker_([this](std::stop_token token) { Worker(token); })
        {
        }

        ~PhysicsBvhService()
        {
            Shutdown();
        }

        void Request(const char* mapKey)
        {
            const std::string normalized = mapKey ? mapKey : "";
            std::lock_guard<std::mutex> lock(mutex_);
            if (stopping_)
                return;
            if (normalized.empty()) {
                requestedMap_.clear();
                ++requestedGeneration_;
                stats_.state = BuildState::Idle;
                stats_.mapKey[0] = '\0';
                std::snprintf(
                    stats_.detail,
                    sizeof(stats_.detail),
                    "%s",
                    "waiting for an active map");
                condition_.notify_all();
                return;
            }
            const std::shared_ptr<const BvhData> current = data_.load();
            const uint64_t nowUs = NowUs();
            if (requestedMap_ == normalized &&
                (stats_.state == BuildState::Queued ||
                 stats_.state == BuildState::Building ||
                 (stats_.state == BuildState::Ready && current &&
                  current->mapKey == normalized) ||
                 (stats_.state == BuildState::Failed &&
                  lastFailureAtUs_ != 0 &&
                  nowUs >= lastFailureAtUs_ &&
                  nowUs - lastFailureAtUs_ < kFailedBuildRetryUs))) {
                return;
            }
            requestedMap_ = normalized;
            ++requestedGeneration_;
            stats_.state = BuildState::Queued;
            stats_.generation = requestedGeneration_;
            std::snprintf(
                stats_.mapKey,
                sizeof(stats_.mapKey),
                "%s",
                normalized.c_str());
            std::snprintf(
                stats_.detail,
                sizeof(stats_.detail),
                "%s",
                "queued");
            condition_.notify_all();
        }

        bool Trace(
            const char* mapKey,
            const Vector3& start,
            const Vector3& end,
            TraceResult* output) const
        {
            const std::shared_ptr<const BvhData> current = data_.load();
            if (!current || !mapKey || current->mapKey != mapKey)
                return false;
            const TraceResult result = current->Trace(start, end);
            if (output)
                *output = result;
            return true;
        }

        bool TraceSegments(
            const char* mapKey,
            const Vector3& start,
            const Vector3& end,
            std::vector<PenetrationSegment>& output) const
        {
            output.clear();
            const std::shared_ptr<const BvhData> current = data_.load();
            if (!current || !mapKey || current->mapKey != mapKey)
                return false;
            const float rayLength = (end - start).Length();
            output = BuildPenetrationSegments(
                current->TraceAll(start, end),
                rayLength);
            return true;
        }

        Stats ReadStats() const
        {
            std::lock_guard<std::mutex> lock(mutex_);
            Stats result = stats_;
            if (readyAtUs_ != 0) {
                const uint64_t now = NowUs();
                result.lastReadyAgeUs = now >= readyAtUs_
                    ? now - readyAtUs_
                    : 0;
            }
            return result;
        }

        void Shutdown()
        {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (stopping_)
                    return;
                stopping_ = true;
                ++requestedGeneration_;
                condition_.notify_all();
            }
            worker_.request_stop();
            condition_.notify_all();
            if (worker_.joinable())
                worker_.join();
        }

    private:
        void Worker(std::stop_token token)
        {
            while (!token.stop_requested()) {
                std::string mapKey;
                uint64_t generation = 0;
                {
                    std::unique_lock<std::mutex> lock(mutex_);
                    condition_.wait(lock, [&] {
                        return stopping_ || token.stop_requested() ||
                               (stats_.state == BuildState::Queued &&
                                !requestedMap_.empty());
                    });
                    if (stopping_ || token.stop_requested())
                        return;
                    mapKey = requestedMap_;
                    generation = requestedGeneration_;
                    stats_.state = BuildState::Building;
                    std::snprintf(
                        stats_.detail,
                        sizeof(stats_.detail),
                        "%s",
                        "reading physics world");
                }

                const uint64_t startedAt = NowUs();
                BuildContext context;
                context.canceled = [this, generation, &token] {
                    std::lock_guard<std::mutex> lock(mutex_);
                    return stopping_ || token.stop_requested() ||
                           requestedGeneration_ != generation;
                };
                std::string error;
                std::shared_ptr<BvhData> built =
                    BuildPhysicsBvh(context, mapKey, error);
                const uint64_t finishedAt = NowUs();

                std::lock_guard<std::mutex> lock(mutex_);
                if (stopping_ || token.stop_requested())
                    return;
                if (requestedGeneration_ != generation) {
                    if (!requestedMap_.empty())
                        stats_.state = BuildState::Queued;
                    continue;
                }
                stats_.buildTimeUs = finishedAt >= startedAt
                    ? finishedAt - startedAt
                    : 0;
                stats_.bytesRead = context.bytesRead;
                if (!built) {
                    stats_.state = BuildState::Failed;
                    lastFailureAtUs_ = finishedAt;
                    stats_.triangles = 0;
                    stats_.nodes = 0;
                    std::snprintf(
                        stats_.detail,
                        sizeof(stats_.detail),
                        "%s",
                        error.empty() ? "BVH build failed" : error.c_str());
                    continue;
                }
                stats_.triangles = built->triangles.size();
                stats_.nodes = built->nodes.size();
                stats_.state = BuildState::Ready;
                lastFailureAtUs_ = 0;
                readyAtUs_ = finishedAt;
                std::snprintf(
                    stats_.detail,
                    sizeof(stats_.detail),
                    "%s",
                    "validated and ready");
                data_.store(std::move(built));
            }
        }

        mutable std::mutex mutex_;
        std::condition_variable condition_;
        std::atomic<std::shared_ptr<const BvhData>> data_ = {};
        std::string requestedMap_;
        uint64_t requestedGeneration_ = 0;
        uint64_t readyAtUs_ = 0;
        uint64_t lastFailureAtUs_ = 0;
        Stats stats_ = {};
        bool stopping_ = false;
        std::jthread worker_;
    };

    PhysicsBvhService& Service()
    {
        static PhysicsBvhService service;
        return service;
    }
}

void target::physics::RequestForMap(const char* mapKey)
{
    Service().Request(mapKey);
}

bool target::physics::TraceRay(
    const char* mapKey,
    const Vector3& start,
    const Vector3& end,
    TraceResult* result)
{
    return Service().Trace(mapKey, start, end, result);
}

bool target::physics::IsLineVisible(
    const char* mapKey,
    const Vector3& start,
    const Vector3& end,
    float endpointTolerance)
{
    TraceResult result;
    if (!TraceRay(mapKey, start, end, &result))
        return false;
    if (!result.hit)
        return true;
    endpointTolerance = std::clamp(endpointTolerance, 0.0f, 16.0f);
    const float fullDistance = (end - start).Length();
    return result.distance + endpointTolerance >= fullDistance;
}

bool target::physics::TracePenetrationSegments(
    const char* mapKey,
    const Vector3& start,
    const Vector3& end,
    std::vector<PenetrationSegment>& segments)
{
    return Service().TraceSegments(mapKey, start, end, segments);
}

target::physics::Stats target::physics::GetStats()
{
    return Service().ReadStats();
}

void target::physics::Shutdown()
{
    Service().Shutdown();
}
