    using Clock = std::chrono::steady_clock;
    auto nextLimitedFrame = Clock::now();
    bool previousVsync = g::vsyncEnabled;
    int previousFpsLimit = g::fpsLimit;
    int previousOverlayKey = g::overlayToggleKey;
    int previousMenuKey = g::menuToggleKey;
    const auto localKeyDown = [](int key) {
        return key > 0 && key <= 0xFE &&
            app::input::IsLocalControlKeyDown(key, GetAsyncKeyState(key));
    };
    bool overlayKeyWasDown = localKeyDown(previousOverlayKey);
    bool menuKeyWasDown = localKeyDown(previousMenuKey);

    const auto controlKeyPressed = [&](
        int key,
        int& trackedKey,
        bool& wasDown) {
        // Screen/Menu belong to the overlay PC, not the game's DMA bitmap.
        const bool down = localKeyDown(key);
        if (trackedKey != key) {
            trackedKey = key;
            wasDown = down;
            return false;
        }
        const bool pressed = down && !wasDown;
        wasDown = down;
        return pressed;
    };

    MSG msg = {};
    while (g::running) {
        const auto frameStart = Clock::now();
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT)
                g::running = false;
        }
        if (!g::running)
            break;

        if (controlKeyPressed(
                g::overlayToggleKey,
                previousOverlayKey,
                overlayKeyWasDown))
            ApplyOverlayWindowMode(!s_overlayVisible);
        if (controlKeyPressed(
                g::menuToggleKey,
                previousMenuKey,
                menuKeyWasDown))
            g::menuOpen = !g::menuOpen;
        if (GetAsyncKeyState(VK_END) & 1)
            g::running = false;
        const auto syncStart = Clock::now();
        SyncOverlayBounds();
        const auto syncEnd = Clock::now();
        if (!s_rtv) {
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
            continue;
        }

        const bool vsyncEnabled = g::vsyncEnabled;
        const int fpsLimit = std::clamp(g::fpsLimit, 0, 500);
        if (vsyncEnabled != previousVsync || fpsLimit != previousFpsLimit) {
            nextLimitedFrame = Clock::now();
            previousVsync = vsyncEnabled;
            previousFpsLimit = fpsLimit;
        }

        uint64_t pacingWaitUs = 0;
        if (vsyncEnabled && s_waitableSwapChainEnabled && s_frameLatencyWaitableObject) {
            const auto pacingStart = Clock::now();
            const DWORD waitResult = WaitForSingleObjectEx(
                s_frameLatencyWaitableObject.Get(),
                100,
                FALSE);
            if (waitResult == WAIT_OBJECT_0) {
                pacingWaitUs = static_cast<uint64_t>(
                    std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - pacingStart).count());
            } else if (waitResult == WAIT_FAILED || waitResult == WAIT_ABANDONED) {
                s_waitableSwapChainEnabled = false;
            }
        } else if (!vsyncEnabled && fpsLimit > 0) {
            const auto frameInterval =
                std::chrono::microseconds(1000000 / fpsLimit);
            const auto pacingStart = Clock::now();
            if (nextLimitedFrame + frameInterval < pacingStart)
                nextLimitedFrame = pacingStart;
            if (nextLimitedFrame > pacingStart)
                WaitForOverlayDeadline(nextLimitedFrame);
            pacingWaitUs = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(
                    Clock::now() - pacingStart).count());
            nextLimitedFrame += frameInterval;
        } else {
            nextLimitedFrame = Clock::now();
        }

        const auto drawStart = Clock::now();
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        webradar::ApplySettingsFromGlobals();
        webradar::CaptureFromEsp();

        {
            std::lock_guard<std::recursive_mutex> lock(g::settingsMutex);
            esp::Draw();
            target::DrawOverlay();
            ui::RenderMenu();
            esp::PublishDataSettingsSnapshot();
        }
        if (g::menuOpen.load(std::memory_order_relaxed))
            config::SaveIfDirty();

        ImGui::Render();

        
        
        const float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
        ID3D11RenderTargetView* rtvRaw = s_rtv.Get();
        s_context->OMSetRenderTargets(1, &rtvRaw, nullptr);
        s_context->ClearRenderTargetView(s_rtv.Get(), clearColor);

        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        const auto drawEnd = Clock::now();

        const auto presentStart = Clock::now();
        if (vsyncEnabled) {
            s_swapChain->Present(1, 0);
        } else {
            const UINT flags = s_tearingSupported ? DXGI_PRESENT_ALLOW_TEARING : 0u;
            s_swapChain->Present(0, flags);
        }
        const auto presentEnd = Clock::now();
        const auto frameEnd = Clock::now();
        const uint64_t frameUs = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(frameEnd - frameStart).count());
        const uint64_t syncUs = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(syncEnd - syncStart).count());
        const uint64_t drawUs = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(drawEnd - drawStart).count());
        const uint64_t presentUs = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(presentEnd - presentStart).count());
        s_overlayFrameUs.store(frameUs, std::memory_order_relaxed);
        s_overlaySyncUs.store(syncUs, std::memory_order_relaxed);
        s_overlayDrawUs.store(drawUs, std::memory_order_relaxed);
        s_overlayPresentUs.store(presentUs, std::memory_order_relaxed);
        s_overlayPacingWaitUs.store(pacingWaitUs, std::memory_order_relaxed);
        uint64_t prevMax = s_overlayMaxFrameUs.load(std::memory_order_relaxed);
        while (frameUs > prevMax &&
               !s_overlayMaxFrameUs.compare_exchange_weak(prevMax, frameUs, std::memory_order_relaxed))
            ;
    }
