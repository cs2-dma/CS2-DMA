#pragma once

#include "app/Core/memory_address.h"
#include "app/Core/remote_string.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <array>
#include <algorithm>
#include <string>
#include <string_view>
#include <vector>
#include <unordered_set>
#include <cstring>

namespace runtime_offsets::resolver_policy
{
    inline constexpr std::string_view kSensitivityPattern = "48 8D 0D ?? ?? ?? ?? 0F 57 C9 0F 28 F0";
    inline constexpr std::string_view kLegacySensitivityPattern = "48 8D 0D ?? ?? ?? ?? 66 0F 6E CD";

    template <typename Reader>
    std::vector<std::uintptr_t> ReadSchemaScopes(std::uintptr_t schemaSystem, Reader&& read)
    {
        if (!app::memory_address::IsLikelyGamePointer(schemaSystem) ||
            schemaSystem > 0x00007FFFFFFFFFFFULL - 0x284)
            return {};
        std::array<std::uint8_t, 0xF4> before{};
        std::array<std::uint8_t, 0xF4> after{};
        if (!read(schemaSystem + 0x190, before.data(), before.size())) return {};
        std::int32_t count = 0;
        std::int32_t registrations = 0;
        std::uintptr_t data = 0;
        std::memcpy(&count, before.data(), sizeof(count));
        std::memcpy(&data, before.data() + 0x08, sizeof(data));
        std::memcpy(&registrations, before.data() + 0xF0, sizeof(registrations));
        if (count <= 0 || count > 64 || registrations <= 0 ||
            !app::memory_address::IsLikelyGamePointer(data) ||
            data > 0x00007FFFFFFFFFFFULL - static_cast<std::size_t>(count) * sizeof(std::uintptr_t))
            return {};
        std::vector<std::uintptr_t> scopes(static_cast<std::size_t>(count));
        if (!read(data, scopes.data(), scopes.size() * sizeof(std::uintptr_t)) ||
            !read(schemaSystem + 0x190, after.data(), after.size()))
            return {};
        if (std::memcmp(before.data(), after.data(), sizeof(count)) != 0 ||
            std::memcmp(before.data() + 0x08, after.data() + 0x08, sizeof(data)) != 0 ||
            std::memcmp(before.data() + 0xF0, after.data() + 0xF0, sizeof(registrations)) != 0)
            return {};
        return scopes;
    }

    template <typename Reader, typename Visitor>
    void ReadSchemaFields(std::uintptr_t address, std::size_t count, Reader&& read, Visitor&& visit)
    {
        constexpr std::size_t stride = 0x20;
        if (!address || count == 0 || count > 512 ||
            address > std::numeric_limits<std::uintptr_t>::max() - count * stride)
            return;
        std::vector<std::uint8_t> bytes(count * stride);
        const bool complete = read(address, bytes.data(), bytes.size());
        for (std::size_t i = 0; i < count; ++i) {
            auto* field = bytes.data() + i * stride;
            if (!complete && !read(address + i * stride, field, stride)) continue;
            visit(std::span<const std::uint8_t>(field, stride));
        }
    }

    template <typename Reader>
    std::string ReadSchemaName(std::uintptr_t address, std::size_t capacity, Reader&& read)
    {
        return app::remote_string::ReadName(address, capacity, read).text;
    }

    inline bool AreSchemaClassHeadersCoherent(std::span<const std::uint8_t> before,
        std::span<const std::uint8_t> after) noexcept
    {
        return before.size() >= 0x38 && after.size() >= 0x38 &&
            std::memcmp(before.data() + 0x08, after.data() + 0x08, sizeof(std::uintptr_t)) == 0 &&
            std::memcmp(before.data() + 0x24, after.data() + 0x24, sizeof(std::int16_t)) == 0 &&
            std::memcmp(before.data() + 0x30, after.data() + 0x30, sizeof(std::uintptr_t)) == 0;
    }

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

    inline constexpr std::size_t kSchemaHashBytes = 0x60 + 256 * 0x18;

    struct SchemaMemoryRead {
        std::uintptr_t address = 0;
        void* output = nullptr;
        std::size_t size = 0;
        std::size_t completed = 0;
    };

    template <typename BatchReader>
    std::vector<std::uintptr_t> ReadSchemaBindingsBatched(
        std::span<const std::uint8_t> hash, BatchReader&& readBatch)
    {
        if (hash.size() < kSchemaHashBytes) return {};
        constexpr std::size_t maximumNodes = 8192;
        constexpr std::size_t batchSize = 256;
        std::vector<std::uintptr_t> bindings;
        std::unordered_set<std::uintptr_t> seenNodes, seenData;
        std::vector<std::uintptr_t> pending;
        std::size_t reads = 0;
        const auto pointerAt = [&](std::size_t offset) {
            std::uintptr_t value = 0;
            std::memcpy(&value, hash.data() + offset, sizeof(value));
            return value;
        };
        const auto enqueue = [&](std::uintptr_t node) {
            if (app::memory_address::IsLikelyGamePointer(node) &&
                seenNodes.size() < maximumNodes && seenNodes.insert(node).second)
                pending.push_back(node);
        };
        const auto drain = [&](std::size_t nextOffset) {
            std::size_t cursor = 0;
            while (cursor < pending.size() && reads < maximumNodes) {
                const auto count = (std::min)({batchSize, pending.size() - cursor, maximumNodes - reads});
                std::array<std::array<std::uint8_t, 0x18>, batchSize> nodes{};
                std::array<SchemaMemoryRead, batchSize> requests{};
                for (std::size_t i = 0; i < count; ++i)
                    requests[i] = {pending[cursor + i], nodes[i].data(), nodes[i].size(), 0};
                readBatch(std::span(requests.data(), count));
                reads += count;
                cursor += count;
                for (std::size_t i = 0; i < count; ++i) {
                    if (requests[i].completed != requests[i].size) continue;
                    const auto decoded = DecodeSchemaHashNode(nodes[i], nextOffset);
                    if (!decoded) continue;
                    if (app::memory_address::IsLikelyGamePointer(decoded->data) &&
                        seenData.insert(decoded->data).second)
                        bindings.push_back(decoded->data);
                    enqueue(decoded->next);
                }
            }
        };
        for (const std::size_t headOffset : {std::size_t{0x08}, std::size_t{0x10}})
            for (std::size_t i = 0; i < 256; ++i)
                enqueue(pointerAt(0x60 + i * 0x18 + headOffset));
        drain(0x08);
        seenNodes.clear();
        pending.clear();
        enqueue(pointerAt(0x20));
        drain(0x00);
        return bindings;
    }

    template <typename BatchReader, typename Reader>
    std::vector<std::string> ReadSchemaNamesBatched(
        std::span<const std::uintptr_t> addresses, BatchReader&& readBatch, Reader&& read)
    {
        constexpr std::size_t batchSize = 256;
        constexpr std::size_t capacity = 128;
        std::vector<std::string> names(addresses.size());
        for (std::size_t begin = 0; begin < addresses.size(); begin += batchSize) {
            const auto count = (std::min)(batchSize, addresses.size() - begin);
            std::array<std::array<char, capacity>, batchSize> buffers{};
            std::array<SchemaMemoryRead, batchSize> requests{};
            std::array<std::size_t, batchSize> indices{};
            std::size_t queued = 0;
            for (std::size_t i = 0; i < count; ++i) {
                const auto address = addresses[begin + i];
                if (!app::memory_address::IsCanonicalUserPointer(address) ||
                    address > app::memory_address::kMaximumUserAddress - capacity)
                    continue;
                const auto size = (std::min)(capacity, std::size_t{4096} - (address & 4095u));
                indices[queued] = i;
                requests[queued++] = {address, buffers[i].data(), size, 0};
            }
            if (queued != 0) readBatch(std::span(requests.data(), queued));
            for (std::size_t r = 0; r < queued; ++r) {
                const auto i = indices[r];
                const auto available = (std::min)(requests[r].size, requests[r].completed);
                bool complete = false;
                bool valid = true;
                for (std::size_t j = 0; j < available; ++j) {
                    const auto ch = static_cast<unsigned char>(buffers[i][j]);
                    if (ch == 0) {
                        names[begin + i].assign(buffers[i].data(), j);
                        complete = true;
                        break;
                    }
                    if (ch < 0x20 || ch == 0x7F) { valid = false; break; }
                }
                if (valid && !complete)
                    names[begin + i] = ReadSchemaName(addresses[begin + i], capacity, read);
            }
        }
        return names;
    }

    template <typename Reader>
    std::vector<std::uintptr_t> ReadSchemaBindings(std::span<const std::uint8_t> hash, Reader&& read)
    {
        if (hash.size() < kSchemaHashBytes) return {};
        constexpr std::size_t maximumNodes = 8192;
        std::vector<std::uintptr_t> bindings;
        std::unordered_set<std::uintptr_t> seenNodes;
        std::unordered_set<std::uintptr_t> seenData;
        std::size_t reads = 0;
        const auto pointerAt = [&](std::size_t offset) {
            std::uintptr_t value = 0;
            std::memcpy(&value, hash.data() + offset, sizeof(value));
            return value;
        };
        const auto visitChain = [&](std::uintptr_t node, std::size_t nextOffset) {
            while (app::memory_address::IsLikelyGamePointer(node) && reads < maximumNodes) {
                if (!seenNodes.insert(node).second) break;
                std::array<std::uint8_t, 0x18> bytes{};
                ++reads;
                if (!read(node, bytes.data(), bytes.size())) break;
                const auto decoded = DecodeSchemaHashNode(bytes, nextOffset);
                if (!decoded) break;
                if (app::memory_address::IsLikelyGamePointer(decoded->data) &&
                    seenData.insert(decoded->data).second)
                    bindings.push_back(decoded->data);
                node = decoded->next;
            }
        };
        for (const std::size_t headOffset : {std::size_t{0x08}, std::size_t{0x10}}) {
            for (std::size_t i = 0; i < 256; ++i)
                visitChain(pointerAt(0x60 + i * 0x18 + headOffset), 0x08);
        }
        seenNodes.clear();
        visitChain(pointerAt(0x20), 0x00);
        return bindings;
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

    template <typename Checkpoint>
    PatternSearchResult FindPatternMatches(
        std::span<const std::uint8_t> bytes,
        std::span<const PatternByte> pattern,
        Checkpoint&& checkpoint)
    {
        checkpoint();
        PatternSearchResult result;
        if (pattern.empty() || bytes.size() < pattern.size())
            return result;

        for (std::size_t i = 0; i <= bytes.size() - pattern.size(); ++i) {
            if ((i & 4095u) == 0) checkpoint();
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
        checkpoint();
        return result;
    }

    inline PatternSearchResult FindPatternMatches(
        std::span<const std::uint8_t> bytes,
        std::span<const PatternByte> pattern) noexcept
    {
        return FindPatternMatches(bytes, pattern, []() noexcept {});
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
