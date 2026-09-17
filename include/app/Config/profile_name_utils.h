#pragma once

#include "app/Config/config_parse_utils.h"

#include <algorithm>
#include <string>
#include <string_view>

namespace app::profile_name
{
    inline constexpr size_t kMaxProfileNameLength = 64;
    inline constexpr std::string_view kDefaultProfileName = "KevqDefault";

    inline bool IsReservedProfileName(std::string_view name)
    {
        const std::string lowered = app::config_parse::ToLower(name);
        return lowered == "offsets" || lowered == "imgui" || lowered == "radar_maps" ||
               lowered == "webradarconfig" || lowered == "user_state" ||
               lowered == "active_profile" || lowered == "offsets_state" ||
               lowered.find(".web") != std::string::npos;
    }

    inline std::string SanitizeProfileName(std::string_view rawName)
    {
        std::string out;
        out.reserve(std::min(rawName.size(), kMaxProfileNameLength));

        for (char c : rawName) {
            if (out.size() >= kMaxProfileNameLength)
                break;
            const bool valid =
                (c >= 'a' && c <= 'z') ||
                (c >= 'A' && c <= 'Z') ||
                (c >= '0' && c <= '9') ||
                c == '_' || c == '-';
            out.push_back(valid ? c : '_');
        }

        while (!out.empty() && (out.back() == ' ' || out.back() == '.'))
            out.pop_back();

        if (out.empty())
            out = std::string(kDefaultProfileName);

        return out;
    }

    inline bool IsUsableProfileName(std::string_view rawName)
    {
        return !IsReservedProfileName(SanitizeProfileName(rawName));
    }
}
