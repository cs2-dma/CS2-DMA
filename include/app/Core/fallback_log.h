#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <cstdio>
#include <cstring>

namespace app::diagnostics
{
    inline void WriteFallbackError(
        const char* message,
        const char* detail = nullptr) noexcept
    {
        char line[4096] = {};
        const char* const safeMessage =
            message ? message : "KevqDMA fallback error";
        if (detail && *detail) {
            std::snprintf(
                line,
                sizeof(line),
                "%s: %s\n",
                safeMessage,
                detail);
        } else {
            std::snprintf(line, sizeof(line), "%s\n", safeMessage);
        }

        const size_t byteCount = std::strlen(line);
        const HANDLE errorHandle = ::GetStdHandle(STD_ERROR_HANDLE);
        DWORD consoleMode = 0;
        if (errorHandle != nullptr &&
            errorHandle != INVALID_HANDLE_VALUE &&
            ::GetConsoleMode(errorHandle, &consoleMode)) {
            wchar_t wideLine[4096] = {};
            const int wideCount = ::MultiByteToWideChar(
                CP_UTF8,
                MB_ERR_INVALID_CHARS,
                line,
                static_cast<int>(byteCount),
                wideLine,
                static_cast<int>(_countof(wideLine)));
            if (wideCount > 0) {
                DWORD written = 0;
                if (::WriteConsoleW(
                        errorHandle,
                        wideLine,
                        static_cast<DWORD>(wideCount),
                        &written,
                        nullptr)) {
                    return;
                }
            }
        }

        std::fwrite(line, 1, byteCount, stderr);
        std::fflush(stderr);
    }
}
