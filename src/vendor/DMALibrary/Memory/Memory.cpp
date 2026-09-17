#include "pch.h"
#include "Memory.h"
#include "ScatterReadTracker.h"

Memory::LibModules Memory::modules{};
Memory::CurrentProcessInformation Memory::current_process{};
Memory::DmaInitStats Memory::LAST_DMA_INIT_STATS{};

#include <thread>
#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <mutex>

namespace {
	std::mutex g_runtimeLibraryMutex;
	std::mutex g_fixCr3Mutex;
	constexpr uint64_t kRecentScatterWindowUs = 10000000u;
	constexpr size_t kMaxScatterHandlesPerThread = 32u;

	struct ScatterBatchState
	{
		VMMDLL_SCATTER_HANDLE handle = nullptr;
		uint32_t pendingCount = 0;
		uint32_t prepareFailureCount = 0;
		bool poisoned = false;
		DWORD pid = 0;
		dma::ScatterReadTracker<DWORD> reads;
	};

	thread_local std::array<
		ScatterBatchState,
		kMaxScatterHandlesPerThread> g_scatterBatchStates = {};

	ScatterBatchState* FindScatterBatchState(
		VMMDLL_SCATTER_HANDLE handle,
		bool create)
	{
		if (!handle)
			return nullptr;

		ScatterBatchState* empty = nullptr;
		for (ScatterBatchState& state : g_scatterBatchStates) {
			if (state.handle == handle)
				return &state;
			if (!state.handle && !empty)
				empty = &state;
		}
		if (!create || !empty)
			return nullptr;

		*empty = {};
		empty->handle = handle;
		return empty;
	}

	void ResetScatterBatchState(VMMDLL_SCATTER_HANDLE handle, bool release)
	{
		if (ScatterBatchState* state =
				FindScatterBatchState(handle, false)) {
			if (release)
				*state = {};
			else {
				state->pendingCount = 0;
				state->prepareFailureCount = 0;
				state->reads.Reset();
			}
		}
	}

	std::string to_lower(std::string s)
	{
		std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return s;
	}

	uint64_t SteadyNowUs()
	{
		return static_cast<uint64_t>(
			std::chrono::duration_cast<std::chrono::microseconds>(
				std::chrono::steady_clock::now().time_since_epoch()).count());
	}

	void UpdateAtomicPeak(std::atomic<uint64_t>& peak, uint64_t value)
	{
		uint64_t previous = peak.load(std::memory_order_relaxed);
		while (value > previous &&
			   !peak.compare_exchange_weak(
				   previous,
				   value,
				   std::memory_order_relaxed,
				   std::memory_order_relaxed)) {
		}
	}

	void UpdateRecentScatterPeak(uint64_t elapsedUs)
	{
		const uint64_t kDataWorkerBudgetUs =
			Memory::DMA_SCATTER_BUDGET_US.load(std::memory_order_relaxed);
		constexpr uint64_t kFiveMillisecondsUs = 5000u;
		constexpr uint64_t kFrameAt60HzUs = 16667u;
		const uint64_t nowUs = SteadyNowUs();
		uint64_t windowStart =
			Memory::DMA_EXECUTE_SCATTER_RECENT_WINDOW_START_US.load(
				std::memory_order_relaxed);
		if (windowStart == 0 ||
			nowUs < windowStart ||
			(nowUs - windowStart) >= kRecentScatterWindowUs) {
			if (Memory::DMA_EXECUTE_SCATTER_RECENT_WINDOW_START_US.compare_exchange_strong(
					windowStart,
					nowUs,
					std::memory_order_relaxed,
					std::memory_order_relaxed)) {
				Memory::DMA_EXECUTE_SCATTER_RECENT_PEAK_US.store(
					elapsedUs,
					std::memory_order_relaxed);
				Memory::DMA_EXECUTE_SCATTER_RECENT_COUNT.store(0, std::memory_order_relaxed);
				Memory::DMA_EXECUTE_SCATTER_OVER_1MS_RECENT_COUNT.store(0, std::memory_order_relaxed);
				Memory::DMA_EXECUTE_SCATTER_OVER_BUDGET_RECENT_COUNT.store(0, std::memory_order_relaxed);
				Memory::DMA_EXECUTE_SCATTER_OVER_5MS_RECENT_COUNT.store(0, std::memory_order_relaxed);
				Memory::DMA_EXECUTE_SCATTER_OVER_16MS_RECENT_COUNT.store(0, std::memory_order_relaxed);
			}
		}

		UpdateAtomicPeak(Memory::DMA_EXECUTE_SCATTER_RECENT_PEAK_US, elapsedUs);
		Memory::DMA_EXECUTE_SCATTER_RECENT_COUNT.fetch_add(1, std::memory_order_relaxed);
		if (elapsedUs > 1000u)
			Memory::DMA_EXECUTE_SCATTER_OVER_1MS_RECENT_COUNT.fetch_add(1, std::memory_order_relaxed);
		if (elapsedUs > kDataWorkerBudgetUs)
			Memory::DMA_EXECUTE_SCATTER_OVER_BUDGET_RECENT_COUNT.fetch_add(1, std::memory_order_relaxed);
		if (elapsedUs > kFiveMillisecondsUs)
			Memory::DMA_EXECUTE_SCATTER_OVER_5MS_RECENT_COUNT.fetch_add(1, std::memory_order_relaxed);
		if (elapsedUs > kFrameAt60HzUs)
			Memory::DMA_EXECUTE_SCATTER_OVER_16MS_RECENT_COUNT.fetch_add(1, std::memory_order_relaxed);
	}

	uint64_t SteadyNowMs()
	{
		return static_cast<uint64_t>(
			std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::steady_clock::now().time_since_epoch()).count());
	}

	uint64_t FileTimeToUnixMs(const FILETIME& fileTime)
	{
		ULARGE_INTEGER value = {};
		value.LowPart = fileTime.dwLowDateTime;
		value.HighPart = fileTime.dwHighDateTime;
		if (value.QuadPart < 116444736000000000ULL)
			return 0;
		return static_cast<uint64_t>((value.QuadPart - 116444736000000000ULL) / 10000ULL);
	}

	uint64_t CurrentSystemTimeMs()
	{
		FILETIME ft = {};
		GetSystemTimeAsFileTime(&ft);
		return FileTimeToUnixMs(ft);
	}

	uint64_t ApproxBootTimeMs()
	{
		const uint64_t nowMs = CurrentSystemTimeMs();
		const uint64_t uptimeMs = GetTickCount64();
		return (nowMs > uptimeMs) ? (nowMs - uptimeMs) : 0;
	}

	std::string BuildMemMapCachePath()
	{
		auto tempPath = std::filesystem::temp_directory_path();
		return (tempPath / "KevqDMA_mmap.txt").string();
	}

	bool IsUsableMemMapCache(const std::string& path)
	{
		WIN32_FILE_ATTRIBUTE_DATA attrs = {};
		if (!GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &attrs))
			return false;
		if ((attrs.nFileSizeHigh == 0 && attrs.nFileSizeLow == 0) ||
			(attrs.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
			return false;

		const uint64_t fileWriteMs = FileTimeToUnixMs(attrs.ftLastWriteTime);
		const uint64_t bootMs = ApproxBootTimeMs();
		if (fileWriteMs == 0 || bootMs == 0)
			return false;

		return fileWriteMs + 5000ULL >= bootMs;
	}
}

bool Memory::EnsureRuntimeLibrariesLoaded()
{
	std::lock_guard<std::mutex> lock(g_runtimeLibraryMutex);

	static bool runtimeValidated = false;
	if (runtimeValidated)
		return true;

	std::array<wchar_t, 32768> executablePath{};
	const DWORD pathLength = GetModuleFileNameW(nullptr, executablePath.data(),
		static_cast<DWORD>(executablePath.size()));
	if (!pathLength || pathLength >= executablePath.size())
		return false;
	const auto runtimeDirectory = std::filesystem::path(executablePath.data()).parent_path();
	auto loadRuntime = [&](const wchar_t* name) {
		const auto path = runtimeDirectory / name;
		return LoadLibraryExW(path.c_str(), nullptr,
			LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
	};
	LOG("[INFO] Loading required runtime libraries.\n");
	if (!modules.LEECHCORE)
		modules.LEECHCORE = loadRuntime(L"leechcore.dll");
	if (!modules.VMM)
		modules.VMM = loadRuntime(L"vmm.dll");
	if (!modules.FTD3XXWU)
		modules.FTD3XXWU = loadRuntime(L"FTD3XXWU.dll");
	if (!modules.FTD3XX)
		modules.FTD3XX = loadRuntime(L"FTD3XX.dll");

	if (!modules.VMM ||
		!modules.LEECHCORE ||
		(!modules.FTD3XXWU && !modules.FTD3XX))
	{
		LOG("vmm: %p\n", modules.VMM);
		LOG("leech: %p\n", modules.LEECHCORE);
		LOG("ftd-wu: %p\n", modules.FTD3XXWU);
		LOG("ftd-legacy: %p\n", modules.FTD3XX);
		LOG("[ERROR] Failed to load one or more runtime libraries.\n");
		return false;
	}

	if (!modules.FTD3XXWU)
		LOG("[WARN] FTDI D3XX 1.4 runtime unavailable; using legacy D3XX runtime.\n");
	else if (!modules.FTD3XX)
		LOG("[WARN] Legacy FTDI D3XX runtime unavailable; no fallback driver is available.\n");

	// VMM/LeechCore are also normal EXE imports. Verify the actual modules,
	// not just the files that LoadLibraryEx was asked to locate.
	for (HMODULE module : {modules.VMM, modules.LEECHCORE, modules.FTD3XXWU, modules.FTD3XX}) {
		if (!module)
			continue;
		std::array<wchar_t, 32768> loadedPath{};
		const DWORD length = GetModuleFileNameW(module, loadedPath.data(),
			static_cast<DWORD>(loadedPath.size()));
		if (!length || length >= loadedPath.size())
			return false;
		LOGW(L"[INFO] DMA runtime module: %ls\n", loadedPath.data());
		const auto actualDirectory = std::filesystem::path(loadedPath.data()).parent_path();
		if (CompareStringOrdinal(actualDirectory.c_str(), -1,
				runtimeDirectory.c_str(), -1, TRUE) != CSTR_EQUAL) {
			LOG("[ERROR] DMA runtime module is outside the executable directory. Restore the pinned x64 bundle.\n");
			return false;
		}
	}
	runtimeValidated = true;
	LOG("[INFO] Runtime libraries loaded successfully.\n");
	return true;
}

Memory::~Memory()
{
	if (vHandle) {
		VMMDLL_Close(vHandle);
		vHandle = nullptr;
	}
	DMA_INITIALIZED = false;
	PROCESS_INITIALIZED = false;
	current_process = {};
}

bool Memory::DumpMemoryMap(const std::string& outputPath, bool debug)
{
	LPCSTR args[] = {
		"",
		"-device",
		"fpga://algo=0",
		"-waitinitialize",
		"-norefresh",
		"",
		""
	};
	int argc = 5;
	if (debug)
	{
		args[argc++] = const_cast<LPCSTR>("-v");
		args[argc++] = const_cast<LPCSTR>("-printf");
	}

	VMM_HANDLE handle = VMMDLL_Initialize(argc, args);
	if (!handle)
	{
		LOG("[ERROR] Failed to open VMM handle.\n");
		return false;
	}

	PVMMDLL_MAP_PHYSMEM pPhysMemMap = NULL;
	if (!VMMDLL_Map_GetPhysMem(handle, &pPhysMemMap))
	{
		LOG("[ERROR] Failed to query physical memory map.\n");
		VMMDLL_Close(handle);
		return false;
	}

	if (pPhysMemMap->dwVersion != VMMDLL_MAP_PHYSMEM_VERSION)
	{
		LOG("[ERROR] Unsupported physical memory map version.\n");
		VMMDLL_MemFree(pPhysMemMap);
		VMMDLL_Close(handle);
		return false;
	}

	if (pPhysMemMap->cMap == 0)
	{
		LOG("[ERROR] Physical memory map is empty.\n");
		VMMDLL_MemFree(pPhysMemMap);
		VMMDLL_Close(handle);
		return false;
	}
	
	std::stringstream sb;
	for (DWORD i = 0; i < pPhysMemMap->cMap; i++)
	{
		sb << std::hex << pPhysMemMap->pMap[i].pa << " " << (pPhysMemMap->pMap[i].pa + pPhysMemMap->pMap[i].cb - 1) << std::endl;
	}

	std::ofstream output(outputPath, std::ios::out | std::ios::trunc);
	if (!output)
	{
		LOG("[ERROR] Failed to create physical memory map file.\n");
		VMMDLL_MemFree(pPhysMemMap);
		VMMDLL_Close(handle);
		return false;
	}

	output << sb.str();
	output.close();
	if (!output)
	{
		LOG("[ERROR] Failed to write physical memory map file.\n");
		VMMDLL_MemFree(pPhysMemMap);
		VMMDLL_Close(handle);
		return false;
	}

	VMMDLL_MemFree(pPhysMemMap);
	LOG("[INFO] Physical memory map file written successfully.\n");
	VMMDLL_Close(handle);
	return true;
}

bool Memory::SetFPGA()
{
	ULONG64 qwID = 0, qwVersionMajor = 0, qwVersionMinor = 0;
	if (!VMMDLL_ConfigGet(this->vHandle, LC_OPT_FPGA_FPGA_ID, &qwID) ||
		!VMMDLL_ConfigGet(this->vHandle, LC_OPT_FPGA_VERSION_MAJOR, &qwVersionMajor) ||
		!VMMDLL_ConfigGet(this->vHandle, LC_OPT_FPGA_VERSION_MINOR, &qwVersionMinor))
	{
		LOG("[WARN] FPGA metadata query failed. Proceeding with limited diagnostics.\n");
		return true;
	}

	LOG("[INFO] VMMDLL_ConfigGet: ID=%lli VERSION=%lli.%lli\n", qwID, qwVersionMajor, qwVersionMinor);

	if ((qwVersionMajor >= 4) && ((qwVersionMajor >= 5) || (qwVersionMinor >= 7)))
	{
		HANDLE handle;
		LC_CONFIG config = {.dwVersion = LC_CONFIG_VERSION, .szDevice = "existing"};
		handle = LcCreate(&config);
		if (!handle)
		{
			LOG("[ERROR] Failed to create FPGA configuration handle.\n");
			return false;
		}

		BYTE pcieConfig[4] = {0x10, 0x00, 0x10, 0x00};
		const BOOL commandSucceeded = LcCommand(
			handle,
			LC_CMD_FPGA_CFGREGPCIE_MARKWR | 0x002,
			static_cast<DWORD>(sizeof(pcieConfig)),
			pcieConfig,
			NULL,
			NULL);
		LcClose(handle);
		if (!commandSucceeded)
		{
			LOG("[ERROR] FPGA PCIe register flag reset failed.\n");
			return false;
		}
		LOG("[INFO] FPGA PCIe register flag reset completed.\n");
	}

	return true;
}

bool Memory::InitDma(bool memMap, bool debug)
{
	if (DMA_INITIALIZED)
		return true;

	LAST_DMA_INIT_STATS = {};
	const uint64_t totalStartMs = SteadyNowMs();
	if (!EnsureRuntimeLibrariesLoaded())
		return false;
	LAST_DMA_INIT_STATS.runtimeLibsMs = SteadyNowMs() - totalStartMs;

	LOG("[INFO] DMA subsystem initialization started.\n");

	bool useMemMap = memMap;
	while (true)
	{
		// Keep VMM background refresh enabled. The runtime cache profile makes
		// expensive process scans infrequent while still expiring stale TLB and
		// process-list data; disabling the scheduler leaves live sessions stale.
		LPCSTR args[] = {
			"",
			"-device",
			"fpga://algo=0",
			"-waitinitialize",
			"",
			"",
			"",
			""
		};
		DWORD argc = 4;
		if (debug)
		{
			args[argc++] = "-v";
			args[argc++] = "-printf";
		}

		std::string path = "";
		if (useMemMap)
		{
			LAST_DMA_INIT_STATS.usedMemMap = true;
			path = BuildMemMapCachePath();
			if (IsUsableMemMapCache(path))
			{
				LAST_DMA_INIT_STATS.reusedMemMapCache = true;
				LOG("[INFO] Reusing physical memory map cache from current boot.\n");
			}
			else
			{
				const uint64_t memMapStartMs = SteadyNowMs();
				LOG("[INFO] Physical memory map acquisition started.\n");
				const bool dumped = this->DumpMemoryMap(path, debug);
				LAST_DMA_INIT_STATS.memMapMs = SteadyNowMs() - memMapStartMs;
				if (!dumped)
				{
					LOG("[WARN] Memory map acquisition failed. Continuing without memory map.\n");
				}
				else
				{
					LOG("[INFO] Physical memory map acquired successfully.\n");
				}
			}

			if (IsUsableMemMapCache(path))
			{
				args[argc++] = "-memmap";
				args[argc++] = path.c_str();
			}
		}

		const uint64_t vmmInitStartMs = SteadyNowMs();
		this->vHandle = VMMDLL_Initialize(argc, args);
		LAST_DMA_INIT_STATS.vmmInitMs += (SteadyNowMs() - vmmInitStartMs);
		if (this->vHandle)
			break;

		if (useMemMap)
		{
			useMemMap = false;
			LOG("[WARN] DMA initialization with memory map failed; retrying without memory map.\n");
			continue;
		}
		LOG("[ERROR] DMA initialization failed. Verify FPGA connection and device availability.\n");
		return false;
	}

	ULONG64 FPGA_ID = 0, DEVICE_ID = 0;
	ULONG64 vmmVersion[3]{}, leechVersion[3]{};
	const bool versionsRead =
		VMMDLL_ConfigGet(vHandle, VMMDLL_OPT_CONFIG_VMM_VERSION_MAJOR, &vmmVersion[0]) &&
		VMMDLL_ConfigGet(vHandle, VMMDLL_OPT_CONFIG_VMM_VERSION_MINOR, &vmmVersion[1]) &&
		VMMDLL_ConfigGet(vHandle, VMMDLL_OPT_CONFIG_VMM_VERSION_REVISION, &vmmVersion[2]) &&
		VMMDLL_ConfigGet(vHandle, LC_OPT_CORE_VERSION_MAJOR, &leechVersion[0]) &&
		VMMDLL_ConfigGet(vHandle, LC_OPT_CORE_VERSION_MINOR, &leechVersion[1]) &&
		VMMDLL_ConfigGet(vHandle, LC_OPT_CORE_VERSION_REVISION, &leechVersion[2]);
	if (versionsRead) {
		LOG("[INFO] DMA runtime versions: MemProcFS=%llu.%llu.%llu LeechCore=%llu.%llu.%llu\n",
			vmmVersion[0], vmmVersion[1], vmmVersion[2],
			leechVersion[0], leechVersion[1], leechVersion[2]);
	} else {
		LOG("[WARN] DMA runtime version query failed.\n");
	}

	VMMDLL_ConfigGet(this->vHandle, LC_OPT_FPGA_FPGA_ID, &FPGA_ID);
	VMMDLL_ConfigGet(this->vHandle, LC_OPT_FPGA_DEVICE_ID, &DEVICE_ID);

	LOG("[INFO] FPGA identifier: %llu\n", FPGA_ID);
	LOG("[INFO] Device identifier: %llu\n", DEVICE_ID);

	const uint64_t fpgaStartMs = SteadyNowMs();
	if (!this->SetFPGA())
	{
		LOG("[ERROR] FPGA configuration step failed.\n");
		VMMDLL_Close(this->vHandle);
		this->vHandle = nullptr;
		return false;
	}
	LAST_DMA_INIT_STATS.fpgaConfigMs = SteadyNowMs() - fpgaStartMs;

	DMA_INITIALIZED = TRUE;
	LAST_DMA_INIT_STATS.totalMs = SteadyNowMs() - totalStartMs;
	LOG(
		"[INFO] DMA init timing: total=%llums libs=%llums memmap=%llums%s vmm=%llums fpga=%llums\n",
		static_cast<unsigned long long>(LAST_DMA_INIT_STATS.totalMs),
		static_cast<unsigned long long>(LAST_DMA_INIT_STATS.runtimeLibsMs),
		static_cast<unsigned long long>(LAST_DMA_INIT_STATS.memMapMs),
		LAST_DMA_INIT_STATS.reusedMemMapCache ? " [cache]" : "",
		static_cast<unsigned long long>(LAST_DMA_INIT_STATS.vmmInitMs),
		static_cast<unsigned long long>(LAST_DMA_INIT_STATS.fpgaConfigMs));
	LOG("[INFO] DMA subsystem initialization completed successfully.\n");
	return true;
}

bool Memory::AttachToProcess(const std::string& process_name, bool applyCr3Fix)
{
	return AttachToProcessId(
		process_name,
		GetPidFromName(process_name),
		applyCr3Fix);
}

bool Memory::AttachToProcessId(
	const std::string& process_name,
	DWORD processId,
	bool applyCr3Fix)
{
	if (!DMA_INITIALIZED)
	{
		LOG("[ERROR] AttachToProcess called before DMA initialization.\n");
		return false;
	}
	if (!vHandle || processId == 0)
		return false;

	auto strip_exe = [&](std::string s) {
		std::string lower = to_lower(s);
		if (lower.size() > 4 && lower.substr(lower.size() - 4) == ".exe")
			return s.substr(0, s.size() - 4);
		return s;
	};
	auto canAccessProbeModules = [&](DWORD pid, const std::string& targetProcessName) -> bool {
		auto hasModule = [&](const std::string& moduleName) -> bool {
			return !moduleName.empty() &&
				VMMDLL_ProcessGetModuleBaseU(this->vHandle, pid, moduleName.c_str()) != 0;
		};

		const std::string targetLower = to_lower(targetProcessName);
		const std::string targetBase = strip_exe(targetProcessName);
		const std::string targetBaseLower = to_lower(targetBase);
		const bool isCs2Target =
			targetLower == "cs2.exe" ||
			targetLower == "cs2" ||
			targetBaseLower == "cs2";

		if (isCs2Target) {
			
			
			
			
			return hasModule("client.dll") || hasModule("engine2.dll");
		}

		return hasModule(targetProcessName) || (targetBase != targetProcessName && hasModule(targetBase));
	};

	const std::string target_name = to_lower(process_name);
	if (PROCESS_INITIALIZED &&
		current_process.PID == processId &&
		to_lower(current_process.process_name) == target_name)
	{
		if (!applyCr3Fix || canAccessProbeModules(current_process.PID, process_name))
			return true;

		LOG("[INFO] Re-attaching %s because probe modules are not yet accessible.\n", process_name.c_str());
		PROCESS_INITIALIZED = FALSE;
		current_process = {};
	}

	PROCESS_INITIALIZED = FALSE;
	current_process = {};

	current_process.PID = processId;
	current_process.process_name = process_name;

	if (applyCr3Fix)
	{
		if (!FixCr3())
			LOG("[WARN] CR3 remediation was not confirmed for %s.\n", process_name.c_str());
		else
			LOG("[INFO] CR3 remediation completed for %s.\n", process_name.c_str());
	}

	current_process.base_address = GetModuleBase(process_name);
	if (!current_process.base_address)
	{
		LOG("[WARN] Unable to resolve module base address for %s during attach. Continuing with PID-only attach.\n", process_name.c_str());
	}

	current_process.base_size = GetModuleSize(process_name);
	if (!current_process.base_size)
	{
		LOG("[WARN] Unable to resolve module image size for %s during attach. Continuing with PID-only attach.\n", process_name.c_str());
	}

	LOG("[INFO] Target process attached.\n");
	LOG("[INFO] Process: %s\n", process_name.c_str());
	LOG("[INFO] PID: %lu\n", static_cast<unsigned long>(current_process.PID));
	LOG("[INFO] Base address: 0x%llx\n", current_process.base_address);
	LOG("[INFO] Image size: 0x%llx\n", current_process.base_size);

	PROCESS_INITIALIZED = TRUE;

	return true;
}

void Memory::ResetProcessState()
{
	PROCESS_INITIALIZED = FALSE;
	current_process = {};
}

static bool s_fixCr3PluginsInitialized = false;

void Memory::CloseDma()
{
	std::lock_guard<std::mutex> lock(g_fixCr3Mutex);

	PROCESS_INITIALIZED = FALSE;
	current_process = {};
	if (vHandle) {
		VMMDLL_Close(vHandle);
		vHandle = nullptr;
	}
	DMA_INITIALIZED = FALSE;
	
	s_fixCr3PluginsInitialized = false;
}

DWORD Memory::GetPidFromName(const std::string& process_name)
{
	std::string base_name = process_name;
	if (base_name.size() > 4)
	{
		std::string tail = base_name.substr(base_name.size() - 4);
		std::transform(tail.begin(), tail.end(), tail.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		if (tail == ".exe")
			base_name = base_name.substr(0, base_name.size() - 4);
	}

	DWORD directPid = 0;
	if (this->vHandle)
	{
		VMMDLL_PidGetFromName(this->vHandle, process_name.c_str(), &directPid);
		if (directPid == 0 && base_name != process_name)
			VMMDLL_PidGetFromName(this->vHandle, base_name.c_str(), &directPid);
	}

	const std::string target_a = to_lower(process_name);
	const std::string target_b = to_lower(base_name);
	const std::string target_c = to_lower(base_name + ".exe");
	const bool isCs2Target =
		target_a == "cs2.exe" ||
		target_a == "cs2" ||
		target_b == "cs2";
	if (directPid != 0 && !isCs2Target)
		return directPid;
	if (directPid != 0 && this->vHandle)
	{
		const bool hasLiveModules =
			VMMDLL_ProcessGetModuleBaseU(this->vHandle, directPid, "client.dll") != 0 ||
			VMMDLL_ProcessGetModuleBaseU(this->vHandle, directPid, "engine2.dll") != 0;
		if (hasLiveModules)
			return directPid;
	}

	PVMMDLL_PROCESS_INFORMATION process_info = NULL;
	DWORD total_processes = 0;
	struct Candidate
	{
		DWORD pid = 0;
		int score = 0;
		bool hasProcessModule = false;
		bool hasClientModule = false;
	};

	Candidate best = {};
	if (this->vHandle &&
		VMMDLL_ProcessGetInformationAll(
			this->vHandle,
			&process_info,
			&total_processes))
	{
		for (DWORD i = 0; i < total_processes; i++)
		{
			std::string short_name = process_info[i].szName ? process_info[i].szName : "";
			std::string long_name = process_info[i].szNameLong ? process_info[i].szNameLong : "";
			short_name = to_lower(short_name);
			long_name = to_lower(long_name);

			int score = 0;
			if (short_name == target_a || short_name == target_b || short_name == target_c)
				score = 4;
			else if (long_name == target_a || long_name == target_b || long_name == target_c)
				score = 3;
			else if (short_name.find(target_a) != std::string::npos || long_name.find(target_a) != std::string::npos ||
			         short_name.find(target_b) != std::string::npos || long_name.find(target_b) != std::string::npos ||
			         short_name.find(target_c) != std::string::npos || long_name.find(target_c) != std::string::npos)
				score = 1;

			if (score == 0 || process_info[i].dwPID == 0)
				continue;

			const DWORD candidatePid = process_info[i].dwPID;
			const bool hasProcessModule =
				VMMDLL_ProcessGetModuleBaseU(this->vHandle, candidatePid, process_name.c_str()) != 0 ||
				(base_name != process_name && VMMDLL_ProcessGetModuleBaseU(this->vHandle, candidatePid, base_name.c_str()) != 0);
			const bool hasClientModule =
				VMMDLL_ProcessGetModuleBaseU(this->vHandle, candidatePid, "client.dll") != 0;

			const bool better =
				best.pid == 0 ||
				score > best.score ||
				(score == best.score && hasClientModule && !best.hasClientModule) ||
				(score == best.score && hasClientModule == best.hasClientModule && hasProcessModule && !best.hasProcessModule) ||
				(score == best.score && hasClientModule == best.hasClientModule && hasProcessModule == best.hasProcessModule && candidatePid > best.pid);

			if (better)
			{
				best.pid = candidatePid;
				best.score = score;
				best.hasProcessModule = hasProcessModule;
				best.hasClientModule = hasClientModule;
			}
		}
		VMMDLL_MemFree(process_info);
	}

	if (best.pid &&
		(!isCs2Target || best.hasProcessModule || best.hasClientModule))
		return best.pid;

	// A DMA session observes the target machine's process catalog. Falling back
	// to Toolhelp would inspect the operator PC and could attach the target VMM
	// session to an unrelated local PID with the same executable name.
	return isCs2Target ? 0 : directPid;
}

size_t Memory::GetModuleBase(const std::string& moduleName)
{
	const ULONG64 base =
		VMMDLL_ProcessGetModuleBaseU(this->vHandle, current_process.PID, moduleName.c_str());
	if (base)
	{
		LOG(
			"[INFO] Base address resolved for %s at 0x%llx.\n",
			moduleName.c_str(),
			static_cast<unsigned long long>(base));
		return static_cast<size_t>(base);
	}

	PVMMDLL_MAP_MODULEENTRY moduleInfo = nullptr;
	if (VMMDLL_Map_GetModuleFromNameU(
			this->vHandle,
			current_process.PID,
			moduleName.c_str(),
			&moduleInfo,
			VMMDLL_MODULE_FLAG_NORMAL) &&
		moduleInfo)
	{
		const size_t mappedBase = static_cast<size_t>(moduleInfo->vaBase);
		VMMDLL_MemFree(moduleInfo);
		LOG(
			"[INFO] Base address resolved for %s at 0x%llx.\n",
			moduleName.c_str(),
			static_cast<unsigned long long>(mappedBase));
		return mappedBase;
	}

	return 0;
}

size_t Memory::GetModuleSize(const std::string& moduleName)
{
	PVMMDLL_MAP_MODULEENTRY moduleInfo = nullptr;
	if (!VMMDLL_Map_GetModuleFromNameU(
			this->vHandle,
			current_process.PID,
			moduleName.c_str(),
			&moduleInfo,
			VMMDLL_MODULE_FLAG_NORMAL) ||
		!moduleInfo)
		return 0;

	const size_t imageSize = static_cast<size_t>(moduleInfo->cbImageSize);
	VMMDLL_MemFree(moduleInfo);
	LOG(
		"[INFO] Image size resolved for %s at 0x%llx.\n",
		moduleName.c_str(),
		static_cast<unsigned long long>(imageSize));
	return imageSize;
}

std::atomic<uint64_t> cbSize = 0x80000;

VOID cbAddFile(_Inout_ HANDLE, _In_ LPCSTR uszName, _In_ ULONG64 cb, _In_opt_ PVMMDLL_VFS_FILELIST_EXINFO)
{
	if (strcmp(uszName, "dtb.txt") == 0)
		cbSize.store(cb, std::memory_order_relaxed);
}

struct Info
{
	uint32_t index;
	uint32_t process_id;
	uint64_t dtb;
	uint64_t kernelAddr;
	std::string name;
};

bool Memory::FixCr3()
{
	std::lock_guard<std::mutex> lock(g_fixCr3Mutex);
	if (!vHandle || current_process.PID == 0 || current_process.process_name.empty())
		return false;

	auto strip_exe = [&](std::string s) {
		std::string lower = to_lower(s);
		if (lower.size() > 4 && lower.substr(lower.size() - 4) == ".exe")
			return s.substr(0, s.size() - 4);
		return s;
	};
	auto canAccessProbeModules = [&](DWORD pid) -> bool {
		auto hasModule = [&](const std::string& moduleName) -> bool {
			if (moduleName.empty()) return false;
			uint64_t base = VMMDLL_ProcessGetModuleBaseU(this->vHandle, pid, moduleName.c_str());
			if (base == 0) return false;
			
			IMAGE_DOS_HEADER dos = { 0 };
			DWORD read_size = 0;
			if (!VMMDLL_MemReadEx(this->vHandle, pid, base, (PBYTE)&dos, sizeof(IMAGE_DOS_HEADER), &read_size, VMMDLL_FLAG_NOCACHE))
				return false;
			return dos.e_magic == 0x5A4D;
		};

		const std::string targetLower = to_lower(current_process.process_name);
		const std::string targetBase = strip_exe(current_process.process_name);
		const std::string targetBaseLower = to_lower(targetBase);
		const bool isCs2Target =
			targetLower == "cs2.exe" ||
			targetLower == "cs2" ||
			targetBaseLower == "cs2";

		if (isCs2Target)
			return hasModule("client.dll") || hasModule("engine2.dll");

		return hasModule(current_process.process_name) ||
			(targetBase != current_process.process_name && hasModule(targetBase));
	};

	if (canAccessProbeModules(current_process.PID))
		return true;

	if (this->vHandle)
		VMMDLL_ConfigSet(this->vHandle, VMMDLL_OPT_REFRESH_ALL, 1);

	if (canAccessProbeModules(current_process.PID))
		return true;

	if (!s_fixCr3PluginsInitialized)
	{
		if (!VMMDLL_InitializePlugins(this->vHandle))
		{
			LOG("[ERROR] VMMDLL_InitializePlugins call failed.\n");
			return false;
		}
		s_fixCr3PluginsInitialized = true;
	}

	
	std::this_thread::sleep_for(std::chrono::milliseconds(500));

	const auto progressDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
	bool progressReady = false;
	while (std::chrono::steady_clock::now() < progressDeadline)
	{
		BYTE bytes[4] = {0};
		DWORD i = 0;
		auto nt = VMMDLL_VfsReadW(this->vHandle, const_cast<LPWSTR>(L"\\misc\\procinfo\\progress_percent.txt"), bytes, 3, &i, 0);
		if (nt == VMMDLL_STATUS_SUCCESS && atoi(reinterpret_cast<LPSTR>(bytes)) == 100) {
			progressReady = true;
			break;
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}

	if (!progressReady)
	{
		LOG("[WARN] CR3 plugin progress timed out.\n");
		return false;
	}

	VMMDLL_VFS_FILELIST2 VfsFileList = {};
	VfsFileList.dwVersion = VMMDLL_VFS_FILELIST_VERSION;
	VfsFileList.h = 0;
	VfsFileList.pfnAddDirectory = 0;
	VfsFileList.pfnAddFile = cbAddFile; 
	cbSize.store(0x80000, std::memory_order_relaxed);

	const bool result = VMMDLL_VfsListU(this->vHandle, const_cast<LPSTR>("\\misc\\procinfo\\"), &VfsFileList);
	if (!result)
		return false;

	
	const uint64_t dtbTextSize = cbSize.load(std::memory_order_relaxed);
	if (dtbTextSize == 0 || dtbTextSize > 16ULL * 1024ULL * 1024ULL)
	{
		LOG("[WARN] Invalid CR3 DTB list size: %llu.\n", static_cast<unsigned long long>(dtbTextSize));
		return false;
	}

	const size_t buffer_size = static_cast<size_t>(dtbTextSize) + 1u;
	std::vector<BYTE> bytes(buffer_size, 0);
	DWORD j = 0;
	auto nt = VMMDLL_VfsReadW(
		this->vHandle,
		const_cast<LPWSTR>(L"\\misc\\procinfo\\dtb.txt"),
		bytes.data(),
		static_cast<DWORD>(buffer_size - 1u),
		&j,
		0);
	if (nt != VMMDLL_STATUS_SUCCESS)
		return false;

	std::vector<uint64_t> possible_dtbs = { };
	const size_t textSize = std::min(static_cast<size_t>(j), buffer_size - 1u);
	std::string lines(reinterpret_cast<char*>(bytes.data()), textSize);
	std::istringstream iss(lines);
	std::string line = "";

	while (std::getline(iss, line))
	{
		Info info = { };

		std::istringstream info_ss(line);
		if (info_ss >> std::hex >> info.index >> std::dec >> info.process_id >> std::hex >> info.dtb >> info.kernelAddr >> info.name)
		{
			if (info.process_id == 0) 
				possible_dtbs.push_back(info.dtb);
			if (current_process.process_name.find(info.name) != std::string::npos)
				possible_dtbs.push_back(info.dtb);
		}
	}

	
	for (size_t i = 0; i < possible_dtbs.size(); i++)
	{
		auto dtb = possible_dtbs[i];
		VMMDLL_ConfigSet(this->vHandle, VMMDLL_OPT_PROCESS_DTB | current_process.PID, dtb);
		VMMDLL_ConfigSet(this->vHandle, VMMDLL_OPT_REFRESH_ALL, 1);
		if (canAccessProbeModules(current_process.PID))
		{
			LOG("[INFO] DTB remediation completed.\n");
			return true;
		}
	}

	LOG("[WARN] DTB remediation did not find a valid candidate.\n");
	VMMDLL_ConfigSet(this->vHandle, VMMDLL_OPT_PROCESS_DTB | current_process.PID, 0);
	VMMDLL_ConfigSet(this->vHandle, VMMDLL_OPT_REFRESH_ALL, 1);
	return false;
}

bool Memory::Read(uintptr_t address, void* buffer, size_t size) const
{
	if (!vHandle ||
		current_process.PID == 0 ||
		address == 0 ||
		buffer == nullptr ||
		size == 0 ||
		size > MAXDWORD)
		return false;

	DWORD read_size = 0;
	if (!VMMDLL_MemReadEx(
			this->vHandle,
			current_process.PID,
			address,
			static_cast<PBYTE>(buffer),
			static_cast<DWORD>(size),
			&read_size,
			VMMDLL_FLAG_NOCACHE))
	{
		static std::atomic<uint32_t> s_directReadFailureCount = 0;
		const uint32_t failureCount = s_directReadFailureCount.fetch_add(1, std::memory_order_relaxed) + 1u;
		if (!DIRECT_WARN_SUPPRESSED.load(std::memory_order_relaxed) &&
			(failureCount == 1u || (failureCount % 250u) == 0u))
		{
			LOG("[!] Failed to read Memory at 0x%p\n", address);
		}
		return false;
	}

	DMA_TOTAL_BYTES_READ.fetch_add(read_size, std::memory_order_relaxed);
	DMA_DIRECT_READ_COUNT.fetch_add(1, std::memory_order_relaxed);
	return (read_size == size);
}

bool Memory::ReadCached(uintptr_t address, void* buffer, size_t size) const
{
	if (!vHandle ||
		current_process.PID == 0 ||
		address == 0 ||
		buffer == nullptr ||
		size == 0 ||
		size > MAXDWORD)
		return false;

	DWORD read_size = 0;
	if (!VMMDLL_MemReadEx(
			this->vHandle,
			current_process.PID,
			address,
			static_cast<PBYTE>(buffer),
			static_cast<DWORD>(size),
			&read_size,
			0))
	{
		return false;
	}

	DMA_TOTAL_BYTES_READ.fetch_add(read_size, std::memory_order_relaxed);
	DMA_DIRECT_READ_COUNT.fetch_add(1, std::memory_order_relaxed);
	return read_size == size;
}

VMMDLL_SCATTER_HANDLE Memory::CreateScatterHandle() const
{
	if (!vHandle || current_process.PID == 0)
		return nullptr;

	const VMMDLL_SCATTER_HANDLE ScatterHandle = VMMDLL_Scatter_Initialize(this->vHandle, current_process.PID, VMMDLL_FLAG_NOCACHE);
	if (!ScatterHandle) {
		LOG("[!] Failed to create scatter handle\n");
		return nullptr;
	}
	ScatterBatchState* state = FindScatterBatchState(ScatterHandle, true);
	if (!state) {
		VMMDLL_Scatter_CloseHandle(ScatterHandle);
		LOG("[ERROR] Per-thread scatter handle capacity exhausted\n");
		return nullptr;
	}
	state->pid = current_process.PID;
	return ScatterHandle;
}

void Memory::CloseScatterHandle(VMMDLL_SCATTER_HANDLE handle)
{
	if (handle) {
		VMMDLL_Scatter_CloseHandle(handle);
		ResetScatterBatchState(handle, true);
	}
}

void Memory::AddScatterReadRequest(
	VMMDLL_SCATTER_HANDLE handle,
	uint64_t address,
	void* buffer,
	size_t size,
	DWORD* bytesRead)
{
	if (bytesRead)
		*bytesRead = 0;

	ScatterBatchState* state = FindScatterBatchState(handle, true);
	if (!state || state->poisoned)
		return;
	if (state->pid != current_process.PID) {
		state->poisoned = true;
		DMA_SCATTER_SETUP_FAILURES.fetch_add(1, std::memory_order_relaxed);
		return;
	}
	if (!address || !buffer || !size || size > MAXDWORD) {
		++state->prepareFailureCount;
		return;
	}
	auto* request = state->reads.Add(static_cast<DWORD>(size), bytesRead);
	if (!request || !VMMDLL_Scatter_PrepareEx(
			handle, address, static_cast<DWORD>(size),
			static_cast<PBYTE>(buffer), &request->completed)) {
		++state->prepareFailureCount;
		return;
	}
	request->prepared = true;
	++state->pendingCount;
}

bool Memory::ExecuteReadScatter(VMMDLL_SCATTER_HANDLE handle)
{
	if (!handle)
		return false;

	const DWORD pid = current_process.PID;

	ScatterBatchState* state = FindScatterBatchState(handle, true);
	if (!state || state->poisoned)
		return false;
	if (state->pid != pid) {
		state->poisoned = true;
		DMA_SCATTER_SETUP_FAILURES.fetch_add(1, std::memory_order_relaxed);
		return false;
	}
	const uint32_t pending = state->pendingCount;
	const uint32_t prepareFails = state->prepareFailureCount;

	
	
	
	if (pending == 0)
	{
		const bool cleared =
			VMMDLL_Scatter_Clear(handle, pid, VMMDLL_FLAG_NOCACHE) != FALSE;
		if (cleared)
			ResetScatterBatchState(handle, false);
		else
			state->poisoned = true;
		DMA_SCATTER_SETUP_FAILURES.fetch_add(prepareFails + !cleared, std::memory_order_relaxed);
		return cleared && prepareFails == 0;
	}

	const auto startTime = std::chrono::steady_clock::now();
	// FALSE is a setup/handle failure, not the normal partial-page case.
	// Do not sleep and replay the entire batch on the latency-critical lane.
	const bool executed = VMMDLL_Scatter_ExecuteRead(handle) != FALSE;
	const dma::ReadBatchQuality quality = state->reads.Complete(executed);
	const bool cleared = VMMDLL_Scatter_Clear(handle, pid, VMMDLL_FLAG_NOCACHE) != FALSE;
	if (cleared)
		ResetScatterBatchState(handle, false);
	else
		state->poisoned = true;
	const bool success = executed && cleared;

	const auto endTime = std::chrono::steady_clock::now();
	const uint64_t elapsedUs = static_cast<uint64_t>(
		std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime).count());

	DMA_EXECUTE_SCATTER_TOTAL_US.fetch_add(elapsedUs, std::memory_order_relaxed);
	DMA_EXECUTE_SCATTER_COUNT.fetch_add(1, std::memory_order_relaxed);

	UpdateAtomicPeak(DMA_EXECUTE_SCATTER_PEAK_US, elapsedUs);
	UpdateRecentScatterPeak(elapsedUs);

	DMA_TOTAL_BYTES_READ.fetch_add(quality.completedBytes, std::memory_order_relaxed);
	DMA_SCATTER_REQUESTED_BYTES.fetch_add(quality.requestedBytes, std::memory_order_relaxed);
	DMA_SCATTER_COMPLETED_BYTES.fetch_add(quality.completedBytes, std::memory_order_relaxed);
	DMA_SCATTER_REQUESTS.fetch_add(quality.requests, std::memory_order_relaxed);
	DMA_SCATTER_INCOMPLETE_REQUESTS.fetch_add(quality.incompleteRequests, std::memory_order_relaxed);
	DMA_SCATTER_PARTIAL_BATCHES.fetch_add(quality.incompleteRequests != 0, std::memory_order_relaxed);
	DMA_SCATTER_SETUP_FAILURES.fetch_add(prepareFails + !executed + !cleared, std::memory_order_relaxed);
	if (success && prepareFails == 0)
		return true;

	static std::atomic<uint32_t> s_scatterReadFailureCount = 0;
	const uint32_t failureCount =
		s_scatterReadFailureCount.fetch_add(1, std::memory_order_relaxed) + 1u;
	if (!SCATTER_WARN_SUPPRESSED.load(std::memory_order_relaxed) &&
		(failureCount == 1u || (failureCount % 250u) == 0u))
	{
		LOG("[WARN] Scatter read failed (%u total). Check UpdateData stage/reason logs for exact failed block.\n", failureCount);
	}

	
	return false;
}

void Memory::SetScatterReadWarningSuppressed(bool suppressed)
{
	SCATTER_WARN_SUPPRESSED.store(suppressed, std::memory_order_relaxed);
}

void Memory::SetDirectReadWarningSuppressed(bool suppressed)
{
	DIRECT_WARN_SUPPRESSED.store(suppressed, std::memory_order_relaxed);
}

bool Memory::IsDirectReadWarningSuppressed() const noexcept
{
	return DIRECT_WARN_SUPPRESSED.load(std::memory_order_relaxed);
}
