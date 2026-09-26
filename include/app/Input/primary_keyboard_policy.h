#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <vector>

namespace app::input::keyboard_policy
{
    inline bool NeedsResolution(bool ready, uint32_t failures, uint64_t failedSinceMs, uint64_t nowMs)
    {
        return !ready || (failures >= 8 && failedSinceMs != 0 && nowMs >= failedSinceMs &&
            nowMs - failedSinceMs >= 500);
    }

    inline uint64_t RetryDelayMs(uint32_t attempts)
    {
        return attempts <= 1 ? 5000 : (attempts == 2 ? 10000 : 30000);
    }

    template <typename Pattern, typename Reader, typename Cancel>
    uintptr_t ScanReadablePattern(uintptr_t base, size_t size, const Pattern& pattern,
        Reader&& read, Cancel&& cancelled, uint64_t* unreadablePages = nullptr)
    {
        constexpr size_t pageSize = 4096;
        constexpr size_t blockSize = 65536;
        if (pattern.empty() || pattern.size() > 128 || size < pattern.size() ||
            base > (std::numeric_limits<uintptr_t>::max)() - size)
            return 0;
        std::vector<uint8_t> block(blockSize);
        std::vector<uint8_t> contiguous;
        contiguous.reserve(blockSize + pattern.size());
        uintptr_t match = 0;
        const auto scan = [&](uintptr_t address, std::span<const uint8_t> bytes) {
            const uintptr_t begin = address - contiguous.size();
            contiguous.insert(contiguous.end(), bytes.begin(), bytes.end());
            for (size_t i = 0; i + pattern.size() <= contiguous.size(); ++i) {
                bool found = true;
                for (size_t j = 0; j < pattern.size(); ++j) {
                    if (!pattern[j].wildcard && pattern[j].value != contiguous[i + j]) {
                        found = false;
                        break;
                    }
                }
                if (found) { match = begin + i; return; }
            }
            const size_t tail = (std::min)(pattern.size() - 1, contiguous.size());
            std::memmove(contiguous.data(), contiguous.data() + contiguous.size() - tail, tail);
            contiguous.resize(tail);
        };
        for (size_t offset = 0; offset < size;) {
            if (cancelled()) return 0;
            const size_t count = (std::min)(blockSize, size - offset);
            if (read(base + offset, block.data(), count)) {
                scan(base + offset, std::span(block.data(), count));
            } else {
                for (size_t part = 0; part < count;) {
                    if (cancelled()) return 0;
                    const size_t page = (std::min)(pageSize - ((base + offset + part) % pageSize), count - part);
                    if (read(base + offset + part, block.data(), page)) {
                        scan(base + offset + part, std::span(block.data(), page));
                        if (match != 0) break;
                    } else {
                        contiguous.clear();
                        if (unreadablePages) ++*unreadablePages;
                    }
                    part += page;
                }
            }
            if (match != 0) return cancelled() ? 0 : match;
            offset += count;
        }
        return 0;
    }
}
