    if (outErrorMessage)
        outErrorMessage->clear();

    struct OverlayCreateRollback {
        bool committed = false;

        ~OverlayCreateRollback()
        {
            if (!committed)
                overlay::Destroy();
        }
    } rollback;

    const HINSTANCE hInst = GetModuleHandleW(nullptr);
    const HICON hIcon = LoadIconW(hInst, L"IDI_APPICON");

    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance      = hInst;
    wc.lpszClassName  = L"KevqDMA_Overlay";
    wc.hCursor        = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground  = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.hIcon          = hIcon;
    wc.hIconSm        = hIcon;
    const ATOM classAtom = RegisterClassExW(&wc);
    if (classAtom == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        if (outErrorMessage)
            *outErrorMessage = "Failed to register the overlay window class.";
        return false;
    }
    s_windowClassRegistered = true;

    const OverlayTargetBounds bounds = ResolveOverlayTargetBounds();
    const int targetX = bounds.x;
    const int targetY = bounds.y;
    const int targetWidth = bounds.width;
    const int targetHeight = bounds.height;
    if (targetWidth <= 0 || targetHeight <= 0) {
        if (outErrorMessage)
            *outErrorMessage = "Failed to resolve valid overlay display bounds.";
        return false;
    }

    width = targetWidth;
    height = targetHeight;

    static const std::wstring windowTitle = Utf8ToWide(app::build_info::RuntimeTitle());

    s_hwnd = CreateWindowExW(
        0,
        wc.lpszClassName,
        windowTitle.c_str(),
        WS_POPUP | WS_VISIBLE,
        targetX, targetY, targetWidth, targetHeight,
        nullptr, nullptr, wc.hInstance, nullptr);

    if (!s_hwnd) {
        if (outErrorMessage) *outErrorMessage = "Failed to create overlay window (CreateWindowExW failed).";
        return false;
    }

    ShowWindow(s_hwnd, SW_SHOWNA);
    UpdateWindow(s_hwnd);

    s_tearingSupported = CheckTearingSupport();

    D3D_FEATURE_LEVEL featureLevel;
    HRESULT hr = D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
        nullptr, 0, D3D11_SDK_VERSION,
        &s_device, &featureLevel, &s_context);
    if (FAILED(hr)) {
        hr = D3D11CreateDevice(
            nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
            nullptr, 0, D3D11_SDK_VERSION,
            &s_device, &featureLevel, &s_context);
    }
    if (FAILED(hr)) {
        if (outErrorMessage)
            *outErrorMessage = "Failed to initialize DirectX 11 device. Install current NVIDIA, AMD, or Intel graphics drivers and verify DirectX 11 support.";
        return false;
    }

    // Optional visual asset: ESP retains its font/text fallback if the
    // embedded atlas cannot be uploaded on a particular graphics adapter.
    esp::render::weapon_icons::Initialize(s_device.Get());

    ComPtr<IDXGIDevice> dxgiDevice;
    ComPtr<IDXGIAdapter> dxgiAdapter;
    ComPtr<IDXGIFactory2> dxgiFactory;

    hr = s_device.As(&dxgiDevice);
    if (FAILED(hr)) {
        if (outErrorMessage) *outErrorMessage = "Failed to query DXGI device interface from D3D11 device.";
        return false;
    }

    hr = dxgiDevice->GetAdapter(&dxgiAdapter);
    if (FAILED(hr)) {
        if (outErrorMessage) *outErrorMessage = "Failed to retrieve DXGI adapter.";
        return false;
    }

    hr = dxgiAdapter->GetParent(IID_PPV_ARGS(&dxgiFactory));
    if (FAILED(hr)) {
        if (outErrorMessage) *outErrorMessage = "Failed to retrieve DXGI factory.";
        return false;
    }

    DXGI_SWAP_CHAIN_DESC1 sd1 = {};
    sd1.Width       = width;
    sd1.Height      = height;
    sd1.Format      = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd1.SampleDesc  = { 1, 0 };
    sd1.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd1.BufferCount = 2;
    sd1.SwapEffect  = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    UINT swapChainFlags =
        (s_tearingSupported ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0u) |
        DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
    sd1.Flags = swapChainFlags;
    hr = dxgiFactory->CreateSwapChainForHwnd(s_device.Get(), s_hwnd, &sd1, nullptr, nullptr, &s_swapChain);
    if (FAILED(hr)) {
        swapChainFlags &= ~DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
        sd1.Flags = swapChainFlags;
        hr = dxgiFactory->CreateSwapChainForHwnd(s_device.Get(), s_hwnd, &sd1, nullptr, nullptr, &s_swapChain);
    }
    if (FAILED(hr)) {
        if (outErrorMessage) *outErrorMessage = "Failed to create DXGI swap chain.";
        return false;
    }

    if (dxgiFactory) {
        dxgiFactory->MakeWindowAssociation(s_hwnd, DXGI_MWA_NO_ALT_ENTER);
    }

    s_swapChain2.Reset();
    s_waitableSwapChainEnabled = false;
    s_frameLatencyWaitableObject.Reset();
    if ((swapChainFlags & DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT) != 0u &&
        SUCCEEDED(s_swapChain.As(&s_swapChain2)) &&
        s_swapChain2 &&
        SUCCEEDED(s_swapChain2->SetMaximumFrameLatency(1))) {
        s_frameLatencyWaitableObject.Reset(s_swapChain2->GetFrameLatencyWaitableObject());
        s_waitableSwapChainEnabled = static_cast<bool>(s_frameLatencyWaitableObject);
        if (!s_waitableSwapChainEnabled)
            s_swapChain2.Reset();
    }

    ComPtr<ID3D11Texture2D> backBuffer;
    hr = s_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    if (FAILED(hr) || !backBuffer) {
        if (outErrorMessage) *outErrorMessage = "Failed to retrieve back buffer from swap chain.";
        return false;
    }

    hr = s_device->CreateRenderTargetView(backBuffer.Get(), nullptr, &s_rtv);
    if (FAILED(hr)) {
        if (outErrorMessage) *outErrorMessage = "Failed to create render target view.";
        return false;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    s_imguiContextCreated = true;
    ImGuiIO& io = ImGui::GetIO();
    static std::string s_imguiIniPath;
    if (s_imguiIniPath.empty()) {
        const auto iniPath = ResolveImGuiIniPath();
        if (!iniPath.empty())
            s_imguiIniPath = iniPath.string();
    }
    io.IniFilename = s_imguiIniPath.empty() ? nullptr : s_imguiIniPath.c_str();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    static const ImWchar glyphRanges[] = {
        0x0020, 0x00FF,
        0x0400, 0x052F,
        0x2000, 0x206F,
        0x2190, 0x21FF,
        0x2500, 0x257F,
        0x25A0, 0x25FF,
        0x2600, 0x26FF,
        0x2700, 0x27BF,
        0
    };
    static ImVector<ImWchar> s_cjkGlyphRanges;
    if (s_cjkGlyphRanges.empty()) {
        ImFontGlyphRangesBuilder builder;
        const std::string catalogText =
            app::localization::CollectCatalogGlyphText();
        builder.AddText(catalogText.c_str());

        // Keep CJK isolated from the Latin UI face so language switching does
        // not change English typography.
        constexpr size_t kCjkStart = 0x2E80u;
        for (size_t codepoint = 0; codepoint < kCjkStart; ++codepoint) {
            const size_t word = codepoint >> 5u;
            const ImU32 mask = 1u << (codepoint & 31u);
            builder.UsedChars[static_cast<int>(word)] &= ~mask;
        }
        builder.BuildRanges(&s_cjkGlyphRanges);
    }
    const UINT windowDpi = s_hwnd ? GetDpiForWindow(s_hwnd) : USER_DEFAULT_SCREEN_DPI;
    const float dpiScale = std::clamp(
        static_cast<float>(windowDpi) / static_cast<float>(USER_DEFAULT_SCREEN_DPI),
        1.0f,
        2.0f);
    const float baseFontSize = 16.0f * dpiScale;
    const float titleFontSize = 21.0f * dpiScale;

    std::string segoeRegularPath, segoeSemiboldPath, segoeBoldPath;
    std::string cjkRegularPath, cjkSemiboldPath;
    {
        PWSTR fontsDir = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Fonts, 0, nullptr, &fontsDir))) {
            std::filesystem::path fontsPath(fontsDir);
            CoTaskMemFree(fontsDir);
            segoeRegularPath = (fontsPath / "segoeui.ttf").string();
            segoeSemiboldPath = (fontsPath / "seguisb.ttf").string();
            segoeBoldPath = (fontsPath / "segoeuib.ttf").string();
            const std::filesystem::path cjkRegularCandidates[] = {
                fontsPath / "msyh.ttc",
                fontsPath / "msyhl.ttc",
                fontsPath / "simsun.ttc"
            };
            const std::filesystem::path cjkSemiboldCandidates[] = {
                fontsPath / "msyhbd.ttc",
                fontsPath / "msyh.ttc",
                fontsPath / "simsun.ttc"
            };
            for (const auto& candidate : cjkRegularCandidates) {
                if (std::filesystem::exists(candidate)) {
                    cjkRegularPath = candidate.string();
                    break;
                }
            }
            for (const auto& candidate : cjkSemiboldCandidates) {
                if (std::filesystem::exists(candidate)) {
                    cjkSemiboldPath = candidate.string();
                    break;
                }
            }
        } else {
            segoeRegularPath = "C:\\Windows\\Fonts\\segoeui.ttf";
            segoeSemiboldPath = "C:\\Windows\\Fonts\\seguisb.ttf";
            segoeBoldPath = "C:\\Windows\\Fonts\\segoeuib.ttf";
            cjkRegularPath = "C:\\Windows\\Fonts\\msyh.ttc";
            cjkSemiboldPath = "C:\\Windows\\Fonts\\msyhbd.ttc";
        }
    }
    if (!std::filesystem::exists(segoeRegularPath))
        segoeRegularPath.clear();
    if (!std::filesystem::exists(segoeSemiboldPath))
        segoeSemiboldPath = std::filesystem::exists(segoeBoldPath)
            ? segoeBoldPath
            : segoeRegularPath;
    if (!std::filesystem::exists(segoeBoldPath))
        segoeBoldPath = segoeSemiboldPath;

    g::fontDefault = nullptr;
    g::fontUiSemibold = nullptr;
    g::fontUiTitle = nullptr;
    g::fontEspName = nullptr;
    g::fontOverlayText = nullptr;
    g::fontUiIcons = nullptr;
    g::fontWeaponIcons = nullptr;
    g::fontWeaponIconsSmall = nullptr;
    g::fontWeaponIconsLarge = nullptr;

    if (!segoeRegularPath.empty()) {
        g::fontDefault = io.Fonts->AddFontFromFileTTF(
            segoeRegularPath.c_str(),
            baseFontSize,
            nullptr,
            glyphRanges);
    }
    if (!segoeSemiboldPath.empty()) {
        g::fontUiSemibold = io.Fonts->AddFontFromFileTTF(
            segoeSemiboldPath.c_str(),
            baseFontSize,
            nullptr,
            glyphRanges);
        g::fontUiTitle = io.Fonts->AddFontFromFileTTF(
            segoeSemiboldPath.c_str(),
            titleFontSize,
            nullptr,
            glyphRanges);
    }
    if (!segoeBoldPath.empty()) {
        g::fontEspName = io.Fonts->AddFontFromFileTTF(
            segoeBoldPath.c_str(),
            std::max(8.0f, g::espNameFontSize),
            nullptr,
            glyphRanges);
    }
    if (!segoeRegularPath.empty()) {
        ImFontConfig overlayTextConfig = {};
        overlayTextConfig.OversampleH = 2;
        overlayTextConfig.OversampleV = 2;
        g::fontOverlayText = io.Fonts->AddFontFromFileTTF(
            segoeRegularPath.c_str(),
            18.0f,
            &overlayTextConfig,
            glyphRanges);
    }

    if (io.Fonts->Fonts.empty()) {
        ImFontConfig fallbackCfg = {};
        fallbackCfg.SizePixels = baseFontSize;
        g::fontDefault = io.Fonts->AddFontDefault(&fallbackCfg);
    }
    if (!g::fontDefault) {
        g::fontDefault = io.Fonts->Fonts[0];
    }
    if (!g::fontUiSemibold)
        g::fontUiSemibold = g::fontDefault;
    if (!g::fontUiTitle)
        g::fontUiTitle = g::fontUiSemibold;
    if (!g::fontEspName)
        g::fontEspName = g::fontDefault;
    if (!g::fontOverlayText)
        g::fontOverlayText = g::fontDefault;
    io.FontDefault = g::fontDefault;

    if (!std::filesystem::exists(cjkRegularPath))
        cjkRegularPath.clear();
    if (!std::filesystem::exists(cjkSemiboldPath))
        cjkSemiboldPath = cjkRegularPath;

    auto mergeCjkGlyphs = [&](
        ImFont* destination,
        const std::string& fontPath,
        float fontSize) {
        if (!destination ||
            fontPath.empty() ||
            s_cjkGlyphRanges.empty())
            return;

        ImFontConfig cjkConfig = {};
        cjkConfig.MergeMode = true;
        cjkConfig.DstFont = destination;
        cjkConfig.OversampleH = 1;
        cjkConfig.OversampleV = 1;
        cjkConfig.PixelSnapH = true;
        io.Fonts->AddFontFromFileTTF(
            fontPath.c_str(),
            fontSize,
            &cjkConfig,
            s_cjkGlyphRanges.Data);
    };

    mergeCjkGlyphs(g::fontDefault, cjkRegularPath, baseFontSize);
    if (g::fontUiSemibold != g::fontDefault)
        mergeCjkGlyphs(g::fontUiSemibold, cjkSemiboldPath, baseFontSize);
    if (g::fontUiTitle != g::fontUiSemibold &&
        g::fontUiTitle != g::fontDefault) {
        mergeCjkGlyphs(g::fontUiTitle, cjkSemiboldPath, titleFontSize);
    }
    if (g::fontEspName != g::fontDefault &&
        g::fontEspName != g::fontUiSemibold &&
        g::fontEspName != g::fontUiTitle) {
        mergeCjkGlyphs(
            g::fontEspName,
            cjkSemiboldPath,
            std::max(8.0f, g::espNameFontSize));
    }
    if (g::fontOverlayText != g::fontDefault &&
        g::fontOverlayText != g::fontUiSemibold &&
        g::fontOverlayText != g::fontUiTitle &&
        g::fontOverlayText != g::fontEspName) {
        mergeCjkGlyphs(g::fontOverlayText, cjkRegularPath, 18.0f);
    }

    const HRSRC iconFontResource =
        FindResourceW(hInst, ui::icons::kFontResourceName, RT_RCDATA);
    if (iconFontResource) {
        const DWORD iconFontSize = SizeofResource(hInst, iconFontResource);
        const HGLOBAL loadedIconFont = LoadResource(hInst, iconFontResource);
        void* const iconFontData = loadedIconFont ? LockResource(loadedIconFont) : nullptr;
        if (iconFontData &&
            iconFontSize > 0 &&
            iconFontSize <= static_cast<DWORD>(std::numeric_limits<int>::max())) {
            ImFontConfig iconCfg = {};
            iconCfg.FontDataOwnedByAtlas = false;
            iconCfg.OversampleH = 2;
            iconCfg.OversampleV = 2;
            iconCfg.PixelSnapH = true;
            g::fontUiIcons = io.Fonts->AddFontFromMemoryTTF(
                iconFontData,
                static_cast<int>(iconFontSize),
                20.0f,
                &iconCfg,
                ui::icons::kGlyphRanges);
        }
    }

    ImFontConfig weaponCfg = {};
    weaponCfg.PixelSnapH = true;
    weaponCfg.OversampleH = 1;
    weaponCfg.OversampleV = 1;
    weaponCfg.FontDataOwnedByAtlas = false;
    static const ImWchar weaponRanges[] = { 0x20, 0x7E, 0 };
    g::fontWeaponIconsSmall = io.Fonts->AddFontFromMemoryTTF(
        resources::fonts::weapons,
        sizeof(resources::fonts::weapons),
        12.0f,
        &weaponCfg,
        weaponRanges);
    g::fontWeaponIcons = io.Fonts->AddFontFromMemoryTTF(
        resources::fonts::weapons,
        sizeof(resources::fonts::weapons),
        17.0f,
        &weaponCfg,
        weaponRanges);
    g::fontWeaponIconsLarge = io.Fonts->AddFontFromMemoryTTF(
        resources::fonts::weapons,
        sizeof(resources::fonts::weapons),
        24.0f,
        &weaponCfg,
        weaponRanges);

    ui::ApplyStyle();

    if (!ImGui_ImplWin32_Init(s_hwnd)) {
        if (outErrorMessage)
            *outErrorMessage = "Failed to initialize the ImGui Win32 backend.";
        return false;
    }
    s_imguiWin32Initialized = true;
    if (!ImGui_ImplDX11_Init(s_device.Get(), s_context.Get())) {
        if (outErrorMessage)
            *outErrorMessage = "Failed to initialize the ImGui DirectX 11 backend.";
        return false;
    }
    s_imguiDx11Initialized = true;

    ApplyOverlayWindowMode(true);
    SyncOverlayBounds();

    rollback.committed = true;
    return true;
