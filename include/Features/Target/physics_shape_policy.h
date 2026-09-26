#pragma once

#include "app/Core/memory_address.h"

#include <array>
#include <cmath>
#include <cstring>
#include <optional>
#include <span>

namespace target::physics::shape_policy
{
    enum class Kind : uint8_t { Hull = 2, Mesh = 3 };

    struct Payload
    {
        uintptr_t address = 0;
        size_t scaleOffset = 0;
        std::array<float, 3> scale{};
        std::array<uint8_t, 0x100> header{};
    };

    template <typename T>
    T Field(std::span<const uint8_t> bytes, size_t offset)
    {
        T value{};
        if (offset <= bytes.size() && sizeof(T) <= bytes.size() - offset)
            std::memcpy(&value, bytes.data() + offset, sizeof(T));
        return value;
    }

    inline bool ValidPayloadHeader(std::span<const uint8_t> header, Kind kind)
    {
        const auto validArray = [&](size_t offset, int maximum) {
            const int count = Field<int>(header, offset);
            return count > 0 && count <= maximum &&
                app::memory_address::IsLikelyGamePointer(Field<uintptr_t>(header, offset + 8));
        };
        if (kind == Kind::Hull)
            return header.size() >= 0xC8 && validArray(0x88, 0xFFFF) &&
                validArray(0xA0, 0xFFFF) && validArray(0xB8, 0xFFFF);
        return kind == Kind::Mesh && header.size() >= 0xA0 &&
            validArray(0x18, 0x1000000) && validArray(0x30, 0x1000000) && validArray(0x48, 0x1000000);
    }

    template <typename Reader>
    std::optional<Payload> ReadPayload(std::span<const uint8_t> shape, Kind kind, Reader&& read)
    {
        if (shape.size() < 0xD0 || (kind != Kind::Hull && kind != Kind::Mesh) ||
            Field<uint8_t>(shape, 0x18) != static_cast<uint8_t>(kind))
            return std::nullopt;
        std::optional<Payload> selected;
        for (const size_t scaleOffset : {size_t{0xB8}, size_t{0xB0}}) {
            Payload candidate;
            candidate.scaleOffset = scaleOffset;
            candidate.address = Field<uintptr_t>(shape, scaleOffset + (kind == Kind::Hull ? 8 : 16));
            if (!app::memory_address::IsLikelyGamePointer(candidate.address)) continue;
            candidate.scale[0] = Field<float>(shape, scaleOffset);
            candidate.scale[1] = kind == Kind::Hull ? candidate.scale[0] : Field<float>(shape, scaleOffset + 4);
            candidate.scale[2] = kind == Kind::Hull ? candidate.scale[0] : Field<float>(shape, scaleOffset + 8);
            bool validScale = true;
            for (const float value : candidate.scale)
                validScale = validScale && std::isfinite(value) && std::fabs(value) >= 1e-6f &&
                    std::fabs(value) <= 1000.0f && (kind != Kind::Hull || value > 0.0f);
            if (!validScale) continue;
            const size_t headerSize = kind == Kind::Hull ? 0x100 : 0xA0;
            if (!read(candidate.address, candidate.header.data(), headerSize) ||
                !ValidPayloadHeader(std::span(candidate.header).first(headerSize), kind))
                continue;
            if (selected && (selected->address != candidate.address || selected->scale != candidate.scale))
                return std::nullopt;
            selected = candidate;
        }
        return selected;
    }
}
