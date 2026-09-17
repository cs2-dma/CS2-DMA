#include "app/Input/primary_keyboard.h"

#include <DMALibrary/Memory/Memory.h>
#include <Windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string_view>
#include <vector>

namespace
{
    constexpr uintptr_t kMinimumKernelAddress = 0x00007FFFFFFFFFFFULL;
    constexpr DWORD kKernelReadFlags =
        VMMDLL_FLAG_NOCACHE | VMMDLL_FLAG_ZEROPAD_ON_FAIL;
    constexpr size_t kBitmapBytes = 64;
    constexpr size_t kBitmapWords = kBitmapBytes / sizeof(uint64_t);
    constexpr DWORD kMaximumScannedModuleBytes = 64u * 1024u * 1024u;

    struct PatternByte
    {
        uint8_t value = 0;
        bool wildcard = false;
    };

    struct ProcessRecord
    {
        DWORD pid = 0;
        DWORD sessionId = 0;
        bool winlogon = false;
        bool csrss = false;
    };

    std::mutex s_initializeMutex;
    std::array<std::atomic<uint64_t>, kBitmapWords> s_bitmap = {};
    std::atomic<uint64_t> s_sequence{0};
    std::atomic<uint64_t> s_publishedAtMs{0};
    std::atomic<uintptr_t> s_bitmapAddress{0};
    std::atomic<DWORD> s_sourcePid{0};
    std::atomic<uint32_t> s_readFailures{0};
    std::atomic<bool> s_ready{false};

    bool EqualsIgnoreCase(const char* value, std::string_view expected)
    {
        if (!value)
            return false;
        const size_t length = std::strlen(value);
        if (length != expected.size())
            return false;
        for (size_t i = 0; i < length; ++i) {
            if (std::tolower(static_cast<unsigned char>(value[i])) !=
                std::tolower(static_cast<unsigned char>(expected[i]))) {
                return false;
            }
        }
        return true;
    }

    std::vector<ProcessRecord> EnumerateInputProcesses()
    {
        std::vector<ProcessRecord> result;
        if (!mem.vHandle)
            return result;

        PVMMDLL_PROCESS_INFORMATION processes = nullptr;
        DWORD count = 0;
        if (!VMMDLL_ProcessGetInformationAll(
                mem.vHandle,
                &processes,
                &count) || !processes) {
            return result;
        }

        result.reserve(count);
        for (DWORD i = 0; i < count; ++i) {
            const bool winlogon =
                EqualsIgnoreCase(processes[i].szName, "winlogon.exe") ||
                EqualsIgnoreCase(processes[i].szNameLong, "winlogon.exe");
            const bool csrss =
                EqualsIgnoreCase(processes[i].szName, "csrss.exe") ||
                EqualsIgnoreCase(processes[i].szNameLong, "csrss.exe");
            if (!winlogon && !csrss)
                continue;
            result.push_back({
                processes[i].dwPID,
                processes[i].win.dwSessionId,
                winlogon,
                csrss
            });
        }
        VMMDLL_MemFree(processes);

        std::stable_sort(
            result.begin(),
            result.end(),
            [](const ProcessRecord& left, const ProcessRecord& right) {
                if (left.sessionId != right.sessionId)
                    return left.sessionId > right.sessionId;
                return left.winlogon && !right.winlogon;
            });
        return result;
    }

    bool ReadKernel(
        DWORD pid,
        uintptr_t address,
        void* output,
        DWORD size,
        DWORD* bytesRead = nullptr)
    {
        if (!mem.vHandle || pid == 0 || address <= kMinimumKernelAddress ||
            !output || size == 0) {
            return false;
        }
        DWORD localBytesRead = 0;
        const bool ok = VMMDLL_MemReadEx(
            mem.vHandle,
            pid | VMMDLL_PID_PROCESS_WITH_KERNELMEMORY,
            address,
            static_cast<PBYTE>(output),
            size,
            &localBytesRead,
            kKernelReadFlags) != FALSE;
        if (bytesRead)
            *bytesRead = localBytesRead;
        return ok && localBytesRead == size;
    }

    template <typename T>
    bool ReadKernelValue(DWORD pid, uintptr_t address, T* output)
    {
        return ReadKernel(pid, address, output, sizeof(T));
    }

    std::vector<PatternByte> ParsePattern(const char* text)
    {
        std::vector<PatternByte> result;
        if (!text)
            return result;
        while (*text) {
            while (*text == ' ')
                ++text;
            if (!*text)
                break;
            if (*text == '?') {
                result.push_back({0, true});
                ++text;
                if (*text == '?')
                    ++text;
                continue;
            }
            if (!text[1])
                return {};
            const char hex[3] = {text[0], text[1], '\0'};
            result.push_back({
                static_cast<uint8_t>(std::strtoul(hex, nullptr, 16)),
                false
            });
            text += 2;
        }
        return result;
    }

    uintptr_t ScanModule(
        DWORD pid,
        uintptr_t moduleBase,
        DWORD moduleSize,
        const char* patternText)
    {
        const std::vector<PatternByte> pattern = ParsePattern(patternText);
        if (pattern.empty() || moduleBase <= kMinimumKernelAddress ||
            moduleSize < pattern.size() ||
            moduleSize > kMaximumScannedModuleBytes) {
            return 0;
        }

        std::vector<uint8_t> image(moduleSize, 0);
        DWORD bytesRead = 0;
        if (!ReadKernel(
                pid,
                moduleBase,
                image.data(),
                moduleSize,
                &bytesRead)) {
            return 0;
        }

        for (size_t offset = 0;
             offset + pattern.size() <= bytesRead;
             ++offset) {
            bool match = true;
            for (size_t i = 0; i < pattern.size(); ++i) {
                if (!pattern[i].wildcard &&
                    image[offset + i] != pattern[i].value) {
                    match = false;
                    break;
                }
            }
            if (match)
                return moduleBase + offset;
        }
        return 0;
    }

    bool ModuleRange(
        DWORD pid,
        const wchar_t* moduleName,
        uintptr_t* base,
        DWORD* size)
    {
        if (base)
            *base = 0;
        if (size)
            *size = 0;
        if (!mem.vHandle || pid == 0 || !moduleName)
            return false;

        PVMMDLL_MAP_MODULEENTRY entry = nullptr;
        if (!VMMDLL_Map_GetModuleFromNameW(
                mem.vHandle,
                pid,
                const_cast<LPWSTR>(moduleName),
                &entry,
                0) || !entry) {
            return false;
        }
        if (base)
            *base = entry->vaBase;
        if (size)
            *size = entry->cbImageSize;
        VMMDLL_MemFree(entry);
        return base && size && *base > kMinimumKernelAddress && *size != 0;
    }

    uintptr_t FindSessionGlobalSlots(DWORD csrssPid)
    {
        constexpr const wchar_t* kModules[] = {
            L"win32ksgd.sys",
            L"win32k.sys",
        };
        constexpr const char* kPatterns[] = {
            "48 8B 05 ?? ?? ?? ?? 48 8B 04 C8",
            "48 8B 05 ?? ?? ?? ?? FF C9",
        };

        for (const wchar_t* module : kModules) {
            uintptr_t base = 0;
            DWORD size = 0;
            if (!ModuleRange(csrssPid, module, &base, &size))
                continue;
            for (const char* pattern : kPatterns) {
                const uintptr_t instruction =
                    ScanModule(csrssPid, base, size, pattern);
                int32_t displacement = 0;
                if (instruction &&
                    ReadKernelValue(
                        csrssPid,
                        instruction + 3,
                        &displacement)) {
                    const uintptr_t address =
                        instruction + 7 + displacement;
                    if (address > kMinimumKernelAddress)
                        return address;
                }
            }
        }
        return 0;
    }

    uint32_t FindGafOffset(DWORD csrssPid, uintptr_t userSessionState)
    {
        uintptr_t base = 0;
        DWORD size = 0;
        if (!ModuleRange(
                csrssPid,
                L"win32kbase.sys",
                &base,
                &size)) {
            return 0;
        }
        const uintptr_t instruction = ScanModule(
            csrssPid,
            base,
            size,
            "48 8D 90 ?? ?? ?? ?? E8 ?? ?? ?? ?? 0F 57 C0");
        int32_t displacement = 0;
        if (!instruction ||
            !ReadKernelValue(
                csrssPid,
                instruction + 3,
                &displacement) ||
            displacement <= 0) {
            return 0;
        }
        const uintptr_t address = userSessionState + displacement;
        return address > kMinimumKernelAddress
            ? static_cast<uint32_t>(displacement)
            : 0;
    }

    std::vector<uintptr_t> ResolveUserSessionStates(DWORD csrssPid)
    {
        std::vector<uintptr_t> result;
        const uintptr_t slotsAddress = FindSessionGlobalSlots(csrssPid);
        uintptr_t slots = 0;
        if (slotsAddress && ReadKernelValue(csrssPid, slotsAddress, &slots)) {
            for (size_t slot = 0; slot < 16; ++slot) {
                uintptr_t entry = 0;
                uintptr_t state = 0;
                if (ReadKernelValue(
                        csrssPid,
                        slots + slot * sizeof(uintptr_t),
                        &entry) &&
                    ReadKernelValue(csrssPid, entry, &state) &&
                    state > kMinimumKernelAddress) {
                    result.push_back(state);
                }
            }
        }

        if (!result.empty())
            return result;

        uintptr_t moduleBase = 0;
        DWORD moduleSize = 0;
        if (!ModuleRange(
                csrssPid,
                L"win32ksgd.sys",
                &moduleBase,
                &moduleSize)) {
            return result;
        }
        constexpr uintptr_t kSlotsOffsets[] = {0x3110, 0x3148};
        for (const uintptr_t offset : kSlotsOffsets) {
            uintptr_t first = 0;
            uintptr_t second = 0;
            uintptr_t state = 0;
            if (ReadKernelValue(csrssPid, moduleBase + offset, &first) &&
                ReadKernelValue(csrssPid, first, &second) &&
                ReadKernelValue(csrssPid, second, &state) &&
                state > kMinimumKernelAddress) {
                result.push_back(state);
            }
        }
        return result;
    }

    bool ValidateBitmapAddress(
        DWORD sourcePid,
        uintptr_t address,
        std::array<uint8_t, kBitmapBytes>* bitmap)
    {
        std::array<uint8_t, kBitmapBytes> local = {};
        if (!ReadKernel(
                sourcePid,
                address,
                local.data(),
                static_cast<DWORD>(local.size()))) {
            return false;
        }
        if (bitmap)
            *bitmap = local;
        return true;
    }

    void PublishBitmap(const std::array<uint8_t, kBitmapBytes>& bitmap)
    {
        s_sequence.fetch_add(1, std::memory_order_acq_rel);
        for (size_t i = 0; i < kBitmapWords; ++i) {
            uint64_t word = 0;
            std::memcpy(
                &word,
                bitmap.data() + i * sizeof(word),
                sizeof(word));
            s_bitmap[i].store(word, std::memory_order_relaxed);
        }
        s_publishedAtMs.store(GetTickCount64(), std::memory_order_relaxed);
        s_sequence.fetch_add(1, std::memory_order_release);
    }

    void ClearPublishedBitmap()
    {
        PublishBitmap({});
    }
}

bool app::input::InitializePrimaryKeyboard()
{
    std::lock_guard<std::mutex> lock(s_initializeMutex);
    s_ready.store(false, std::memory_order_release);
    s_bitmapAddress.store(0, std::memory_order_release);
    s_sourcePid.store(0, std::memory_order_release);
    s_readFailures.store(0, std::memory_order_relaxed);
    ClearPublishedBitmap();
    if (!mem.vHandle)
        return false;

    const std::vector<ProcessRecord> processes = EnumerateInputProcesses();
    std::vector<ProcessRecord> winlogons;
    std::vector<ProcessRecord> csrssProcesses;
    for (const ProcessRecord& process : processes) {
        if (process.winlogon)
            winlogons.push_back(process);
        if (process.csrss)
            csrssProcesses.push_back(process);
    }

    for (const ProcessRecord& winlogon : winlogons) {
        for (const ProcessRecord& csrss : csrssProcesses) {
            if (winlogon.sessionId != 0 &&
                csrss.sessionId != winlogon.sessionId) {
                continue;
            }
            const std::vector<uintptr_t> states =
                ResolveUserSessionStates(csrss.pid);
            for (const uintptr_t state : states) {
                std::array<uint32_t, 3> offsets = {
                    FindGafOffset(csrss.pid, state),
                    0x3690u,
                    0x36A8u,
                };
                for (const uint32_t offset : offsets) {
                    if (offset == 0)
                        continue;
                    const uintptr_t candidate = state + offset;
                    std::array<uint8_t, kBitmapBytes> bitmap = {};
                    if (!ValidateBitmapAddress(
                            winlogon.pid,
                            candidate,
                            &bitmap)) {
                        continue;
                    }
                    s_sourcePid.store(winlogon.pid, std::memory_order_release);
                    s_bitmapAddress.store(candidate, std::memory_order_release);
                    PublishBitmap(bitmap);
                    s_ready.store(true, std::memory_order_release);
                    return true;
                }
            }
        }
    }

    // Windows 10 exposes gafAsyncKeyState directly from win32kbase's EAT.
    for (const ProcessRecord& winlogon : winlogons) {
        PVMMDLL_MAP_EAT eat = nullptr;
        if (!VMMDLL_Map_GetEATU(
                mem.vHandle,
                winlogon.pid | VMMDLL_PID_PROCESS_WITH_KERNELMEMORY,
                const_cast<LPSTR>("win32kbase.sys"),
                &eat) || !eat) {
            continue;
        }
        uintptr_t address = 0;
        for (DWORD i = 0; i < eat->cMap; ++i) {
            if (eat->pMap[i].uszFunction &&
                std::strcmp(
                    eat->pMap[i].uszFunction,
                    "gafAsyncKeyState") == 0) {
                address = eat->pMap[i].vaFunction;
                break;
            }
        }
        VMMDLL_MemFree(eat);
        std::array<uint8_t, kBitmapBytes> bitmap = {};
        if (!address ||
            !ValidateBitmapAddress(winlogon.pid, address, &bitmap)) {
            continue;
        }
        s_sourcePid.store(winlogon.pid, std::memory_order_release);
        s_bitmapAddress.store(address, std::memory_order_release);
        PublishBitmap(bitmap);
        s_ready.store(true, std::memory_order_release);
        return true;
    }
    return false;
}

void app::input::ResetPrimaryKeyboard()
{
    std::lock_guard<std::mutex> lock(s_initializeMutex);
    s_ready.store(false, std::memory_order_release);
    s_bitmapAddress.store(0, std::memory_order_release);
    s_sourcePid.store(0, std::memory_order_release);
    s_readFailures.store(0, std::memory_order_relaxed);
    ClearPublishedBitmap();
}

bool app::input::PollPrimaryKeyboard()
{
    if (!s_ready.load(std::memory_order_acquire))
        return false;
    const DWORD sourcePid = s_sourcePid.load(std::memory_order_acquire);
    const uintptr_t address = s_bitmapAddress.load(std::memory_order_acquire);
    std::array<uint8_t, kBitmapBytes> bitmap = {};
    if (!ValidateBitmapAddress(sourcePid, address, &bitmap)) {
        s_readFailures.fetch_add(1, std::memory_order_relaxed);
        ClearPublishedBitmap();
        return false;
    }
    s_readFailures.store(0, std::memory_order_relaxed);
    PublishBitmap(bitmap);
    return true;
}

app::input::KeyState app::input::ReadPrimaryKeyState(int virtualKey)
{
    if (virtualKey < 0 || virtualKey > 0xFF ||
        !s_ready.load(std::memory_order_acquire)) {
        return {};
    }
    const size_t byteIndex = static_cast<size_t>(virtualKey) * 2u / 8u;
    const size_t wordIndex = byteIndex / sizeof(uint64_t);
    const size_t byteOffset = byteIndex % sizeof(uint64_t);
    const uint8_t mask = static_cast<uint8_t>(
        1u << ((static_cast<unsigned>(virtualKey) % 4u) * 2u));

    for (int attempt = 0; attempt < 3; ++attempt) {
        const uint64_t before = s_sequence.load(std::memory_order_acquire);
        if ((before & 1u) != 0u)
            continue;
        const uint64_t word =
            s_bitmap[wordIndex].load(std::memory_order_relaxed);
        const uint64_t publishedAtMs = s_publishedAtMs.load(std::memory_order_relaxed);
        const uint64_t after = s_sequence.load(std::memory_order_acquire);
        if (before == after) {
            const uint64_t nowMs = GetTickCount64();
            if (!s_ready.load(std::memory_order_acquire) ||
                s_readFailures.load(std::memory_order_relaxed) != 0 ||
                publishedAtMs == 0 || nowMs < publishedAtMs || nowMs - publishedAtMs > 250u)
                return {};
            return {true, ((word >> (byteOffset * 8u)) & mask) != 0u};
        }
    }
    // Contention is unknown input, not a synthetic release edge.
    return {};
}

bool app::input::IsPrimaryKeyDown(int virtualKey)
{
    const auto key = ReadPrimaryKeyState(virtualKey);
    return key.available && key.down;
}

app::input::PrimaryKeyboardStatus app::input::GetPrimaryKeyboardStatus()
{
    return {
        s_ready.load(std::memory_order_acquire),
        s_sourcePid.load(std::memory_order_relaxed),
        s_readFailures.load(std::memory_order_relaxed),
        s_sequence.load(std::memory_order_relaxed) / 2u,
    };
}
