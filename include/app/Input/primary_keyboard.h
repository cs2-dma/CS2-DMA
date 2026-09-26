#pragma once

#include <cstdint>
#include "app/Input/input_device_policy.h"

namespace app::input
{
    struct PrimaryKeyboardStatus
    {
        bool ready = false;
        uint32_t sourcePid = 0;
        uint32_t consecutiveReadFailures = 0;
        uint64_t publishGeneration = 0;
        bool resolving = false;
        uint64_t resolveAttempts = 0;
        uint64_t unreadablePages = 0;
    };

    bool InitializePrimaryKeyboard();
    void ResetPrimaryKeyboard();
    void StartPrimaryKeyboardRecovery();
    void StopPrimaryKeyboardRecovery();

    // Poll is a single 64-byte read. The data worker calls it while holding the
    // shared DMA lifecycle lock; render/UI consumers only read atomics.
    bool PollPrimaryKeyboard();
    bool IsPrimaryKeyDown(int virtualKey);
    KeyState ReadPrimaryKeyState(int virtualKey);
    PrimaryKeyboardStatus GetPrimaryKeyboardStatus();
}
