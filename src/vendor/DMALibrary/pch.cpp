

#include "pch.h"

#include <atomic>
#include <cctype>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <cwchar>
#include <cwctype>
#include <mutex>

namespace
{
	std::atomic<DmaLogLevel> g_dmaLogLevel { DmaLogLevel::Warning };
	std::atomic<DmaLogTranslateFn> g_dmaLogTranslator { nullptr };

	const char* TranslateLogText(const char* text)
	{
		const DmaLogTranslateFn translator = g_dmaLogTranslator.load(std::memory_order_acquire);
		return translator && text ? translator(text) : (text ? text : "");
	}

	template <typename CharT>
	void TrimTrailingLineBreaks(std::basic_string<CharT>& value)
	{
		while (!value.empty()) {
			const CharT ch = value.back();
			if (ch == static_cast<CharT>('\n') || ch == static_cast<CharT>('\r'))
				value.pop_back();
			else
				break;
		}
	}

	DmaLogLevel ParseLogLevel(const char* raw)
	{
		if (!raw || !raw[0])
			return DmaLogLevel::Warning;

		std::string value(raw);
		for (char& c : value)
			c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

		if (value == "error")
			return DmaLogLevel::Error;
		if (value == "warn" || value == "warning")
			return DmaLogLevel::Warning;
		if (value == "debug")
			return DmaLogLevel::Debug;
		if (value == "info")
			return DmaLogLevel::Info;
		if (value == "silent" || value == "off" || value == "none")
			return DmaLogLevel::Silent;
		return DmaLogLevel::Warning;
	}

	DmaLogLevel LevelFromMessage(const char* fmt)
	{
		if (!fmt)
			return DmaLogLevel::Info;
		if (std::strncmp(fmt, "[ERROR]", 7) == 0)
			return DmaLogLevel::Error;
		if (std::strncmp(fmt, "[WARN]", 6) == 0 || std::strncmp(fmt, "[!]", 3) == 0 || std::strncmp(fmt, "[-]", 3) == 0)
			return DmaLogLevel::Warning;
		if (std::strncmp(fmt, "[DEBUG]", 7) == 0)
			return DmaLogLevel::Debug;
		if (std::strncmp(fmt, "[PERF]", 6) == 0)
			return DmaLogLevel::Info;
		return DmaLogLevel::Info;
	}

	DmaLogLevel LevelFromMessage(const wchar_t* fmt)
	{
		if (!fmt)
			return DmaLogLevel::Info;
		if (wcsncmp(fmt, L"[ERROR]", 7) == 0)
			return DmaLogLevel::Error;
		if (wcsncmp(fmt, L"[WARN]", 6) == 0 || wcsncmp(fmt, L"[!]", 3) == 0 || wcsncmp(fmt, L"[-]", 3) == 0)
			return DmaLogLevel::Warning;
		if (wcsncmp(fmt, L"[DEBUG]", 7) == 0)
			return DmaLogLevel::Debug;
		if (wcsncmp(fmt, L"[PERF]", 6) == 0)
			return DmaLogLevel::Info;
		return DmaLogLevel::Info;
	}

	bool ShouldPrint(DmaLogLevel msgLevel)
	{
		const DmaLogLevel currentLevel = g_dmaLogLevel.load();
		if (currentLevel == DmaLogLevel::Silent)
			return false;
		return static_cast<int>(msgLevel) <= static_cast<int>(currentLevel);
	}

	const char* PrefixToStrip(const char* fmt)
	{
		if (!fmt)
			return "";
		if (std::strncmp(fmt, "[ERROR]", 7) == 0)
			return fmt + 7;
		if (std::strncmp(fmt, "[WARN]", 6) == 0)
			return fmt + 6;
		if (std::strncmp(fmt, "[INFO]", 6) == 0)
			return fmt + 6;
		if (std::strncmp(fmt, "[DEBUG]", 7) == 0)
			return fmt + 7;
		if (std::strncmp(fmt, "[PERF]", 6) == 0)
			return fmt + 6;
		if (std::strncmp(fmt, "[!]", 3) == 0 || std::strncmp(fmt, "[-]", 3) == 0 || std::strncmp(fmt, "[+]", 3) == 0)
			return fmt + 3;
		return fmt;
	}

	const wchar_t* PrefixToStrip(const wchar_t* fmt)
	{
		if (!fmt)
			return L"";
		if (wcsncmp(fmt, L"[ERROR]", 7) == 0)
			return fmt + 7;
		if (wcsncmp(fmt, L"[WARN]", 6) == 0)
			return fmt + 6;
		if (wcsncmp(fmt, L"[INFO]", 6) == 0)
			return fmt + 6;
		if (wcsncmp(fmt, L"[DEBUG]", 7) == 0)
			return fmt + 7;
		if (wcsncmp(fmt, L"[PERF]", 6) == 0)
			return fmt + 6;
		if (wcsncmp(fmt, L"[!]", 3) == 0 || wcsncmp(fmt, L"[-]", 3) == 0 || wcsncmp(fmt, L"[+]", 3) == 0)
			return fmt + 3;
		return fmt;
	}

	std::string NormalizeLogFormat(const char* format)
	{
		const char* body = PrefixToStrip(format);
		while (*body != '\0' &&
			   std::isspace(static_cast<unsigned char>(*body)))
			++body;

		std::string normalized(body);
		TrimTrailingLineBreaks(normalized);
		return normalized;
	}

	const char* LabelForLevel(DmaLogLevel level)
	{
		switch (level) {
		case DmaLogLevel::Error:   return TranslateLogText("Error");
		case DmaLogLevel::Warning: return TranslateLogText("Warning");
		case DmaLogLevel::Debug:   return TranslateLogText("Debug");
		case DmaLogLevel::Info:
		default:                   return TranslateLogText("Info");
		}
	}

	std::string WideToUtf8(std::wstring_view value)
	{
		if (value.empty())
			return {};
		const int required = WideCharToMultiByte(
			CP_UTF8,
			WC_ERR_INVALID_CHARS,
			value.data(),
			static_cast<int>(value.size()),
			nullptr,
			0,
			nullptr,
			nullptr);
		if (required <= 0)
			return {};
		std::string result(static_cast<size_t>(required), '\0');
		if (WideCharToMultiByte(
				CP_UTF8,
				WC_ERR_INVALID_CHARS,
				value.data(),
				static_cast<int>(value.size()),
				result.data(),
				required,
				nullptr,
				nullptr) != required) {
			return {};
		}
		return result;
	}

	void ApplyColorForLevel(DmaLogLevel level, WORD* outOld);
	void RestoreColor(WORD oldAttributes);

	std::mutex g_logMutex;
	std::mutex g_consoleMutex;

	void WriteUtf8ToStdoutUnlocked(std::string_view text)
	{
		if (text.empty())
			return;

		HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
		DWORD consoleMode = 0;
		if (output != nullptr &&
			output != INVALID_HANDLE_VALUE &&
			GetConsoleMode(output, &consoleMode)) {
			const int required = MultiByteToWideChar(
				CP_UTF8,
				MB_ERR_INVALID_CHARS,
				text.data(),
				static_cast<int>(text.size()),
				nullptr,
				0);
			if (required > 0) {
				std::wstring wide(static_cast<size_t>(required), L'\0');
				if (MultiByteToWideChar(
						CP_UTF8,
						MB_ERR_INVALID_CHARS,
						text.data(),
						static_cast<int>(text.size()),
						wide.data(),
						required) == required) {
					DWORD written = 0;
					if (WriteConsoleW(
							output,
							wide.data(),
							static_cast<DWORD>(wide.size()),
							&written,
							nullptr)) {
						return;
					}
				}
			}
		}

		std::fwrite(text.data(), 1, text.size(), stdout);
		std::fflush(stdout);
	}

	std::string GetTimestampString()
	{
		SYSTEMTIME st{};
		GetLocalTime(&st);
		char timeBuf[64] = {};
		std::snprintf(timeBuf, sizeof(timeBuf), "[%04u-%02u-%02u %02u:%02u:%02u.%03u] ",
					  st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
		return timeBuf;
	}

	void WriteLogToFile(const std::string& line)
	{
		std::lock_guard<std::mutex> lock(g_logMutex);

		wchar_t exePath[MAX_PATH] = {};
		GetModuleFileNameW(nullptr, exePath, MAX_PATH);
		wchar_t* lastSlash = wcsrchr(exePath, L'\\');
		if (lastSlash) lastSlash[1] = L'\0';
		else exePath[0] = L'\0';

		std::wstring logPathW = std::wstring(exePath) + L"KevqDMA.log";

		std::error_code ec;
		if (std::filesystem::exists(logPathW, ec)) {
			uintmax_t size = std::filesystem::file_size(logPathW, ec);
			if (!ec && size >= 5 * 1024 * 1024) { 
				std::wstring logPath3 = std::wstring(exePath) + L"KevqDMA.log.3";
				std::wstring logPath2 = std::wstring(exePath) + L"KevqDMA.log.2";
				std::wstring logPath1 = std::wstring(exePath) + L"KevqDMA.log.1";

				std::filesystem::remove(logPath3, ec);
				std::filesystem::rename(logPath2, logPath3, ec);
				std::filesystem::rename(logPath1, logPath2, ec);
				std::filesystem::rename(logPathW, logPath1, ec);
			}
		}

		FILE* file = nullptr;
		if (_wfopen_s(&file, logPathW.c_str(), L"ab") == 0 && file) {
			std::fwrite(line.data(), 1, line.size(), file);
			std::fclose(file);
		}
	}

	void PrintStyledLine(const std::string& message, DmaLogLevel level)
	{
		std::lock_guard<std::mutex> consoleLock(g_consoleMutex);
		std::string ts = GetTimestampString();
		WORD oldAttributes = 0;
		WriteUtf8ToStdoutUnlocked("\r  ");
		WriteUtf8ToStdoutUnlocked(ts);
		ApplyColorForLevel(level, &oldAttributes);
		WriteUtf8ToStdoutUnlocked("| ");
		WriteUtf8ToStdoutUnlocked(LabelForLevel(level));
		WriteUtf8ToStdoutUnlocked(" | ");
		RestoreColor(oldAttributes);
		WriteUtf8ToStdoutUnlocked(message);
		WriteUtf8ToStdoutUnlocked("                    \n");

		std::string fileLine = ts + "| " + LabelForLevel(level) + " | " + message + "\n";
		WriteLogToFile(fileLine);
	}

	void PrintStyledLine(const std::wstring& message, DmaLogLevel level)
	{
		PrintStyledLine(WideToUtf8(message), level);
	}

	WORD ColorForLevel(DmaLogLevel level)
	{
		switch (level) {
		case DmaLogLevel::Error:   return FOREGROUND_RED | FOREGROUND_INTENSITY;
		case DmaLogLevel::Warning: return FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY;
		case DmaLogLevel::Info:    return FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
		case DmaLogLevel::Debug:   return FOREGROUND_BLUE | FOREGROUND_GREEN;
		default:                   return FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
		}
	}

	void ApplyColorForLevel(DmaLogLevel level, WORD* outOld)
	{
		if (outOld)
			*outOld = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;

		HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
		if (h == INVALID_HANDLE_VALUE)
			return;

		CONSOLE_SCREEN_BUFFER_INFO info = {};
		if (GetConsoleScreenBufferInfo(h, &info) && outOld)
			*outOld = info.wAttributes;

		SetConsoleTextAttribute(h, ColorForLevel(level));
	}

	void RestoreColor(WORD oldAttributes)
	{
		HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
		if (h == INVALID_HANDLE_VALUE)
			return;
		SetConsoleTextAttribute(h, oldAttributes);
	}

	struct DmaLogInit
	{
		DmaLogInit()
		{
			char* envValue = nullptr;
			size_t envLen = 0;
			if (_dupenv_s(&envValue, &envLen, "KEVQDMA_LOG_LEVEL") == 0 && envValue)
			{
				g_dmaLogLevel.store(ParseLogLevel(envValue));
			}
			if (envValue)
				free(envValue);
		}
	} g_dmaLogInit;
}

void DmaSetLogLevel(DmaLogLevel level)
{
	g_dmaLogLevel.store(level);
}

DmaLogLevel DmaGetLogLevel()
{
	return g_dmaLogLevel.load();
}

void DmaSetLogTranslator(DmaLogTranslateFn translator)
{
	g_dmaLogTranslator.store(translator, std::memory_order_release);
}

void DmaConsoleWriteUtf8(const char* text)
{
	if (!text)
		return;
	std::lock_guard<std::mutex> consoleLock(g_consoleMutex);
	WriteUtf8ToStdoutUnlocked(text);
}

void DmaLogPrintf(const char* fmt, ...)
{
	const DmaLogLevel level = LevelFromMessage(fmt);
	if (!fmt || !ShouldPrint(level))
		return;

	va_list args;
	va_start(args, fmt);
	char buffer[4096] = {};
	const std::string normalizedFormat = NormalizeLogFormat(fmt);
	std::vsnprintf(
		buffer,
		sizeof(buffer),
		TranslateLogText(normalizedFormat.c_str()),
		args);
	va_end(args);
	std::string message(buffer);
	while (!message.empty() && std::isspace(static_cast<unsigned char>(message.front())))
		message.erase(message.begin());
	TrimTrailingLineBreaks(message);
	if (message.empty())
		return;
	PrintStyledLine(message, level);
}

void DmaLogWPrintf(const wchar_t* fmt, ...)
{
	const DmaLogLevel level = LevelFromMessage(fmt);
	if (!fmt || !ShouldPrint(level))
		return;

	va_list args;
	va_start(args, fmt);
	wchar_t buffer[4096] = {};
	_vsnwprintf_s(buffer, _countof(buffer), _TRUNCATE, PrefixToStrip(fmt), args);
	va_end(args);
	std::wstring message(buffer);
		while (!message.empty() && std::iswspace(message.front()))
			message.erase(message.begin());
	TrimTrailingLineBreaks(message);
	if (message.empty())
		return;
	PrintStyledLine(message, level);
}
