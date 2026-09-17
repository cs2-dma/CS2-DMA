#pragma once

#include "app/Core/text_utils.h"

#include <chrono>
#include <cstdint>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

namespace runtime_offsets::parse_utils
{
    using app::text::Trim;

    inline std::string ToHex(std::ptrdiff_t value)
    {
        std::ostringstream oss;
        oss << "0x" << std::uppercase << std::hex << static_cast<std::uint64_t>(value);
        return oss.str();
    }

    inline std::string ToAddressHex(std::uintptr_t value)
    {
        std::ostringstream oss;
        oss << "0x" << std::uppercase << std::hex << value;
        return oss.str();
    }

    inline bool TryParseOffset(std::string_view rawText, std::ptrdiff_t& out)
    {
        const std::string text = Trim(rawText);
        if (text.empty())
            return false;

        int base = 10;
        if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))
            base = 16;

        try {
            size_t parsedChars = 0;
            const long long parsed = std::stoll(text, &parsedChars, base);
            if (parsedChars != text.size())
                return false;
            out = static_cast<std::ptrdiff_t>(parsed);
            return true;
        } catch (...) {
            return false;
        }
    }

    inline int ParsePatchBuildNumber(std::string_view patchVersion)
    {
        std::string digits;
        digits.reserve(patchVersion.size());
        for (const char ch : patchVersion) {
            if (ch >= '0' && ch <= '9')
                digits.push_back(ch);
        }

        if (digits.empty())
            return 0;

        try {
            return std::stoi(digits);
        } catch (...) {
            return 0;
        }
    }

    inline int ParseDecimalPart(
        std::string_view text,
        size_t start,
        size_t length) noexcept
    {
        if (length == 0 || start > text.size() || length > text.size() - start)
            return -1;

        int value = 0;
        for (size_t i = 0; i < length; ++i) {
            const char ch = text[start + i];
            if (ch < '0' || ch > '9')
                return -1;
            value = (value * 10) + (ch - '0');
        }
        return value;
    }

    inline std::optional<std::chrono::sys_seconds> ParseCanonicalUtcTimestamp(std::string_view canonical)
    {
        if (canonical.size() < 14)
            return std::nullopt;

        const int year = ParseDecimalPart(canonical, 0, 4);
        const int month = ParseDecimalPart(canonical, 4, 2);
        const int day = ParseDecimalPart(canonical, 6, 2);
        const int hour = ParseDecimalPart(canonical, 8, 2);
        const int minute = ParseDecimalPart(canonical, 10, 2);
        const int second = ParseDecimalPart(canonical, 12, 2);
        if (year < 1970 ||
            month <= 0 ||
            day <= 0 ||
            hour < 0 || hour > 23 ||
            minute < 0 || minute > 59 ||
            second < 0 || second > 59) {
            return std::nullopt;
        }

        const std::chrono::year_month_day ymd {
            std::chrono::year { year },
            std::chrono::month { static_cast<unsigned int>(month) },
            std::chrono::day { static_cast<unsigned int>(day) }
        };
        if (!ymd.ok())
            return std::nullopt;

        return std::chrono::sys_days { ymd } +
               std::chrono::hours { hour } +
               std::chrono::minutes { minute } +
               std::chrono::seconds { second };
    }

    inline std::optional<std::chrono::sys_seconds> ParseSteamVersionTimestamp(
        std::string_view versionDate,
        std::string_view versionTime)
    {
        static constexpr std::pair<std::string_view, unsigned int> kMonths[] = {
            { "Jan", 1 }, { "Feb", 2 }, { "Mar", 3 }, { "Apr", 4 },
            { "May", 5 }, { "Jun", 6 }, { "Jul", 7 }, { "Aug", 8 },
            { "Sep", 9 }, { "Oct", 10 }, { "Nov", 11 }, { "Dec", 12 }
        };

        std::istringstream dateStream { std::string(versionDate) };
        std::string monthToken;
        int day = 0;
        int year = 0;
        if (!(dateStream >> monthToken >> day >> year))
            return std::nullopt;

        unsigned int month = 0;
        for (const auto& [name, value] : kMonths) {
            if (monthToken == name) {
                month = value;
                break;
            }
        }
        if (month == 0)
            return std::nullopt;

        int hour = 0;
        int minute = 0;
        int second = 0;
        char colon1 = '\0';
        char colon2 = '\0';
        std::istringstream timeStream { std::string(versionTime) };
        if (!(timeStream >> hour >> colon1 >> minute >> colon2 >> second) ||
            colon1 != ':' ||
            colon2 != ':' ||
            hour < 0 || hour > 23 ||
            minute < 0 || minute > 59 ||
            second < 0 || second > 59) {
            return std::nullopt;
        }

        const std::chrono::year_month_day ymd {
            std::chrono::year { year },
            std::chrono::month { month },
            std::chrono::day { static_cast<unsigned int>(day) }
        };
        if (!ymd.ok())
            return std::nullopt;

        return std::chrono::sys_days { ymd } +
               std::chrono::hours { hour } +
               std::chrono::minutes { minute } +
               std::chrono::seconds { second };
    }
}
