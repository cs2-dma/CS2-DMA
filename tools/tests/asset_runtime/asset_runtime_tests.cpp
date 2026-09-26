#include <array>
#include <iostream>
#include <string>
#include "Features/WebRadar/embedded_assets.h"
#include "../../../src/Features/ESP/Render/weapon_icon_atlas.cpp"
#include <bcrypt.h>

std::string HashPixels(const std::vector<BYTE>& pixels)
{
    std::array<BYTE, 32> digest{};
    if (BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0, const_cast<BYTE*>(pixels.data()),
            static_cast<ULONG>(pixels.size()), digest.data(), static_cast<ULONG>(digest.size())) < 0)
        return {};
    const char* hex = "0123456789abcdef";
    std::string result;
    for (const auto byte : digest) { result += hex[byte >> 4]; result += hex[byte & 15]; }
    return result;
}

int main()
{
    namespace icons = esp::render::weapon_icons;
    int failures = 0;
    const auto check = [&](bool condition, const char* name) {
        if (!condition) { std::cerr << name << '\n'; ++failures; }
    };
    const auto resource = FindResourceW(nullptr, L"WEAPON_ICON_ATLAS_PNG", RT_RCDATA);
    const auto size = SizeofResource(nullptr, resource);
    const auto* data = LockResource(LoadResource(nullptr, resource));
    check(data && size > 0, "Embedded PNG");
    webradar::EmbeddedAsset embedded{};
    check(webradar::FindEmbeddedAsset("/test/asset.png", &embedded) && embedded.size == size, "Quoted asset lookup");
    check(webradar::FindEmbeddedAsset("/test/legacy.png", &embedded) && embedded.size == size, "Legacy asset lookup");
    check(!webradar::FindEmbeddedAsset("/missing", &embedded), "Missing asset lookup");
    std::vector<BYTE> pixels;
    constexpr const char* expected = "c19d484a91210baef34f810dd054119f52affaa7430ca3fb2d22c6ce2161c423";
    check(DecodeAtlasPng(data, size, pixels), "WIC decode without existing COM apartment");
    check(HashPixels(pixels) == expected, "Decoded RGBA hash matches original raw atlas");
    check(!DecodeAtlasPng(nullptr, size, pixels) && pixels.empty(), "Null input rejected");
    check(!DecodeAtlasPng(data, 8, pixels) && pixels.empty(), "Truncated input rejected");
    check(!DecodeAtlasPng(data, 5 * 1024 * 1024, pixels) && pixels.empty(), "Oversized input rejected");
    const HRESULT apartment = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    check(SUCCEEDED(apartment), "STA initialization");
    check(DecodeAtlasPng(data, size, pixels) && HashPixels(pixels) == expected, "WIC decode in existing STA");
    check(!icons::Initialize(nullptr) && !icons::IsReady(), "Null device rejected");
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    check(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
        D3D11_SDK_VERSION, &device, nullptr, &context)), "WARP device creation");
    if (device) {
        check(icons::Initialize(device.Get()) && icons::IsReady(), "Texture initialization");
        check(icons::Initialize(device.Get()), "Repeated initialization");
        if (s_atlasView) {
            ComPtr<ID3D11Resource> resourceTexture;
            s_atlasView->GetResource(&resourceTexture);
            ComPtr<ID3D11Texture2D> texture;
            check(SUCCEEDED(resourceTexture.As(&texture)), "Texture interface");
            if (texture) {
                D3D11_TEXTURE2D_DESC description{};
                texture->GetDesc(&description);
                description.Usage = D3D11_USAGE_STAGING;
                description.BindFlags = 0;
                description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
                ComPtr<ID3D11Texture2D> staging;
                check(SUCCEEDED(device->CreateTexture2D(&description, nullptr, &staging)), "Staging texture");
                if (staging) {
                    context->CopyResource(staging.Get(), texture.Get());
                    D3D11_MAPPED_SUBRESOURCE mapped{};
                    const HRESULT result = context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
                    check(SUCCEEDED(result), "GPU texture readback");
                    if (SUCCEEDED(result)) {
                        std::vector<BYTE> actual(pixels.size());
                        const size_t rowBytes = static_cast<size_t>(description.Width) * 4;
                        for (UINT y = 0; y < description.Height; ++y)
                            memcpy(actual.data() + y * rowBytes, static_cast<const BYTE*>(mapped.pData) +
                                static_cast<size_t>(y) * mapped.RowPitch, rowBytes);
                        context->Unmap(staging.Get(), 0);
                        check(HashPixels(actual) == expected, "GPU RGBA unchanged");
                    }
                }
            }
        }
        ImVec2 drawSize{};
        check(icons::CalculateDrawSize(7, 24, &drawSize) && drawSize.x > 0 && drawSize.y == 24, "Icon regions");
        icons::Shutdown();
        check(!icons::IsReady(), "Shutdown");
        check(icons::Initialize(device.Get()), "Reinitialize");
        icons::Shutdown();
    }
    if (SUCCEEDED(apartment)) CoUninitialize();
    std::cout << "Asset runtime: " << failures << " failures\n";
    return failures ? 1 : 0;
}
