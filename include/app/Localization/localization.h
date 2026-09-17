#pragma once

#include <format>
#include <string>
#include <string_view>

namespace app::localization
{
    enum class Language
    {
        English = 0,
        SimplifiedChinese = 1
    };

    void Initialize() noexcept;
    Language GetLanguage() noexcept;
    void SetLanguage(Language language, bool persist = true) noexcept;

    [[nodiscard]] const char* Get(const char* englishKey) noexcept;
    [[nodiscard]] std::string GetCopy(std::string_view englishKey);
    [[nodiscard]] const char* LanguageCode(Language language) noexcept;
    [[nodiscard]] const char* NativeLanguageName(Language language) noexcept;
    [[nodiscard]] std::string CollectCatalogGlyphText();

    template <typename... Args>
    [[nodiscard]] std::string Format(const char* englishKey, Args&&... args)
    {
        try {
            return std::vformat(
                Get(englishKey),
                std::make_format_args(args...));
        }
        catch (...) {
            return GetCopy(englishKey);
        }
    }
}

#define KEVQ_TR(text) ::app::localization::Get(text)
