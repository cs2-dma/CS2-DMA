#pragma once

#include <Windows.h>
#include <winhttp.h>

#include <utility>

namespace app::platform
{
    class UniqueWinHttpHandle
    {
    public:
        UniqueWinHttpHandle() noexcept = default;
        explicit UniqueWinHttpHandle(HINTERNET value) noexcept
            : value_(value)
        {
        }

        ~UniqueWinHttpHandle()
        {
            Reset();
        }

        UniqueWinHttpHandle(const UniqueWinHttpHandle&) = delete;
        UniqueWinHttpHandle& operator=(const UniqueWinHttpHandle&) = delete;

        UniqueWinHttpHandle(UniqueWinHttpHandle&& other) noexcept
            : value_(std::exchange(other.value_, nullptr))
        {
        }

        UniqueWinHttpHandle& operator=(UniqueWinHttpHandle&& other) noexcept
        {
            if (this != &other)
                Reset(std::exchange(other.value_, nullptr));
            return *this;
        }

        HINTERNET Get() const noexcept
        {
            return value_;
        }

        void Reset(HINTERNET value = nullptr) noexcept
        {
            if (value_)
                WinHttpCloseHandle(value_);
            value_ = value;
        }

        explicit operator bool() const noexcept
        {
            return value_ != nullptr;
        }

    private:
        HINTERNET value_ = nullptr;
    };

    class UniqueWinHandle
    {
    public:
        UniqueWinHandle() noexcept = default;
        explicit UniqueWinHandle(HANDLE value) noexcept
            : value_(value)
        {
        }

        ~UniqueWinHandle()
        {
            Reset();
        }

        UniqueWinHandle(const UniqueWinHandle&) = delete;
        UniqueWinHandle& operator=(const UniqueWinHandle&) = delete;

        UniqueWinHandle(UniqueWinHandle&& other) noexcept
            : value_(std::exchange(other.value_, INVALID_HANDLE_VALUE))
        {
        }

        UniqueWinHandle& operator=(UniqueWinHandle&& other) noexcept
        {
            if (this != &other)
                Reset(std::exchange(other.value_, INVALID_HANDLE_VALUE));
            return *this;
        }

        HANDLE Get() const noexcept
        {
            return value_;
        }

        void Reset(HANDLE value = INVALID_HANDLE_VALUE) noexcept
        {
            if (IsValid(value_))
                CloseHandle(value_);
            value_ = value;
        }

        explicit operator bool() const noexcept
        {
            return IsValid(value_);
        }

    private:
        static bool IsValid(HANDLE value) noexcept
        {
            return value != nullptr && value != INVALID_HANDLE_VALUE;
        }

        HANDLE value_ = INVALID_HANDLE_VALUE;
    };

    class UniqueRegKey
    {
    public:
        UniqueRegKey() noexcept = default;
        explicit UniqueRegKey(HKEY value) noexcept
            : value_(value)
        {
        }

        ~UniqueRegKey()
        {
            Reset();
        }

        UniqueRegKey(const UniqueRegKey&) = delete;
        UniqueRegKey& operator=(const UniqueRegKey&) = delete;

        UniqueRegKey(UniqueRegKey&& other) noexcept
            : value_(std::exchange(other.value_, nullptr))
        {
        }

        UniqueRegKey& operator=(UniqueRegKey&& other) noexcept
        {
            if (this != &other)
                Reset(std::exchange(other.value_, nullptr));
            return *this;
        }

        HKEY Get() const noexcept
        {
            return value_;
        }

        HKEY* Put() noexcept
        {
            Reset();
            return &value_;
        }

        void Reset(HKEY value = nullptr) noexcept
        {
            if (value_)
                RegCloseKey(value_);
            value_ = value;
        }

        explicit operator bool() const noexcept
        {
            return value_ != nullptr;
        }

    private:
        HKEY value_ = nullptr;
    };
}
