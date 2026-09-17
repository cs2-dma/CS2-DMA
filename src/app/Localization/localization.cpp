#include "app/Localization/localization.h"

#include "app/Config/user_state.h"
#include "app/Core/fallback_log.h"
#include "app/Localization/localization_catalog.h"
#include "app/Platform/console_text.h"

#include <Windows.h>

#include <array>
#include <atomic>
#include <exception>
#include <mutex>
#include <string>

namespace
{
    using app::localization::Catalog;
    using app::localization::Language;

    constexpr size_t kMaxCatalogBytes =
        size_t{2} * size_t{1024} * size_t{1024};
    constexpr wchar_t kEnglishResourceName[] = L"LOCALIZATION_EN_JSON";
    constexpr wchar_t kChineseResourceName[] = L"LOCALIZATION_ZH_CN_JSON";

    std::array<Catalog, 2> s_catalogs;
    std::atomic<const Catalog*> s_activeCatalog{nullptr};
    std::once_flag s_initializeOnce;

    constexpr size_t LanguageIndex(Language language) noexcept
    {
        return language == Language::SimplifiedChinese ? 1u : 0u;
    }

    Language LanguageForCatalog(const Catalog* catalog) noexcept
    {
        return catalog == &s_catalogs[LanguageIndex(Language::SimplifiedChinese)]
            ? Language::SimplifiedChinese
            : Language::English;
    }

    bool ReadCatalogResource(const wchar_t* resourceName, std::string* output)
    {
        if (!resourceName || !output)
            return false;

        const HMODULE module = GetModuleHandleW(nullptr);
        const HRSRC resource = FindResourceW(module, resourceName, RT_RCDATA);
        if (!resource)
            return false;

        const DWORD size = SizeofResource(module, resource);
        if (size == 0 || size > kMaxCatalogBytes)
            return false;

        const HGLOBAL loaded = LoadResource(module, resource);
        const void* data = loaded ? LockResource(loaded) : nullptr;
        if (!data)
            return false;

        output->assign(
            static_cast<const char*>(data),
            static_cast<size_t>(size));
        return true;
    }

    Catalog LoadCatalog(const wchar_t* resourceName)
    {
        Catalog result;
        std::string text;
        Catalog parsed;
        if (ReadCatalogResource(resourceName, &text) &&
            app::localization::ParseCatalogJson(text, &parsed)) {
            result = std::move(parsed);
        }
        return result;
    }

    Language LoadPersistedLanguage()
    {
        std::string code;
        if (app::user_state::LoadLanguageCode(&code) &&
            (code == "zh-CN" || code == "zh_CN" || code == "zh")) {
            return Language::SimplifiedChinese;
        }
        return Language::English;
    }
}

void app::localization::Initialize() noexcept
{
    try {
        std::call_once(s_initializeOnce, [] {
            try {
                s_catalogs[LanguageIndex(Language::English)] =
                    LoadCatalog(kEnglishResourceName);
                s_catalogs[LanguageIndex(Language::SimplifiedChinese)] =
                    LoadCatalog(kChineseResourceName);

                const Language language = LoadPersistedLanguage();
                if (language == Language::SimplifiedChinese)
                    app::platform::EnsureConsoleCjkRendering();
                s_activeCatalog.store(
                    &s_catalogs[LanguageIndex(language)],
                    std::memory_order_release);
            }
            catch (...) {
                s_activeCatalog.store(
                    &s_catalogs[LanguageIndex(Language::English)],
                    std::memory_order_release);
            }
        });
    }
    catch (...) {
        s_activeCatalog.store(
            &s_catalogs[LanguageIndex(Language::English)],
            std::memory_order_release);
    }
}

app::localization::Language app::localization::GetLanguage() noexcept
{
    if (!s_activeCatalog.load(std::memory_order_acquire))
        Initialize();
    return LanguageForCatalog(
        s_activeCatalog.load(std::memory_order_acquire));
}

void app::localization::SetLanguage(Language language, bool persist) noexcept
{
    Initialize();
    if (language == Language::SimplifiedChinese)
        app::platform::EnsureConsoleCjkRendering();
    s_activeCatalog.store(
        &s_catalogs[LanguageIndex(language)],
        std::memory_order_release);
    if (persist) {
        try {
            if (!app::user_state::SaveLanguageCode(LanguageCode(language))) {
                app::diagnostics::WriteFallbackError(
                    "Language preference could not be saved");
            }
        }
        catch (const std::exception& error) {
            app::diagnostics::WriteFallbackError(
                "Language preference could not be saved",
                error.what());
        }
        catch (...) {
            app::diagnostics::WriteFallbackError(
                "Language preference could not be saved",
                "unknown exception");
        }
    }
}

const char* app::localization::Get(const char* englishKey) noexcept
{
    if (!englishKey)
        return "";

    const Catalog* active = s_activeCatalog.load(std::memory_order_acquire);
    if (!active) {
        Initialize();
        active = s_activeCatalog.load(std::memory_order_acquire);
    }

    const Catalog& english = s_catalogs[LanguageIndex(Language::English)];
    return FindTranslation(active, &english, englishKey);
}

std::string app::localization::GetCopy(std::string_view englishKey)
{
    const std::string stableKey(englishKey);
    return Get(stableKey.c_str());
}

const char* app::localization::LanguageCode(Language language) noexcept
{
    return language == Language::SimplifiedChinese ? "zh-CN" : "en";
}

const char* app::localization::NativeLanguageName(Language language) noexcept
{
    return language == Language::SimplifiedChinese
        ? "\xE7\xAE\x80\xE4\xBD\x93\xE4\xB8\xAD\xE6\x96\x87"
        : "English";
}

std::string app::localization::CollectCatalogGlyphText()
{
    Initialize();
    std::string result;
    for (const Catalog& catalog : s_catalogs) {
        for (const auto& [key, value] : catalog.translations) {
            result.append(key);
            result.push_back('\n');
            result.append(value);
            result.push_back('\n');
        }
    }
    return result;
}
