#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace runtime_offsets::resolver_policy
{
    struct PatternByte {
        std::uint8_t value = 0;
        bool wildcard = false;
    };

    struct PatternSearchResult {
        std::optional<std::size_t> firstOffset;
        std::size_t matchCount = 0;
    };

    struct SchemaHashNode {
        std::uintptr_t next = 0;
        std::uintptr_t data = 0;
    };

    inline std::optional<SchemaHashNode> DecodeSchemaHashNode(
        std::span<const std::uint8_t> bytes,
        std::size_t nextOffset) noexcept
    {
        constexpr std::size_t kDataOffset = 0x10;
        if (nextOffset > bytes.size() ||
            sizeof(std::uintptr_t) > bytes.size() - nextOffset ||
            kDataOffset > bytes.size() ||
            sizeof(std::uintptr_t) > bytes.size() - kDataOffset) {
            return std::nullopt;
        }

        SchemaHashNode node = {};
        for (std::size_t i = 0; i < sizeof(std::uintptr_t); ++i) {
            reinterpret_cast<std::uint8_t*>(&node.next)[i] = bytes[nextOffset + i];
            reinterpret_cast<std::uint8_t*>(&node.data)[i] = bytes[kDataOffset + i];
        }
        return node;
    }

    inline int HexNibble(char ch) noexcept
    {
        if (ch >= '0' && ch <= '9')
            return ch - '0';
        if (ch >= 'a' && ch <= 'f')
            return ch - 'a' + 10;
        if (ch >= 'A' && ch <= 'F')
            return ch - 'A' + 10;
        return -1;
    }

    inline std::vector<PatternByte> CompilePattern(std::string_view text)
    {
        std::vector<PatternByte> result;
        std::size_t cursor = 0;
        while (cursor < text.size()) {
            while (cursor < text.size() && text[cursor] == ' ')
                ++cursor;
            if (cursor >= text.size())
                break;

            const std::size_t tokenStart = cursor;
            while (cursor < text.size() && text[cursor] != ' ')
                ++cursor;
            const std::string_view token = text.substr(tokenStart, cursor - tokenStart);
            if (token == "?" || token == "??") {
                result.push_back({0, true});
                continue;
            }
            if (token.size() != 2)
                return {};
            const int high = HexNibble(token[0]);
            const int low = HexNibble(token[1]);
            if (high < 0 || low < 0)
                return {};
            result.push_back({static_cast<std::uint8_t>((high << 4) | low), false});
        }
        return result;
    }

    inline PatternSearchResult FindPatternMatches(
        std::span<const std::uint8_t> bytes,
        std::span<const PatternByte> pattern) noexcept
    {
        PatternSearchResult result;
        if (pattern.empty() || bytes.size() < pattern.size())
            return result;

        for (std::size_t i = 0; i <= bytes.size() - pattern.size(); ++i) {
            bool matches = true;
            for (std::size_t j = 0; j < pattern.size(); ++j) {
                if (!pattern[j].wildcard && bytes[i + j] != pattern[j].value) {
                    matches = false;
                    break;
                }
            }
            if (!matches)
                continue;
            if (!result.firstOffset)
                result.firstOffset = i;
            ++result.matchCount;
        }
        return result;
    }

    inline std::optional<std::size_t> FindUniquePattern(
        std::span<const std::uint8_t> bytes,
        std::span<const PatternByte> pattern) noexcept
    {
        const PatternSearchResult result = FindPatternMatches(bytes, pattern);
        return result.matchCount == 1 ? result.firstOffset : std::nullopt;
    }

    inline std::optional<std::uint32_t> ResolveRelativeRva(
        std::uint32_t matchRva,
        std::size_t displacementOffset,
        std::span<const std::uint8_t> bytes,
        std::size_t matchOffset,
        std::uint32_t imageSize) noexcept
    {
        if (matchOffset > bytes.size() ||
            displacementOffset > bytes.size() - matchOffset ||
            sizeof(std::int32_t) >
                bytes.size() - matchOffset - displacementOffset) {
            return std::nullopt;
        }
        std::int32_t displacement = 0;
        const auto* source = bytes.data() + matchOffset + displacementOffset;
        for (std::size_t i = 0; i < sizeof(displacement); ++i)
            reinterpret_cast<std::uint8_t*>(&displacement)[i] = source[i];

        const std::int64_t resolved =
            static_cast<std::int64_t>(matchRva) +
            static_cast<std::int64_t>(displacementOffset) +
            static_cast<std::int64_t>(sizeof(displacement)) +
            static_cast<std::int64_t>(displacement);
        if (resolved <= 0 || resolved >= imageSize)
            return std::nullopt;
        return static_cast<std::uint32_t>(resolved);
    }

    inline std::optional<std::ptrdiff_t> AddRvaOffset(
        const std::optional<std::ptrdiff_t>& rva,
        std::ptrdiff_t adjustment) noexcept
    {
        if (!rva || *rva <= 0 || adjustment < 0 ||
            *rva > std::numeric_limits<std::ptrdiff_t>::max() - adjustment) {
            return std::nullopt;
        }
        return *rva + adjustment;
    }
}
