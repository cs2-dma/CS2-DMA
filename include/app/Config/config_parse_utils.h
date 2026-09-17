#pragma once

#include "app/Core/text_utils.h"

#include <charconv>
#include <cmath>
#include <string_view>
#include <system_error>

namespace app::config_parse
{
    inline std::string ToLower(std::string_view value)
    {
        return app::text::ToLowerAscii(value);
    }

    using app::text::Trim;

    inline bool ParseBoolString(std::string_view value, bool fallback)
    {
        const std::string lowered = ToLower(Trim(value));
        if (lowered == "1" || lowered == "true" || lowered == "yes" || lowered == "on")
            return true;
        if (lowered == "0" || lowered == "false" || lowered == "no" || lowered == "off")
            return false;
        return fallback;
    }

    inline int ParseIntString(std::string_view value, int fallback)
    {
        const std::string trimmed = Trim(value);
        int parsed = 0;
        const char* begin = trimmed.data();
        const char* end = begin + trimmed.size();
        if (begin != end && *begin == '+')
            ++begin;
        const auto result = std::from_chars(
            begin,
            end,
            parsed);
        return result.ec == std::errc{} &&
               result.ptr != begin
            ? parsed
            : fallback;
    }

    inline float ParseFloatString(std::string_view value, float fallback)
    {
        const std::string trimmed = Trim(value);
        float parsed = 0.0f;
        const char* begin = trimmed.data();
        const char* end = begin + trimmed.size();
        if (begin != end && *begin == '+')
            ++begin;
        const auto result = std::from_chars(
            begin,
            end,
            parsed,
            std::chars_format::general);
        return result.ec == std::errc{} &&
               result.ptr != begin &&
               std::isfinite(parsed)
            ? parsed
            : fallback;
    }
}
