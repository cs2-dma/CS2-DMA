#pragma once

#include <winsock2.h>

#include <cstddef>
#include <string>

namespace webradar::runtime
{
    std::string Base64Encode(const unsigned char* data, size_t size);
    bool SendAll(SOCKET socketHandle, const char* data, size_t size) noexcept;
}
