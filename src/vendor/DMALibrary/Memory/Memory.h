#pragma once
#include "../pch.h"
#include <atomic>

class Memory
{
private:
	struct DmaInitStats
	{
		bool usedMemMap = false;
		bool reusedMemMapCache = false;
		uint64_t runtimeLibsMs = 0;
		uint64_t memMapMs = 0;
		uint64_t vmmInitMs = 0;
		uint64_t fpgaConfigMs = 0;
		uint64_t totalMs = 0;
	};

	struct LibModules
	{
		HMODULE VMM = nullptr;
		HMODULE LEECHCORE = nullptr;
		HMODULE FTD3XXWU = nullptr;
		HMODULE FTD3XX = nullptr;
	};

	static LibModules modules;

	struct CurrentProcessInformation
	{
		DWORD PID = 0;
		size_t base_address = 0;
		size_t base_size = 0;
		std::string process_name = "";
	};

	static CurrentProcessInformation current_process;

	static inline BOOLEAN DMA_INITIALIZED = FALSE;
	static inline BOOLEAN PROCESS_INITIALIZED = FALSE;
	static inline std::atomic<bool> SCATTER_WARN_SUPPRESSED = false;
	static inline std::atomic<bool> DIRECT_WARN_SUPPRESSED = false;
	static DmaInitStats LAST_DMA_INIT_STATS;
	
	bool DumpMemoryMap(const std::string& outputPath, bool debug = false);
	bool EnsureRuntimeLibrariesLoaded();
	bool SetFPGA();
	size_t GetModuleSize(const std::string& moduleName);

public:
	static inline std::atomic<uint64_t> DMA_EXECUTE_SCATTER_TOTAL_US = 0;
	static inline std::atomic<uint64_t> DMA_EXECUTE_SCATTER_PEAK_US = 0;
	static inline std::atomic<uint64_t> DMA_EXECUTE_SCATTER_RECENT_PEAK_US = 0;
	static inline std::atomic<uint64_t> DMA_EXECUTE_SCATTER_RECENT_WINDOW_START_US = 0;
	static inline std::atomic<uint64_t> DMA_EXECUTE_SCATTER_COUNT = 0;
	static inline std::atomic<uint64_t> DMA_EXECUTE_SCATTER_RECENT_COUNT = 0;
	static inline std::atomic<uint64_t> DMA_EXECUTE_SCATTER_OVER_1MS_RECENT_COUNT = 0;
	static inline std::atomic<uint64_t> DMA_EXECUTE_SCATTER_OVER_BUDGET_RECENT_COUNT = 0;
	static inline std::atomic<uint64_t> DMA_EXECUTE_SCATTER_OVER_5MS_RECENT_COUNT = 0;
	static inline std::atomic<uint64_t> DMA_EXECUTE_SCATTER_OVER_16MS_RECENT_COUNT = 0;
	static inline std::atomic<uint64_t> DMA_TOTAL_BYTES_READ = 0;
	static inline std::atomic<uint64_t> DMA_DIRECT_READ_COUNT = 0;
	static inline std::atomic<uint64_t> DMA_SCATTER_BUDGET_US = 4000;
	static inline std::atomic<uint64_t> DMA_SCATTER_REQUESTED_BYTES = 0;
	static inline std::atomic<uint64_t> DMA_SCATTER_COMPLETED_BYTES = 0;
	static inline std::atomic<uint64_t> DMA_SCATTER_REQUESTS = 0;
	static inline std::atomic<uint64_t> DMA_SCATTER_INCOMPLETE_REQUESTS = 0;
	static inline std::atomic<uint64_t> DMA_SCATTER_PARTIAL_BATCHES = 0;
	static inline std::atomic<uint64_t> DMA_SCATTER_SETUP_FAILURES = 0;

	Memory() = default;
	~Memory();

	
	bool InitDma(bool memMap = true, bool debug = false);

	
	bool AttachToProcess(const std::string& process_name, bool applyCr3Fix = true);
	bool AttachToProcessId(
		const std::string& process_name,
		DWORD processId,
		bool applyCr3Fix = true);
	void ResetProcessState();
	DWORD GetAttachedPid() const { return current_process.PID; }
	void CloseDma();

	

	
	DWORD GetPidFromName(const std::string& process_name);

	
	size_t GetModuleBase(const std::string& moduleName);

	
	bool FixCr3();

	
	bool Read(uintptr_t address, void* buffer, size_t size) const;
	bool ReadCached(uintptr_t address, void* buffer, size_t size) const;

	
	VMMDLL_SCATTER_HANDLE CreateScatterHandle() const;

	
	void CloseScatterHandle(VMMDLL_SCATTER_HANDLE handle);

	
	void AddScatterReadRequest(
		VMMDLL_SCATTER_HANDLE handle,
		uint64_t address,
		void* buffer,
		size_t size,
		DWORD* bytesRead = nullptr);

	template <typename T>
	void AddScatterReadRequest(VMMDLL_SCATTER_HANDLE handle, uint64_t address, T* buffer)
	{
		AddScatterReadRequest(handle, address, reinterpret_cast<void*>(buffer), sizeof(T));
	}
		
	
	// Returns dispatch/handle status, NOT all-fields-complete. Consumers must
	// check their bytesRead before publishing a value (zero is also valid data).
	bool ExecuteReadScatter(VMMDLL_SCATTER_HANDLE handle);
	void SetScatterReadWarningSuppressed(bool suppressed);
	void SetDirectReadWarningSuppressed(bool suppressed);
	bool IsDirectReadWarningSuppressed() const noexcept;

	
	VMM_HANDLE vHandle = nullptr;
};

inline Memory mem;
