#pragma warning(push)
#pragma warning(disable: 4200 4201)
#include "DMALibrary/pch.h"
#pragma warning(pop)

#include "../../../src/app/Input/primary_keyboard.cpp"
#include <iostream>

namespace fixture
{
    std::shared_timed_mutex lifecycle;
    std::atomic<bool> sessionValid{true};
    std::atomic<bool> processesReady{false};
    std::atomic<bool> bitmapReadable{true};
    std::atomic<bool> blockEnumeration{false};
    std::atomic<bool> enumerationEntered{false};
    std::atomic<int> reads{0};
    std::atomic<int> failures{0};
    std::atomic<uint8_t> firstByte{0};
    constexpr uintptr_t bitmap = 0xFFFFF80000300000ULL;
    constexpr uintptr_t module = 0xFFFFF80000500000ULL;
    std::vector<uint8_t> moduleBytes(32768, 0xCC);
    int missingPage = -1;
    void Check(bool ok, const char* text, int line)
    {
        if (!ok) { ++failures; std::cerr << line << ": " << text << '\n'; }
    }
    template <typename Predicate>
    bool Wait(Predicate&& ready)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (!ready() && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        return ready();
    }
}
#define CHECK(value) fixture::Check((value), #value, __LINE__)

Memory::~Memory() = default;
esp::recovery::DmaReadSession::DmaReadSession() : lock_(fixture::lifecycle, std::try_to_lock) {}
bool esp::recovery::DmaReadSession::Valid() const noexcept
{
    return lock_.owns_lock() && fixture::sessionValid.load();
}

BOOL VMMDLL_ProcessGetInformationAll(VMM_HANDLE, PVMMDLL_PROCESS_INFORMATION* output, PDWORD count)
{
    fixture::enumerationEntered.store(true);
    while (fixture::blockEnumeration.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    if (!fixture::processesReady.load()) return FALSE;
    *count = 1;
    *output = static_cast<PVMMDLL_PROCESS_INFORMATION>(std::calloc(1, sizeof(VMMDLL_PROCESS_INFORMATION)));
    (*output)->dwPID = 100;
    (*output)->win.dwSessionId = 1;
    strcpy_s((*output)->szName, "winlogon.exe");
    return TRUE;
}

BOOL VMMDLL_Map_GetModuleFromNameW(VMM_HANDLE, DWORD, LPCWSTR, PVMMDLL_MAP_MODULEENTRY*, DWORD)
{
    return FALSE;
}

BOOL VMMDLL_Map_GetEATU(VMM_HANDLE, DWORD pid, LPCSTR, PVMMDLL_MAP_EAT* output)
{
    CHECK((pid & VMMDLL_PID_PROCESS_WITH_KERNELMEMORY) != 0);
    *output = static_cast<PVMMDLL_MAP_EAT>(std::calloc(1, sizeof(VMMDLL_MAP_EAT) + sizeof(VMMDLL_MAP_EATENTRY)));
    (*output)->cMap = 1;
    static char name[] = "gafAsyncKeyState";
    (*output)->pMap[0].uszFunction = name;
    (*output)->pMap[0].vaFunction = fixture::bitmap;
    return TRUE;
}

VOID VMMDLL_MemFree(PVOID memory) { std::free(memory); }

BOOL VMMDLL_MemReadEx(VMM_HANDLE, DWORD pid, ULONG64 address, PBYTE out, DWORD size, PDWORD completed, ULONG64 flags)
{
    ++fixture::reads;
    CHECK((pid & VMMDLL_PID_PROCESS_WITH_KERNELMEMORY) != 0);
    CHECK((flags & VMMDLL_FLAG_ZEROPAD_ON_FAIL) == 0);
    *completed = 0;
    if (address == fixture::bitmap && size == 64 && fixture::bitmapReadable.load()) {
        std::memset(out, 0, size);
        out[0] = fixture::firstByte.load();
        *completed = size;
        return TRUE;
    }
    if (address >= fixture::module && address - fixture::module < fixture::moduleBytes.size()) {
        const size_t offset = static_cast<size_t>(address - fixture::module);
        if (size <= fixture::moduleBytes.size() - offset &&
            !(fixture::missingPage >= 0 && offset < (static_cast<size_t>(fixture::missingPage) + 1) * 4096 &&
                offset + size > static_cast<size_t>(fixture::missingPage) * 4096)) {
            std::memcpy(out, fixture::moduleBytes.data() + offset, size);
            *completed = size;
            return TRUE;
        }
    }
    std::memset(out, 0, size);
    return FALSE;
}

int main()
{
    using namespace app::input;
    mem.vHandle = reinterpret_cast<VMM_HANDLE>(uintptr_t{1});
    CHECK(!InitializePrimaryKeyboard());
    CHECK(!GetPrimaryKeyboardStatus().ready && !ReadPrimaryKeyState(2).available);
    fixture::processesReady.store(true);
    StartPrimaryKeyboardRecovery();
    CHECK(fixture::Wait([] { return GetPrimaryKeyboardStatus().ready; }));
    StopPrimaryKeyboardRecovery();
    CHECK(GetPrimaryKeyboardStatus().sourcePid == 100);
    fixture::firstByte.store(0x10);
    CHECK(PollPrimaryKeyboard());
    CHECK(ReadPrimaryKeyState(2).available && ReadPrimaryKeyState(2).down);
    fixture::bitmapReadable.store(false);
    CHECK(!PollPrimaryKeyboard());
    CHECK(!ReadPrimaryKeyState(2).available);
    CHECK(GetPrimaryKeyboardStatus().consecutiveReadFailures == 1);
    fixture::bitmapReadable.store(true);
    fixture::firstByte.store(0);
    CHECK(PollPrimaryKeyboard());
    CHECK(ReadPrimaryKeyState(2).available && !ReadPrimaryKeyState(2).down);

    s_readFailures.store(10);
    s_failedSinceMs.store(GetTickCount64() - 501);
    const auto before = GetPrimaryKeyboardStatus().resolveAttempts;
    StartPrimaryKeyboardRecovery();
    CHECK(fixture::Wait([&] {
        const auto status = GetPrimaryKeyboardStatus();
        return status.resolveAttempts > before && status.ready && !status.resolving;
    }));
    StopPrimaryKeyboardRecovery();
    CHECK(GetPrimaryKeyboardStatus().ready && PollPrimaryKeyboard());

    ResetPrimaryKeyboard();
    fixture::blockEnumeration.store(true);
    fixture::enumerationEntered.store(false);
    StartPrimaryKeyboardRecovery();
    CHECK(fixture::Wait([] { return fixture::enumerationEntered.load(); }));
    const auto pollStarted = std::chrono::steady_clock::now();
    CHECK(!PollPrimaryKeyboard());
    CHECK(std::chrono::steady_clock::now() - pollStarted < std::chrono::milliseconds(100));
    fixture::sessionValid.store(false);
    fixture::blockEnumeration.store(false);
    StopPrimaryKeyboardRecovery();
    CHECK(!GetPrimaryKeyboardStatus().ready && !ReadPrimaryKeyState(2).available);
    fixture::sessionValid.store(true);
    StartPrimaryKeyboardRecovery();
    CHECK(fixture::Wait([] { return GetPrimaryKeyboardStatus().ready; }));
    StopPrimaryKeyboardRecovery();

    const std::array<uint8_t, 5> pattern{0x48, 0x8B, 0x11, 0x22, 0xEE};
    std::copy(pattern.begin(), pattern.end(), fixture::moduleBytes.begin() + 16382);
    fixture::missingPage = 0;
    CHECK(ScanModule(100, fixture::module, static_cast<DWORD>(fixture::moduleBytes.size()), "48 8B ? 22 EE") == fixture::module + 16382);
    fixture::missingPage = 4;
    CHECK(ScanModule(100, fixture::module, static_cast<DWORD>(fixture::moduleBytes.size()), "48 8B ? 22 EE") == 0);
    ResetPrimaryKeyboard();
    mem.vHandle = nullptr;
    if (fixture::failures.load() != 0) return 1;
    std::cout << "Primary keyboard tests passed: startup recovery, read failure, re-resolution, nonblocking poll, cancellation, restart and partial-page scan.\n";
}
