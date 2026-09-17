#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <filesystem>
#include <system_error>

namespace app::platform
{
    inline bool RemoveFileIfExists(const std::filesystem::path& path)
    {
        if (path.empty())
            return false;

        std::error_code ec;
        const bool removed = std::filesystem::remove(path, ec);
        return removed && !ec;
    }

    inline bool ReplaceFileWithTemp(
        const std::filesystem::path& tempPath,
        const std::filesystem::path& finalPath,
        std::error_code& ec)
    {
        ec.clear();
        if (tempPath.empty() || finalPath.empty()) {
            ec = std::make_error_code(std::errc::invalid_argument);
            return false;
        }

        const std::wstring tempWide = tempPath.wstring();
        const std::wstring finalWide = finalPath.wstring();
        if (::MoveFileExW(
                tempWide.c_str(),
                finalWide.c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            return true;
        }

        ec = std::error_code(static_cast<int>(::GetLastError()), std::system_category());
        return false;
    }
}
