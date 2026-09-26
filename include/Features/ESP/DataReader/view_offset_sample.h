#pragma once

#include "Game/Schema/structs.h"
#include "app/Core/memory_address.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace esp::data
{
    inline bool IsValidViewOffset(const Vector3& value) noexcept
    {
        return std::isfinite(value.x) && std::isfinite(value.y) &&
            std::isfinite(value.z) && std::fabs(value.x) <= 32.0f &&
            std::fabs(value.y) <= 32.0f && value.z >= 8.0f && value.z <= 96.0f;
    }

    struct ViewOffsetSample
    {
        std::array<uint8_t, 128> bytes = {};
        std::array<size_t, 3> components = {};
        uintptr_t pawn = 0;
        uintptr_t address = 0;
        size_t size = 0;
        bool hasComponents = false;

        bool Prepare(uintptr_t owner, std::ptrdiff_t field,
            std::ptrdiff_t x, std::ptrdiff_t y, std::ptrdiff_t z) noexcept
        {
            *this = {};
            const std::array<std::ptrdiff_t, 3> offsets = {x, y, z};
            if (!app::memory_address::IsLikelyGamePointer(owner) ||
                field <= 0 || field > 0x100000)
                return false;
            const uintptr_t start = owner + static_cast<uintptr_t>(field);
            if (!app::memory_address::IsCanonicalUserPointer(start + sizeof(Vector3) - 1))
                return false;
            pawn = owner;
            address = start;
            size = sizeof(Vector3);
            if (x == y || x == z || y == z)
                return true;
            for (const auto offset : offsets)
                if (offset < 0 || offset > 0x100 || offset % alignof(float) != 0)
                    return true;
            const auto last = *std::max_element(offsets.begin(), offsets.end());
            const size_t extent = (std::max)(sizeof(Vector3), static_cast<size_t>(last) + sizeof(float));
            if (extent > bytes.size() ||
                !app::memory_address::IsCanonicalUserPointer(start + extent - 1))
                return true;
            size = extent;
            hasComponents = true;
            for (size_t i = 0; i < components.size(); ++i)
                components[i] = static_cast<size_t>(offsets[i]);
            return true;
        }

        bool Decode(uintptr_t owner, size_t bytesRead, Vector3& value) const noexcept
        {
            value = {};
            if (owner == 0 || owner != pawn || size < sizeof(float) || size > bytes.size() || bytesRead != size)
                return false;
            Vector3 packed;
            if (size >= sizeof(packed)) {
                std::memcpy(&packed, bytes.data(), sizeof(packed));
                if (IsValidViewOffset(packed)) {
                    value = packed;
                    return true;
                }
            }
            if (!hasComponents)
                return false;
            std::array<float, 3> decoded = {};
            for (size_t i = 0; i < components.size(); ++i) {
                if (components[i] > size - sizeof(float))
                    return false;
                std::memcpy(&decoded[i], bytes.data() + components[i], sizeof(float));
            }
            const Vector3 candidate{decoded[0], decoded[1], decoded[2]};
            if (!IsValidViewOffset(candidate))
                return false;
            value = candidate;
            return true;
        }
    };
}
