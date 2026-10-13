#pragma once

#include <string_view>
#include <cstdint>

namespace dma
{
    inline constexpr bool IsActiveProcessCandidate(uint32_t pid, uint32_t state) noexcept
    {
        return pid != 0 && state == 0;
    }

    template <typename Lookup, typename ReadHeader>
    bool HasReadableProcessAddressSpace(
        bool isCs2Target, std::string_view imageName, std::string_view imageBaseName,
        Lookup&& lookup, ReadHeader&& readHeader)
    {
        const auto probe = [&](auto base) { return base != 0 && readHeader(base); };
        if (isCs2Target) {
            const auto client = lookup("client.dll");
            const auto engine = lookup("engine2.dll");
            if (probe(client) || probe(engine))
                return true;
            if (client != 0 || engine != 0)
                return false;
        }
        return probe(lookup(imageName)) ||
            (imageBaseName != imageName && probe(lookup(imageBaseName)));
    }
}
