#include "Features/ESP/Render/weapon_icon_atlas.h"

#include "Features/ESP/Render/weapon_icon_atlas_data.generated.h"
#include "Features/ESP/weapon_catalog.h"

#include <Windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <wincodec.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")

namespace
{
    using Microsoft::WRL::ComPtr;

    constexpr wchar_t kAtlasResourceName[] = L"WEAPON_ICON_ATLAS_PNG";
    ComPtr<ID3D11ShaderResourceView> s_atlasView;

    bool DecodeAtlasPng(const void* data, DWORD size, std::vector<BYTE>& pixels) noexcept
    {
        pixels.clear();
        if (!data || size < 8 || size > 4 * 1024 * 1024)
            return false;
        struct Apartment {
            HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            ~Apartment() { if (SUCCEEDED(result)) CoUninitialize(); }
        } apartment;
        if (FAILED(apartment.result) && apartment.result != RPC_E_CHANGED_MODE)
            return false;
        try {
            ComPtr<IWICImagingFactory> factory;
            ComPtr<IWICStream> stream;
            ComPtr<IWICBitmapDecoder> decoder;
            ComPtr<IWICBitmapFrameDecode> frame;
            ComPtr<IWICFormatConverter> converter;
            if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                    IID_PPV_ARGS(&factory))) ||
                FAILED(factory->CreateStream(&stream)) ||
                FAILED(stream->InitializeFromMemory(static_cast<BYTE*>(const_cast<void*>(data)), size)) ||
                FAILED(factory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, &decoder)))
                return false;
            GUID format{};
            UINT frames = 0, width = 0, height = 0;
            if (FAILED(decoder->GetContainerFormat(&format)) || format != GUID_ContainerFormatPng ||
                FAILED(decoder->GetFrameCount(&frames)) || frames != 1 ||
                FAILED(decoder->GetFrame(0, &frame)) || FAILED(frame->GetSize(&width, &height)) ||
                width != resources::weapon_icons::kAtlasWidth || height != resources::weapon_icons::kAtlasHeight ||
                FAILED(factory->CreateFormatConverter(&converter)) ||
                FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone,
                    nullptr, 0, WICBitmapPaletteTypeCustom)))
                return false;
            pixels.resize(static_cast<size_t>(width) * height * 4);
            if (FAILED(converter->CopyPixels(nullptr, width * 4, static_cast<UINT>(pixels.size()), pixels.data()))) {
                pixels.clear();
                return false;
            }
            return true;
        } catch (...) {
            pixels.clear();
            return false;
        }
    }

    ImTextureRef AtlasTextureRef() noexcept
    {
        const auto textureId = static_cast<ImTextureID>(
            reinterpret_cast<std::uintptr_t>(s_atlasView.Get()));
        return ImTextureRef(textureId);
    }
}

bool esp::render::weapon_icons::Initialize(ID3D11Device* device) noexcept
{
    if (s_atlasView)
        return true;
    if (!device)
        return false;

    const HMODULE module = GetModuleHandleW(nullptr);
    const HRSRC resource = module
        ? FindResourceW(module, kAtlasResourceName, RT_RCDATA)
        : nullptr;
    if (!resource)
        return false;

    const DWORD resourceSize = SizeofResource(module, resource);
    const HGLOBAL loaded = LoadResource(module, resource);
    const void* encoded = loaded ? LockResource(loaded) : nullptr;
    std::vector<BYTE> pixels;
    if (!DecodeAtlasPng(encoded, resourceSize, pixels))
        return false;

    D3D11_TEXTURE2D_DESC textureDesc = {};
    textureDesc.Width = resources::weapon_icons::kAtlasWidth;
    textureDesc.Height = resources::weapon_icons::kAtlasHeight;
    textureDesc.MipLevels = 1;
    textureDesc.ArraySize = 1;
    textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    textureDesc.SampleDesc.Count = 1;
    textureDesc.Usage = D3D11_USAGE_IMMUTABLE;
    textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA initialData = {};
    initialData.pSysMem = pixels.data();
    initialData.SysMemPitch =
        static_cast<UINT>(resources::weapon_icons::kAtlasWidth) * 4u;

    ComPtr<ID3D11Texture2D> texture;
    if (FAILED(device->CreateTexture2D(&textureDesc, &initialData, &texture)) ||
        !texture) {
        return false;
    }

    return SUCCEEDED(device->CreateShaderResourceView(
        texture.Get(),
        nullptr,
        &s_atlasView)) &&
        s_atlasView;
}

void esp::render::weapon_icons::Shutdown() noexcept
{
    s_atlasView.Reset();
}

bool esp::render::weapon_icons::IsReady() noexcept
{
    return static_cast<bool>(s_atlasView);
}

bool esp::render::weapon_icons::CalculateDrawSize(
    uint16_t itemId,
    float height,
    ImVec2* outSize) noexcept
{
    if (!outSize || !s_atlasView || height <= 0.0f)
        return false;

    esp::weapons::WeaponIconRegion region = {};
    if (!esp::weapons::WeaponIconRegionFromItemId(itemId, &region) ||
        region.width == 0 ||
        region.height == 0) {
        return false;
    }

    outSize->x = height *
        static_cast<float>(region.width) /
        static_cast<float>(region.height);
    outSize->y = height;
    return true;
}

bool esp::render::weapon_icons::Draw(
    ImDrawList* drawList,
    uint16_t itemId,
    const ImVec2& topLeft,
    float height,
    ImU32 color,
    bool shadow) noexcept
{
    if (!drawList || !s_atlasView)
        return false;

    esp::weapons::WeaponIconRegion region = {};
    if (!esp::weapons::WeaponIconRegionFromItemId(itemId, &region) ||
        region.width == 0 ||
        region.height == 0) {
        return false;
    }

    ImVec2 size = {};
    if (!CalculateDrawSize(itemId, height, &size))
        return false;

    const float inverseWidth =
        1.0f / static_cast<float>(resources::weapon_icons::kAtlasWidth);
    const float inverseHeight =
        1.0f / static_cast<float>(resources::weapon_icons::kAtlasHeight);
    const ImVec2 uvMin(
        static_cast<float>(region.x) * inverseWidth,
        static_cast<float>(region.y) * inverseHeight);
    const ImVec2 uvMax(
        static_cast<float>(region.x + region.width) * inverseWidth,
        static_cast<float>(region.y + region.height) * inverseHeight);
    const ImVec2 bottomRight(topLeft.x + size.x, topLeft.y + size.y);
    const ImTextureRef texture = AtlasTextureRef();

    if (shadow) {
        const ImU32 alpha = (color >> IM_COL32_A_SHIFT) & 0xFFu;
        const ImU32 shadowColor = IM_COL32(
            0,
            0,
            0,
            static_cast<int>(std::min<ImU32>(alpha, 210u)));
        drawList->AddImage(
            texture,
            ImVec2(topLeft.x + 1.0f, topLeft.y + 1.0f),
            ImVec2(bottomRight.x + 1.0f, bottomRight.y + 1.0f),
            uvMin,
            uvMax,
            shadowColor);
    }

    drawList->AddImage(texture, topLeft, bottomRight, uvMin, uvMax, color);
    return true;
}
