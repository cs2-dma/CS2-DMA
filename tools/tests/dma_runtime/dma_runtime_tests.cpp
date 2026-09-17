#include <Windows.h>
using NTSTATUS = LONG;
#include "DMALibrary/libs/vmmdll.h"
#include "DMALibrary/Memory/ScatterReadTracker.h"

#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
    void Require(bool condition, const char* message)
    {
        if (!condition) throw std::runtime_error(message);
    }

    struct Fixture {
        std::filesystem::path path;
        VMM_HANDLE vmm = nullptr;
        VMMDLL_SCATTER_HANDLE scatter = nullptr;
        HANDLE leech = nullptr;
        PPMEM_SCATTER pages = nullptr;
        ~Fixture()
        {
            if (scatter) VMMDLL_Scatter_CloseHandle(scatter);
            if (vmm) VMMDLL_Close(vmm);
            if (pages) LcMemFree(pages);
            if (leech) LcClose(leech);
            std::error_code error;
            if (!path.empty()) std::filesystem::remove(path, error);
        }
    };
}

int main()
{
    try {
        Fixture fixture;
        std::array<wchar_t, MAX_PATH> tempPath{}, tempName{};
        const DWORD tempLength = GetTempPathW(static_cast<DWORD>(tempPath.size()), tempPath.data());
        Require(tempLength > 0 && tempLength < tempPath.size(), "temporary path");
        Require(GetTempFileNameW(tempPath.data(), L"dma", 0, tempName.data()) != 0, "temporary fixture");
        fixture.path = tempName.data();
        std::array<BYTE, 8192> data{};
        data.fill(0x5a);
        const DWORD health = 100, deadHealth = 0;
        constexpr QWORD fixtureSize = 16 * 1024 * 1024; // upstream file backend minimum
        std::memcpy(data.data() + 256, &health, sizeof(health));
        std::memcpy(data.data() + 512, &deadHealth, sizeof(deadHealth));
        {
            std::ofstream output(fixture.path, std::ios::binary | std::ios::trunc);
            output.write(reinterpret_cast<const char*>(data.data()), data.size());
            output.seekp(fixtureSize - 1);
            output.put('\0');
            Require(output.good(), "write own synthetic fixture");
        }
        const std::string devicePath = fixture.path.string();
        // Pinned upstream test-only option: no OS parsing, process access,
        // symbols, Python or hardware. The device is our own local test file.
        LPCSTR arguments[] = {"", "-printf", "-device", devicePath.c_str(),
            "-_internal_physical_memory_only", "-disable-python", "-disable-symbols",
            "-disable-symbolserver", "-norefresh"};
        fixture.vmm = VMMDLL_Initialize(static_cast<DWORD>(std::size(arguments)), arguments);
        Require(fixture.vmm != nullptr, "initialize physical file backend");
        ULONG64 version = 0;
        Require(VMMDLL_ConfigGet(fixture.vmm, VMMDLL_OPT_CONFIG_VMM_VERSION_MINOR, &version) &&
            version == 18, "loaded MemProcFS minor version");
        Require(VMMDLL_ConfigGet(fixture.vmm, VMMDLL_OPT_CONFIG_VMM_VERSION_REVISION, &version) &&
            version == 10, "loaded MemProcFS revision");

        const DWORD physicalPid = static_cast<DWORD>(-1);
        fixture.scatter = VMMDLL_Scatter_Initialize(fixture.vmm, physicalPid, VMMDLL_FLAG_NOCACHE);
        Require(fixture.scatter != nullptr, "scatter handle");
        dma::ScatterReadTracker<DWORD> tracker;
        for (int cycle = 0; cycle < 256; ++cycle) {
            DWORD live = 99, dead = 99, missing = 99;
            DWORD liveBytes = 0, deadBytes = 0, missingBytes = 0, crossingBytes = 0;
            std::array<BYTE, 32> crossing{};
            auto queue = [&](QWORD address, void* buffer, DWORD size, DWORD* bytes) {
                auto* request = tracker.Add(size, bytes);
                Require(request != nullptr, "stable completion storage");
                request->prepared = VMMDLL_Scatter_PrepareEx(fixture.scatter, address, size,
                    static_cast<PBYTE>(buffer), &request->completed) != FALSE;
                Require(request->prepared, "prepare synthetic range");
            };
            queue(256, &live, sizeof(live), &liveBytes);
            queue(512, &dead, sizeof(dead), &deadBytes);
            queue(fixtureSize, &missing, sizeof(missing), &missingBytes);
            queue(fixtureSize - 16, crossing.data(), static_cast<DWORD>(crossing.size()), &crossingBytes);
            Require(live == 0 && dead == 0 && missing == 0, "PrepareEx zero-fill contract");
            const bool dispatched = VMMDLL_Scatter_ExecuteRead(fixture.scatter) != FALSE;
            const auto quality = tracker.Complete(dispatched);
            Require(dispatched, "partial batch still dispatches successfully");
            Require(live == 100 && liveBytes == 4, "valid nonzero read");
            Require(dead == 0 && deadBytes == 4, "valid zero is different from read failure");
            Require(missing == 0 && missingBytes == 0, "out-of-range is not a complete zero");
            Require(crossingBytes == 16, "partial range byte count");
            Require(quality.requests == 4 && quality.requestedBytes == 44 &&
                quality.completedBytes == 24 && quality.incompleteRequests == 2, "exact batch quality");
            Require(VMMDLL_Scatter_Clear(fixture.scatter, physicalPid, VMMDLL_FLAG_NOCACHE) != FALSE, "clear batch");
            tracker.Reset();
        }
        VMMDLL_Scatter_CloseHandle(fixture.scatter);
        fixture.scatter = nullptr;
        VMMDLL_Close(fixture.vmm);
        fixture.vmm = nullptr;

        LC_CONFIG config{};
        config.dwVersion = LC_CONFIG_VERSION;
        Require(devicePath.size() < sizeof(config.szDevice), "device path length");
        strcpy_s(config.szDevice, devicePath.c_str());
        fixture.leech = LcCreate(&config);
        Require(fixture.leech != nullptr, "LeechCore LC_CONFIG ABI");
        Require(LcAllocScatter1(2, &fixture.pages) != FALSE, "LeechCore scatter allocation");
        fixture.pages[0]->qwA = 0;
        fixture.pages[1]->qwA = fixtureSize;
        LC_READ_PAGE_RESULT results[2]{};
        Require(LcReadScatterEx(fixture.leech, 2, fixture.pages, results) != FALSE, "new typed scatter API export");
        Require(fixture.pages[0]->f && results[0] == LC_READ_PAGE_RESULT_SUCCESS, "typed valid page");
        Require(!fixture.pages[1]->f && results[1] != LC_READ_PAGE_RESULT_SUCCESS &&
            results[1] != LC_READ_PAGE_RESULT_SUCCESS_AFTER_RETRY, "typed failed page");
        std::cout << "dma_runtime_tests passed: real pinned DLLs, file backend, 256 mixed batches; no FPGA/process access.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "dma_runtime_tests failed: " << error.what() << '\n';
        return 1;
    }
}
