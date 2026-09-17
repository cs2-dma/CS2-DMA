#pragma once

struct _EXCEPTION_POINTERS;

namespace bootstrap {

void InstallCrashHandler();
void WriteCrashLog(const char* type, const char* message, struct _EXCEPTION_POINTERS* info);

}
