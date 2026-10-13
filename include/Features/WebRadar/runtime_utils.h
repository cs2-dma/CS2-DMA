#pragma once

#include <winsock2.h>

#include <cstddef>
#include <cstdint>
#include <string>

namespace webradar::runtime
{
    inline bool IsSnapshotReceiptFresh(uint64_t receivedMs, uint64_t nowMs) noexcept
    {
        return receivedMs > 0 && nowMs >= receivedMs && nowMs - receivedMs <= 500u;
    }

    std::string Base64Encode(const unsigned char* data, size_t size);
    bool SendAll(SOCKET socketHandle, const char* data, size_t size) noexcept;
}
