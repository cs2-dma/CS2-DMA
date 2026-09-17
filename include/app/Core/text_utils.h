#pragma once

#include <cctype>
#include <string>
#include <string_view>

namespace app::text
{
    inline std::string Trim(std::string_view value)
    {
        size_t begin = 0;
        size_t end = value.size();

        while (begin < end &&
               std::isspace(static_cast<unsigned char>(value[begin])) != 0) {
            ++begin;
        }
        while (end > begin &&
               std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
            --end;
        }

        return std::string(value.substr(begin, end - begin));
    }

    inline std::string ToLowerAscii(std::string_view value)
    {
        std::string lowered;
        lowered.reserve(value.size());
        for (const char ch : value) {
            if (ch >= 'A' && ch <= 'Z')
                lowered.push_back(static_cast<char>(ch - 'A' + 'a'));
            else
                lowered.push_back(ch);
        }
        return lowered;
    }
}
