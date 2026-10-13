#include "app/Bootstrap/crash_handler.h"
#include "app/Platform/win_handle.h"
#include "app/Core/build_info.h"

#include <Windows.h>
#include <DbgHelp.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <exception>
#include <filesystem>
#include <csignal>
#include <string>


#pragma comment(lib, "Dbghelp.lib")

namespace {

std::atomic<bool> g_installed{false};
std::atomic_flag g_crashHandling = ATOMIC_FLAG_INIT;
LPTOP_LEVEL_EXCEPTION_FILTER g_previousFilter = nullptr;

bool BeginCrashHandling() noexcept {
    return !g_crashHandling.test_and_set(std::memory_order_acq_rel);
}

constexpr size_t kCrashPathCapacity = 32768;
wchar_t g_crashDirectories[2][kCrashPathCapacity] = {};
std::atomic<unsigned> g_dumpSequence{0};
char g_buildStamp[128] = {};

bool PrepareCrashDirectory(size_t index, const wchar_t* base)
{
    if (!base || !base[0] || index >= 2)
        return false;
    try {
        std::filesystem::path directory(base);
        directory /= L"KevqDMA";
        directory /= L"Crashes";
        std::wstring value = std::filesystem::absolute(directory).wstring();
        if (value.size() >= MAX_PATH && value.rfind(L"\\\\?\\", 0) != 0) {
            value = value.rfind(L"\\\\", 0) == 0
                ? L"\\\\?\\UNC\\" + value.substr(2)
                : L"\\\\?\\" + value;
            directory = value;
        }
        std::error_code error;
        std::filesystem::create_directories(directory, error);
        if (error)
            return false;
        if (value.size() + 128 >= kCrashPathCapacity)
            return false;
        return wcscpy_s(g_crashDirectories[index], value.c_str()) == 0;
    } catch (...) {
        return false;
    }
}

void PrepareCrashDirectories()
{
    wchar_t base[kCrashPathCapacity] = {};
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", base,
        static_cast<DWORD>(kCrashPathCapacity));
    if (length > 0 && length < kCrashPathCapacity)
        PrepareCrashDirectory(0, base);
    const DWORD tempLength = GetTempPathW(static_cast<DWORD>(kCrashPathCapacity), base);
    if (tempLength > 0 && tempLength < kCrashPathCapacity)
        PrepareCrashDirectory(1, base);
    try {
        std::snprintf(g_buildStamp, sizeof(g_buildStamp), "%s",
            app::build_info::DisplayStamp().c_str());
    } catch (...) {
        std::snprintf(g_buildStamp, sizeof(g_buildStamp), "%s %s", __DATE__, __TIME__);
    }
}

bool BuildDumpPath(wchar_t* buf, size_t capacity, size_t index)
{
    if (index >= 2 || !g_crashDirectories[index][0])
        return false;
    SYSTEMTIME st{};
    GetLocalTime(&st);
    return swprintf_s(buf, capacity,
        L"%s\\KevqDMA-%04u%02u%02u-%02u%02u%02u-%03u-%lu-%lu-%u.dmp",
        g_crashDirectories[index], st.wYear, st.wMonth, st.wDay,
        st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
        GetCurrentProcessId(), GetCurrentThreadId(),
        g_dumpSequence.fetch_add(1, std::memory_order_relaxed)) > 0;
}

LONG WINAPI UnhandledFilter(EXCEPTION_POINTERS* info) {
    if (!BeginCrashHandling())
        return EXCEPTION_EXECUTE_HANDLER;
    bootstrap::WriteCrashLog("STRUCTURED_EXCEPTION", nullptr, info);
    if (g_previousFilter)
        return g_previousFilter(info);
    return EXCEPTION_EXECUTE_HANDLER;
}

void TerminateHandler() {
    if (!BeginCrashHandling())
        std::_Exit(3);
    char exceptionMsg[1024] = "std::terminate called without active exception details.";
    const auto exPtr = std::current_exception();
    if (exPtr) {
        try {
            std::rethrow_exception(exPtr);
        } catch (const std::exception& e) {
            std::snprintf(exceptionMsg, sizeof(exceptionMsg), "Unhandled std::exception: %s", e.what());
        } catch (...) {
            std::snprintf(exceptionMsg, sizeof(exceptionMsg), "%s", "Unhandled non-standard exception.");
        }
    }
    bootstrap::WriteCrashLog("TERMINATE", exceptionMsg, nullptr);
    std::_Exit(3);
}

void PureCallHandler() {
    if (!BeginCrashHandling())
        std::_Exit(3);
    bootstrap::WriteCrashLog("PURECALL", "Pure virtual function call error detected.", nullptr);
    std::_Exit(3);
}

void InvalidParameterHandler(const wchar_t* expression, const wchar_t* function, const wchar_t* file, unsigned int line, uintptr_t pReserved) {
    (void)pReserved;
    if (!BeginCrashHandling())
        std::_Exit(3);
    char msg[1024];
    std::snprintf(msg, sizeof(msg), "Invalid parameter in function %ls (file %ls:%u): %ls",
                  function ? function : L"unknown",
                  file ? file : L"unknown",
                  line,
                  expression ? expression : L"unknown");
    bootstrap::WriteCrashLog("INVALID_PARAMETER", msg, nullptr);
    std::_Exit(3);
}

void AbortHandler(int sig) {
    if (!BeginCrashHandling())
        std::_Exit(3);
    char msg[64];
    std::snprintf(msg, sizeof(msg), "Abort signal caught (sig=%d).", sig);
    bootstrap::WriteCrashLog("ABORT_SIGNAL", msg, nullptr);
    std::_Exit(3);
}

}

namespace bootstrap {

void InstallCrashHandler() {
    bool expected = false;
    if (!g_installed.compare_exchange_strong(expected, true))
        return;
    PrepareCrashDirectories();
    g_previousFilter = SetUnhandledExceptionFilter(&UnhandledFilter);

    std::set_terminate(&TerminateHandler);
    _set_purecall_handler(&PureCallHandler);
    _set_invalid_parameter_handler(&InvalidParameterHandler);
    std::signal(SIGABRT, &AbortHandler);

    // Disable standard Windows crash dialog boxes for abort()
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
}

void WriteCrashLog(const char* type, const char* message, _EXCEPTION_POINTERS* info) {
    static wchar_t dumpPath[kCrashPathCapacity] = {};
    dumpPath[0] = L'\0';
    size_t directoryIndex = 0;

    bool dumpWritten = false;
    app::platform::UniqueWinHandle hFile;
    for (; directoryIndex < 2; ++directoryIndex) {
        if (!BuildDumpPath(dumpPath, kCrashPathCapacity, directoryIndex))
            continue;
        hFile.Reset(CreateFileW(dumpPath, GENERIC_WRITE, 0, nullptr,
            CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
        if (hFile)
            break;
    }
    if (hFile) {
        MINIDUMP_EXCEPTION_INFORMATION mei{};
        mei.ThreadId = GetCurrentThreadId();
        mei.ExceptionPointers = reinterpret_cast<EXCEPTION_POINTERS*>(info);
        mei.ClientPointers = FALSE;

        const MINIDUMP_TYPE dumpType = static_cast<MINIDUMP_TYPE>(
            MiniDumpWithThreadInfo |
            MiniDumpWithIndirectlyReferencedMemory |
            MiniDumpScanMemory);

        if (MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(),
                               hFile.Get(), dumpType, info ? &mei : nullptr, nullptr, nullptr)) {
            dumpWritten = true;
        }

        static char asciiPath[kCrashPathCapacity * 4] = {};
        WideCharToMultiByte(CP_UTF8, 0, dumpPath, -1, asciiPath,
            static_cast<int>(sizeof(asciiPath)), nullptr, nullptr);
        if (dumpWritten) {
            std::fprintf(stderr, "\n[KevqDMA] crashed (%s); minidump written to %s\n", type, asciiPath);
            std::fprintf(stderr, "  To analyze this dump: open the .dmp file in Visual Studio or WinDbg to inspect the native callstack.\n");
        } else {
            std::fprintf(stderr, "\n[KevqDMA] crashed (%s); failed to write minidump\n", type);
        }
    } else {
        std::fprintf(stderr, "\n[KevqDMA] crashed (%s); failed to write minidump\n", type);
    }

    static wchar_t logPath[kCrashPathCapacity] = {};
    FILE* logFile = nullptr;
    for (size_t attempt = 0; attempt < 2; ++attempt) {
        const size_t index = directoryIndex < 2 ? (directoryIndex + attempt) % 2 : attempt;
        if (!g_crashDirectories[index][0])
            continue;
        if (swprintf_s(logPath, kCrashPathCapacity, L"%s\\KevqDMA-crash.log",
                g_crashDirectories[index]) < 0)
            continue;
        if (_wfopen_s(&logFile, logPath, L"a") == 0 && logFile)
            break;
    }
    if (logFile) {
        SYSTEMTIME st{};
        GetLocalTime(&st);
        std::fprintf(logFile, "[%04u-%02u-%02u %02u:%02u:%02u] KevqDMA crashed (%s)!\n",
                     st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, type);
        if (message && message[0] != '\0') {
            std::fprintf(logFile, "  Details: %s\n", message);
        }
        std::fprintf(logFile, "  Version: %s; build: %s; pid: %lu; tid: %lu\n",
            KEVQDMA_VERSION_TAG, g_buildStamp, GetCurrentProcessId(), GetCurrentThreadId());
        if (info && info->ExceptionRecord) {
            std::fprintf(logFile, "  Exception code: 0x%08X\n", info->ExceptionRecord->ExceptionCode);
            std::fprintf(logFile, "  Exception address: 0x%p\n", info->ExceptionRecord->ExceptionAddress);
            if (info->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && info->ExceptionRecord->NumberParameters >= 2) {
                const ULONG_PTR operation =
                    info->ExceptionRecord->ExceptionInformation[0];
                const char* operationName =
                    operation == 0 ? "READ" :
                    operation == 1 ? "WRITE" :
                    operation == 8 ? "EXECUTE" :
                    "UNKNOWN";
                std::fprintf(logFile, "  Access violation: %s at address 0x%llX\n",
                             operationName,
                             static_cast<unsigned long long>(
                                 info->ExceptionRecord->ExceptionInformation[1]));
            }
        }
        if (dumpWritten) {
            static char asciiPath[kCrashPathCapacity * 4] = {};
            WideCharToMultiByte(CP_UTF8, 0, dumpPath, -1, asciiPath,
                static_cast<int>(sizeof(asciiPath)), nullptr, nullptr);
            std::fprintf(logFile, "  Minidump written to: %s\n", asciiPath);
            std::fprintf(logFile, "  Analysis: Open the .dmp file in Visual Studio or WinDbg to load symbols and inspect the callstack.\n");
        } else {
            std::fprintf(logFile, "  Failed to write minidump.\n");
        }
        std::fprintf(logFile, "--------------------------------------------------\n");
        std::fclose(logFile);
    }
}

}

