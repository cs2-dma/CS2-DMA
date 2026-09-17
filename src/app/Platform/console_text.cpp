#include "app/Platform/console_text.h"

#include <Windows.h>

#include <array>
#include <atomic>
#include <cwchar>

namespace
{
    bool FontContainsCjkGlyphs(const CONSOLE_FONT_INFOEX& consoleFont) noexcept
    {
        HDC dc = CreateCompatibleDC(nullptr);
        if (!dc)
            return false;

        LOGFONTW font = {};
        font.lfHeight = consoleFont.dwFontSize.Y > 0
            ? -static_cast<LONG>(consoleFont.dwFontSize.Y)
            : -16;
        font.lfWeight = consoleFont.FontWeight > 0
            ? static_cast<LONG>(consoleFont.FontWeight)
            : FW_NORMAL;
        font.lfPitchAndFamily =
            static_cast<BYTE>(consoleFont.FontFamily);
        wcsncpy_s(
            font.lfFaceName,
            _countof(font.lfFaceName),
            consoleFont.FaceName,
            _TRUNCATE);

        HFONT selectedFont = CreateFontIndirectW(&font);
        if (!selectedFont) {
            DeleteDC(dc);
            return false;
        }

        HGDIOBJ previousFont = SelectObject(dc, selectedFont);
        constexpr wchar_t kProbe[] = L"\x7B80\x4E2D";
        std::array<WORD, 2> glyphs = {};
        const DWORD glyphCount = GetGlyphIndicesW(
            dc,
            kProbe,
            static_cast<int>(glyphs.size()),
            glyphs.data(),
            GGI_MARK_NONEXISTING_GLYPHS);
        if (previousFont)
            SelectObject(dc, previousFont);
        DeleteObject(selectedFont);
        DeleteDC(dc);

        return glyphCount != GDI_ERROR &&
               glyphs[0] != 0xFFFFu &&
               glyphs[1] != 0xFFFFu;
    }
}

void app::platform::EnsureConsoleCjkRendering() noexcept
{
    static std::atomic<bool> configured{false};
    if (configured.load(std::memory_order_acquire))
        return;

    const HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (!output ||
        output == INVALID_HANDLE_VALUE ||
        !GetConsoleMode(output, &mode)) {
        return;
    }

    CONSOLE_FONT_INFOEX original = {};
    original.cbSize = sizeof(original);
    if (!GetCurrentConsoleFontEx(output, FALSE, &original))
        return;

    if (FontContainsCjkGlyphs(original)) {
        configured.store(true, std::memory_order_release);
        return;
    }

    constexpr std::array<const wchar_t*, 3> kCjkConsoleFonts = {
        L"NSimSun",
        L"Microsoft YaHei UI",
        L"MS Gothic"
    };
    for (const wchar_t* faceName : kCjkConsoleFonts) {
        CONSOLE_FONT_INFOEX candidate = original;
        candidate.FontFamily = FF_MODERN | TMPF_TRUETYPE;
        candidate.FontWeight = FW_NORMAL;
        wcsncpy_s(
            candidate.FaceName,
            _countof(candidate.FaceName),
            faceName,
            _TRUNCATE);
        if (!SetCurrentConsoleFontEx(output, FALSE, &candidate))
            continue;

        CONSOLE_FONT_INFOEX applied = {};
        applied.cbSize = sizeof(applied);
        if (GetCurrentConsoleFontEx(output, FALSE, &applied) &&
            FontContainsCjkGlyphs(applied)) {
            configured.store(true, std::memory_order_release);
            return;
        }
    }

    SetCurrentConsoleFontEx(output, FALSE, &original);
}
