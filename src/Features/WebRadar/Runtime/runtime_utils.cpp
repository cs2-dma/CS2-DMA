#include "Features/WebRadar/runtime_utils.h"

#include <algorithm>
#include <cstdint>
#include <limits>

std::string webradar::runtime::Base64Encode(
    const unsigned char* data,
    size_t size)
{
    constexpr char kAlphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789+/";

    if (!data || size == 0)
        return {};
    if (size > (std::numeric_limits<size_t>::max() - 2u) / 4u * 3u)
        return {};

    std::string output;
    output.reserve(((size + 2u) / 3u) * 4u);
    for (size_t offset = 0; offset < size; offset += 3u) {
        const uint32_t first = data[offset];
        const uint32_t second = offset + 1u < size ? data[offset + 1u] : 0u;
        const uint32_t third = offset + 2u < size ? data[offset + 2u] : 0u;
        const uint32_t value = (first << 16u) | (second << 8u) | third;

        output.push_back(kAlphabet[(value >> 18u) & 0x3Fu]);
        output.push_back(kAlphabet[(value >> 12u) & 0x3Fu]);
        output.push_back(
            offset + 1u < size ? kAlphabet[(value >> 6u) & 0x3Fu] : '=');
        output.push_back(
            offset + 2u < size ? kAlphabet[value & 0x3Fu] : '=');
    }
    return output;
}

bool webradar::runtime::SendAll(
    SOCKET socketHandle,
    const char* data,
    size_t size) noexcept
{
    if (socketHandle == INVALID_SOCKET || (!data && size != 0))
        return false;

    constexpr size_t kMaxSendChunk = size_t{64} * 1024u;
    size_t sentBytes = 0;
    while (sentBytes < size) {
        const int chunkSize = static_cast<int>(
            std::min(size - sentBytes, kMaxSendChunk));
        const int result = send(socketHandle, data + sentBytes, chunkSize, 0);
        if (result <= 0)
            return false;
        sentBytes += static_cast<size_t>(result);
    }
    return true;
}
