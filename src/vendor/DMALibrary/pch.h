





#ifndef PCH_H
#define PCH_H

#include <Windows.h>
using NTSTATUS = LONG;

#include "libs/vmmdll.h"
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <sstream>
#include <fstream>
#include <filesystem>

enum class DmaLogLevel : uint8_t
{
	Error = 0,
	Warning = 1,
	Info = 2,
	Debug = 3,
	Silent = 255
};

void DmaSetLogLevel(DmaLogLevel level);
DmaLogLevel DmaGetLogLevel();
using DmaLogTranslateFn = const char* (*)(const char*);
void DmaSetLogTranslator(DmaLogTranslateFn translator);
void DmaConsoleWriteUtf8(const char* text);
void DmaLogPrintf(const char* fmt, ...);
void DmaLogWPrintf(const wchar_t* fmt, ...);

#define LOG(fmt, ...) DmaLogPrintf(fmt, ##__VA_ARGS__)
#define LOGW(fmt, ...) DmaLogWPrintf(fmt, ##__VA_ARGS__)

#define THROW_EXCEPTION
#ifdef THROW_EXCEPTION
#define THROW(fmt, ...) do { \
    char _throw_buf[512]; \
    std::snprintf(_throw_buf, sizeof(_throw_buf), fmt, ##__VA_ARGS__); \
    throw std::runtime_error(_throw_buf); \
} while(0)
#endif

#endif 
