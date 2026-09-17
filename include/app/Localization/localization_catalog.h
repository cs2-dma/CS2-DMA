#pragma once

#include <json/json.hpp>

#include <string>
#include <string_view>
#include <unordered_map>

namespace app::localization
{
    struct TransparentStringHash
    {
        using is_transparent = void;

        size_t operator()(std::string_view value) const noexcept
        {
            return std::hash<std::string_view>{}(value);
        }
    };

    using TranslationMap = std::unordered_map<
        std::string,
        std::string,
        TransparentStringHash,
        std::equal_to<>>;

    struct Catalog
    {
        TranslationMap translations;
    };

    inline std::string PrintfArgumentSignature(std::string_view format)
    {
        constexpr std::string_view kFlags = "-+ #0'";
        constexpr std::string_view kConversions = "diuoxXfFeEgGaAcspn";
        std::string signature;

        for (size_t i = 0; i < format.size(); ++i) {
            if (format[i] != '%')
                continue;
            if (i + 1u < format.size() && format[i + 1u] == '%') {
                ++i;
                continue;
            }

            size_t cursor = i + 1u;
            size_t positionalCursor = cursor;
            while (positionalCursor < format.size() &&
                   format[positionalCursor] >= '0' &&
                   format[positionalCursor] <= '9') {
                ++positionalCursor;
            }
            if (positionalCursor < format.size() &&
                format[positionalCursor] == '$') {
                cursor = positionalCursor + 1u;
            }

            while (cursor < format.size() &&
                   kFlags.find(format[cursor]) != std::string_view::npos) {
                ++cursor;
            }

            int starArguments = 0;
            if (cursor < format.size() && format[cursor] == '*') {
                ++starArguments;
                ++cursor;
            } else {
                while (cursor < format.size() &&
                       format[cursor] >= '0' &&
                       format[cursor] <= '9') {
                    ++cursor;
                }
            }

            if (cursor < format.size() && format[cursor] == '.') {
                ++cursor;
                if (cursor < format.size() && format[cursor] == '*') {
                    ++starArguments;
                    ++cursor;
                } else {
                    while (cursor < format.size() &&
                           format[cursor] >= '0' &&
                           format[cursor] <= '9') {
                        ++cursor;
                    }
                }
            }

            std::string length;
            if (cursor + 2u < format.size() &&
                format.substr(cursor, 3u) == "I64") {
                length = "I64";
                cursor += 3u;
            } else if (cursor + 2u < format.size() &&
                       format.substr(cursor, 3u) == "I32") {
                length = "I32";
                cursor += 3u;
            } else if (cursor + 1u < format.size() &&
                       (format.substr(cursor, 2u) == "hh" ||
                        format.substr(cursor, 2u) == "ll")) {
                length.assign(format.substr(cursor, 2u));
                cursor += 2u;
            } else if (cursor < format.size() &&
                       std::string_view("hljztL").find(format[cursor]) !=
                           std::string_view::npos) {
                length.push_back(format[cursor]);
                ++cursor;
            }

            if (cursor >= format.size() ||
                kConversions.find(format[cursor]) == std::string_view::npos) {
                continue;
            }

            signature.push_back('|');
            signature.append(static_cast<size_t>(starArguments), '*');
            signature.append(length);
            signature.push_back(format[cursor]);
            i = cursor;
        }
        return signature;
    }

    inline size_t FormatReplacementCount(std::string_view format)
    {
        size_t count = 0;
        for (size_t i = 0; i < format.size(); ++i) {
            if (format[i] != '{')
                continue;
            if (i + 1u < format.size() && format[i + 1u] == '{') {
                ++i;
                continue;
            }
            ++count;
        }
        return count;
    }

    inline bool HasCompatibleFormatArguments(
        std::string_view source,
        std::string_view translation)
    {
        return PrintfArgumentSignature(source) ==
                   PrintfArgumentSignature(translation) &&
               FormatReplacementCount(source) ==
                   FormatReplacementCount(translation);
    }

    inline const char* FindTranslation(
        const Catalog* primary,
        const Catalog* fallback,
        const char* englishKey) noexcept
    {
        if (!englishKey)
            return "";
        if (primary) {
            const auto translated = primary->translations.find(englishKey);
            if (translated != primary->translations.end())
                return translated->second.c_str();
        }
        if (fallback) {
            const auto translated = fallback->translations.find(englishKey);
            if (translated != fallback->translations.end())
                return translated->second.c_str();
        }
        return englishKey;
    }

    inline bool ParseCatalogJson(std::string_view jsonText, Catalog* output) noexcept
    {
        if (!output || jsonText.empty())
            return false;

        try {
            const nlohmann::json root =
                nlohmann::json::parse(jsonText.begin(), jsonText.end(), nullptr, false);
            if (root.is_discarded() || !root.is_object())
                return false;

            const auto translationsIt = root.find("translations");
            if (translationsIt == root.end() || !translationsIt->is_object())
                return false;

            Catalog parsed;
            parsed.translations.reserve(translationsIt->size());
            for (auto it = translationsIt->begin(); it != translationsIt->end(); ++it) {
                if (!it.value().is_string() || it.key().empty())
                    return false;
                const std::string translated = it.value().get<std::string>();
                if (translated.empty() ||
                    !HasCompatibleFormatArguments(it.key(), translated)) {
                    return false;
                }
                parsed.translations.emplace(it.key(), translated);
            }

            if (parsed.translations.empty())
                return false;
            *output = std::move(parsed);
            return true;
        }
        catch (...) {
            return false;
        }
    }
}
