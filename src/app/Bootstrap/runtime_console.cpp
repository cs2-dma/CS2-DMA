#include "app/Core/build_info.h"
#include "app/Bootstrap/runtime_console.h"
#include "app/Localization/localization.h"
#include "app/Platform/console_text.h"

#include <Windows.h>
#include <DMALibrary/Memory/Memory.h>

#include <chrono>
#include <sstream>
#include <thread>

namespace
{
    const char* kColorReset = "\x1b[0m";
    const char* kColorYellow = "\x1b[93m";
    const char* kColorGreen = "\x1b[92m";
    const char* kColorRed = "\x1b[91m";
}

const char* bootstrap::RuntimeConsole::C(const char* colorCode) const
{
    return useAnsi_ ? colorCode : "";
}

void bootstrap::RuntimeConsole::Initialize(bool verboseLogs)
{
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    app::localization::Initialize();
    if (app::localization::GetLanguage() ==
        app::localization::Language::SimplifiedChinese) {
        app::platform::EnsureConsoleCjkRendering();
    }
    DmaSetLogTranslator(&app::localization::Get);
    DmaSetLogLevel(verboseLogs ? DmaLogLevel::Info : DmaLogLevel::Warning);
    EnableAnsiColors();
    SetConsoleTitleA(BuildRuntimeTitle().c_str());
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut != INVALID_HANDLE_VALUE) {
        CONSOLE_CURSOR_INFO ci = { 1, FALSE };
        SetConsoleCursorInfo(hOut, &ci);
    }
    EnableBestDpiAwareness();
}

void bootstrap::RuntimeConsole::PrintStartupBanner() const
{
    DmaConsoleWriteUtf8(
        "   ____  __            ________    \n"
        "  |    |/ _|_______  _\\_____  \\   \n"
        "  |      <_/ __ \\  \\/ //  / \\  \\  \n"
        "  |    |  \\  ___/\\   //   \\_/   \\ \n"
        "  |____|__ \\___  >\\_/ \\_____\\ \\_/ \n"
        "          \\/   \\/            \\__/ \n"
        "\n");
}

void bootstrap::RuntimeConsole::PrintBlankLine() const
{
    DmaConsoleWriteUtf8("\n");
}

void bootstrap::RuntimeConsole::AnimateForAtLeast(const std::string& text, int ms) const
{
    const uint64_t start = GetTickCount64();
    int phase = 0;
    while (GetTickCount64() - start < static_cast<uint64_t>(ms)) {
        PrintInfoPending(text, phase++);
        std::this_thread::sleep_for(std::chrono::milliseconds(90));
    }
}

void bootstrap::RuntimeConsole::PrintPending(const std::string& label, const std::string& text, int phase) const
{
    std::ostringstream line;
    line << "\r" << C(kColorYellow) << "  | "
         << app::localization::Get(label.c_str()) << " | " << C(kColorReset)
         << app::localization::Get(text.c_str()) << ' ' << DotPhase(phase)
         << "                    ";
    DmaConsoleWriteUtf8(line.str().c_str());
}

void bootstrap::RuntimeConsole::PrintOk(const std::string& label, const std::string& text) const
{
    std::ostringstream line;
    line << "\r" << C(kColorYellow) << "  | "
         << app::localization::Get(label.c_str()) << " | " << C(kColorReset)
         << app::localization::Get(text.c_str()) << " ["
         << C(kColorGreen) << "+" << C(kColorReset)
         << "]                    \n";
    DmaConsoleWriteUtf8(line.str().c_str());
}

void bootstrap::RuntimeConsole::PrintOk(const std::string& label, const std::string& text, int plusCount) const
{
    if (plusCount < 1)
        plusCount = 1;

    std::ostringstream line;
    line << "\r" << C(kColorYellow) << "  | "
         << app::localization::Get(label.c_str()) << " | " << C(kColorReset)
         << app::localization::Get(text.c_str()) << " [";
    for (int i = 0; i < plusCount; ++i) {
        if (i > 0)
            line << ' ';
        line << C(kColorGreen) << "+" << C(kColorReset);
    }
    line << "]                    \n";
    DmaConsoleWriteUtf8(line.str().c_str());
}

void bootstrap::RuntimeConsole::PrintFail(const std::string& label, const std::string& text) const
{
    std::ostringstream line;
    line << "\r" << C(kColorYellow) << "  | "
         << app::localization::Get(label.c_str()) << " | " << C(kColorReset)
         << app::localization::Get(text.c_str()) << " ["
         << C(kColorRed) << "-" << C(kColorReset)
         << "]                    \n";
    DmaConsoleWriteUtf8(line.str().c_str());
}

void bootstrap::RuntimeConsole::PrintLine(const std::string& label, const std::string& text) const
{
    std::ostringstream line;
    line << C(kColorYellow) << "  | "
         << app::localization::Get(label.c_str()) << " | " << C(kColorReset)
         << app::localization::Get(text.c_str()) << '\n';
    DmaConsoleWriteUtf8(line.str().c_str());
}

void bootstrap::RuntimeConsole::PrintMarkedLine(const std::string& label, const std::string& text) const
{
    std::ostringstream line;
    line << C(kColorYellow) << "  | "
         << app::localization::Get(label.c_str()) << " | " << C(kColorReset)
         << app::localization::Get(text.c_str()) << " ["
         << C(kColorGreen) << "*" << C(kColorReset) << "]\n";
    DmaConsoleWriteUtf8(line.str().c_str());
}

void bootstrap::RuntimeConsole::PrintInfoPending(const std::string& text, int phase) const
{
    PrintPending("Info", text, phase);
}

void bootstrap::RuntimeConsole::PrintInfoOk(const std::string& text) const
{
    PrintOk("Info", text);
}

void bootstrap::RuntimeConsole::PrintInfoOk(const std::string& text, int plusCount) const
{
    PrintOk("Info", text, plusCount);
}

void bootstrap::RuntimeConsole::PrintInfoFail(const std::string& text) const
{
    PrintFail("Info", text);
}

void bootstrap::RuntimeConsole::PrintInfoLine(const std::string& text) const
{
    PrintLine("Info", text);
}

void bootstrap::RuntimeConsole::PrintInfoMarkedLine(const std::string& text) const
{
    PrintMarkedLine("Info", text);
}

void bootstrap::RuntimeConsole::PrintErrorLine(const std::string& text) const
{
    std::ostringstream line;
    line << "\r" << C(kColorRed) << "  | "
         << app::localization::Get("Error") << " | " << C(kColorReset)
         << app::localization::Get(text.c_str()) << "                    \n";
    DmaConsoleWriteUtf8(line.str().c_str());
}

void bootstrap::RuntimeConsole::EnableBestDpiAwareness()
{
    HMODULE user32 = GetModuleHandleA("user32.dll");
    if (!user32)
        return;

    using SetProcessDpiAwarenessContextFn = BOOL(WINAPI*)(HANDLE);
    auto setDpiContext = reinterpret_cast<SetProcessDpiAwarenessContextFn>(
        GetProcAddress(user32, "SetProcessDpiAwarenessContext"));

    HANDLE perMonitorAwareV2 = reinterpret_cast<HANDLE>(-4);
    if (setDpiContext && setDpiContext(perMonitorAwareV2))
        return;

    SetProcessDPIAware();
}

std::string bootstrap::RuntimeConsole::BuildRuntimeTitle()
{
    return app::build_info::RuntimeTitle();
}

std::string bootstrap::RuntimeConsole::DotPhase(int phase)
{
    switch (phase % 5) {
    case 0: return ".    ";
    case 1: return ". .  ";
    case 2: return ". . .";
    case 3: return ". .  ";
    default: return ".    ";
    }
}

void bootstrap::RuntimeConsole::EnableAnsiColors()
{
    HANDLE outputHandle = GetStdHandle(STD_OUTPUT_HANDLE);
    if (outputHandle == INVALID_HANDLE_VALUE)
        return;

    DWORD mode = 0;
    if (!GetConsoleMode(outputHandle, &mode))
        return;

    mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    if (SetConsoleMode(outputHandle, mode))
        useAnsi_ = true;
}
