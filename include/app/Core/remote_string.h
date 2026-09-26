#pragma once

#include "app/Core/memory_address.h"

#include <algorithm>
#include <array>
#include <string>

namespace app::remote_string
{
    struct NameResult
    {
        std::string text;
        bool readFailed = false;
    };

    template <typename Reader>
    NameResult ReadName(std::uintptr_t address, std::size_t capacity, Reader&& read)
    {
        if (!memory_address::IsCanonicalUserPointer(address) || capacity < 2 || capacity > 256 ||
            address > memory_address::kMaximumUserAddress - capacity)
            return {};
        NameResult result;
        std::array<char, 32> bytes{};
        while (result.text.size() < capacity) {
            const auto current = address + result.text.size();
            const auto count = (std::min)({bytes.size(), capacity - result.text.size(),
                std::size_t{4096} - static_cast<std::size_t>(current & 4095u)});
            if (!read(current, bytes.data(), count))
                return {{}, true};
            for (std::size_t i = 0; i < count; ++i) {
                const auto ch = static_cast<unsigned char>(bytes[i]);
                if (ch == 0) return result;
                if (ch < 0x20 || ch == 0x7F) return {};
                result.text.push_back(static_cast<char>(ch));
            }
        }
        return {};
    }
}
