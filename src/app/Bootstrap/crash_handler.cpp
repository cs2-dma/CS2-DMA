#include "app/Bootstrap/crash_handler.h"
#include "app/Platform/win_handle.h"

#include <Windows.h>
#include <DbgHelp.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <exception>
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

void BuildDumpPath(wchar_t (&buf)[MAX_PATH]) {
    wchar_t exePath[MAX_PATH] = {};
    const DWORD pathLength = GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    if (pathLength == 0 || pathLength >= MAX_PATH)
        exePath[0] = L'\0';
    else
        exePath[pathLength] = L'\0';

    wchar_t* lastSlash = wcsrchr(exePath, L'\\');
    if (lastSlash) lastSlash[1] = L'\0';
    else exePath[0] = L'\0';

    SYSTEMTIME st{};
    GetLocalTime(&st);

    swprintf_s(buf, MAX_PATH, L"%sKevqDMA-%04u%02u%02u-%02u%02u%02u.dmp",
               exePath, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
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
    std::string exceptionMsg = "std::terminate called without active exception details.";
    if (auto exPtr = std::current_exception()) {
        try {
            std::rethrow_exception(exPtr);
        } catch (const std::exception& e) {
            exceptionMsg = std::string("std::terminate called due to unhandled std::exception: ") + e.what();
        } catch (...) {
            exceptionMsg = "std::terminate called due to an unhandled non-standard exception.";
        }
    }
    bootstrap::WriteCrashLog("TERMINATE", exceptionMsg.c_str(), nullptr);
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
    g_previousFilter = SetUnhandledExceptionFilter(&UnhandledFilter);

    std::set_terminate(&TerminateHandler);
    _set_purecall_handler(&PureCallHandler);
    _set_invalid_parameter_handler(&InvalidParameterHandler);
    std::signal(SIGABRT, &AbortHandler);

    // Disable standard Windows crash dialog boxes for abort()
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
}

void WriteCrashLog(const char* type, const char* message, _EXCEPTION_POINTERS* info) {
    wchar_t dumpPath[MAX_PATH] = {};
    BuildDumpPath(dumpPath);

    bool dumpWritten = false;
    app::platform::UniqueWinHandle hFile(CreateFileW(dumpPath, GENERIC_WRITE, 0, nullptr,
                                                     CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
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

        char asciiPath[MAX_PATH] = {};
        WideCharToMultiByte(CP_UTF8, 0, dumpPath, -1, asciiPath, MAX_PATH, nullptr, nullptr);
        if (dumpWritten) {
            std::fprintf(stderr, "\n[KevqDMA] crashed (%s); minidump written to %s\n", type, asciiPath);
            std::fprintf(stderr, "  To analyze this dump: open the .dmp file in Visual Studio or WinDbg to inspect the native callstack.\n");
        } else {
            std::fprintf(stderr, "\n[KevqDMA] crashed (%s); failed to write minidump\n", type);
        }
    } else {
        std::fprintf(stderr, "\n[KevqDMA] crashed (%s); failed to write minidump\n", type);
    }

    wchar_t logPath[MAX_PATH] = {};
    wchar_t exePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    wchar_t* lastSlash = wcsrchr(exePath, L'\\');
    if (lastSlash) lastSlash[1] = L'\0';
    else exePath[0] = L'\0';
    swprintf_s(logPath, MAX_PATH, L"%sKevqDMA-crash.log", exePath);

    FILE* logFile = nullptr;
    if (_wfopen_s(&logFile, logPath, L"a") == 0 && logFile) {
        SYSTEMTIME st{};
        GetLocalTime(&st);
        std::fprintf(logFile, "[%04u-%02u-%02u %02u:%02u:%02u] KevqDMA crashed (%s)!\n",
                     st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, type);
        if (message && message[0] != '\0') {
            std::fprintf(logFile, "  Details: %s\n", message);
        }
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
            char asciiPath[MAX_PATH] = {};
            WideCharToMultiByte(CP_UTF8, 0, dumpPath, -1, asciiPath, MAX_PATH, nullptr, nullptr);
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

