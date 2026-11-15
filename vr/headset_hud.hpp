#pragma once
#include "game_math.hpp"
#include <d3dcompiler.h>
#include <cwchar>

namespace dtvr {
inline constexpr unsigned hudWidth = 512, hudHeight = 136;
inline constexpr float hudY = -.32f, hudZ = -1.5f;

inline Matrix headsetHudProjection(const XrView& eye, const XrPosef& head) {
    // Use the runtime's physical eyes, independent of the game's eye-distance
    // effect, so the notification stays at a comfortable, fixed reading depth.
    return multiply(projection(eye.fov, .05f, 10.f), viewMatrix(eyeInHeadSpace(eye.pose, head)));
}

inline std::vector<uint32_t> eyeDistanceHudImage(float distance) {
    struct Bitmap {
        HDC dc = CreateCompatibleDC(nullptr);
        HBITMAP image = nullptr;
        HGDIOBJ previous = nullptr;
        ~Bitmap() {
            if (previous)
                SelectObject(dc, previous);
            if (image)
                DeleteObject(image);
            if (dc)
                DeleteDC(dc);
        }
    } bitmap;
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = hudWidth;
    info.bmiHeader.biHeight = -(int)hudHeight;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    void* pixels = nullptr;
    bitmap.image = CreateDIBSection(bitmap.dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    if (!bitmap.dc || !bitmap.image)
        throw std::runtime_error("Cannot create headset notification bitmap");
    bitmap.previous = SelectObject(bitmap.dc, bitmap.image);
    std::memset(pixels, 0, hudWidth * hudHeight * 4);
    SetBkMode(bitmap.dc, TRANSPARENT);
    SetTextColor(bitmap.dc, RGB(255, 255, 255));
    auto text = [&](const wchar_t* caption, int size, int weight, int y, int height) {
        auto font = CreateFontW(-size, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0,
                                ANTIALIASED_QUALITY, 0, L"Segoe UI");
        if (!font)
            throw std::runtime_error("Cannot create headset notification font");
        auto previous = SelectObject(bitmap.dc, font);
        RECT bounds{12, y, (LONG)hudWidth - 12, y + height};
        DrawTextW(bitmap.dc, caption, -1, &bounds, DT_CENTER | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(bitmap.dc, previous);
        DeleteObject(font);
    };
    const int hundredths = (int)std::lround(distance * 100);
    wchar_t value[32];
    std::swprintf(value, 32, L"%d.%02d\u00d7", hundredths / 100, hundredths % 100);
    text(L"Eye distance", 21, FW_NORMAL, 12, 28);
    text(value, 46, FW_BOLD, 37, 58);
    text(L"Shift+F9  decrease   /   Shift+F10  increase", 17, FW_NORMAL, 105, 24);
    // GDI batches text operations; finish before reading the DIB on the CPU.
    GdiFlush();
    auto* mask = static_cast<const uint32_t*>(pixels);
    std::vector<uint32_t> result(hudWidth * hudHeight);
    for (unsigned y = 0; y < hudHeight; ++y)
        for (unsigned x = 0; x < hudWidth; ++x) {
            const unsigned index = y * hudWidth + x;
            const float glyph = (mask[index] & 255) / 255.f;
            const float dx =
                std::max(std::abs(x + .5f - hudWidth / 2.f) - (hudWidth / 2.f - 18), 0.f);
            const float dy =
                std::max(std::abs(y + .5f - hudHeight / 2.f) - (hudHeight / 2.f - 18), 0.f);
            const float coverage = std::clamp(18.5f - std::sqrt(dx * dx + dy * dy), 0.f, 1.f);
            const bool number = y >= 37 && y < 95;
            const unsigned r = (unsigned)(24 + glyph * ((number ? 239 : 204) - 24));
            const unsigned g = (unsigned)(30 + glyph * ((number ? 179 : 213) - 30));
            const unsigned b = (unsigned)(32 + glyph * ((number ? 84 : 216) - 32));
            result[index] = r | (g << 8) | (b << 16) | ((unsigned)(coverage * 235) << 24);
        }
    return result;
}

class HeadsetHud {
    ComPtr<ID3D12Resource> texture, upload;
    ComPtr<ID3D12PipelineState> pipeline;
    void* mapped = nullptr;
    bool dirty = false, uploaded = false;
    ULONGLONG visibleUntil = 0;

  public:
    void setup(ID3D12Device* device, DXGI_FORMAT format, ID3D12RootSignature* root) {
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = hudWidth;
        desc.Height = hudHeight;
        desc.DepthOrArraySize = desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        desc.SampleDesc.Count = 1;
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        check_hr(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                                 D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                 IID_PPV_ARGS(&texture)),
                 "Create headset notification texture");
        desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = hudWidth * hudHeight * 4;
        desc.Height = desc.DepthOrArraySize = desc.MipLevels = 1;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        heap.Type = D3D12_HEAP_TYPE_UPLOAD;
        check_hr(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                                 D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                 IID_PPV_ARGS(&upload)),
                 "Create headset notification upload");
        D3D12_RANGE noRead{0, 0};
        check_hr(upload->Map(0, &noRead, &mapped), "Map headset notification upload");
        const char shader[] = R"(
Texture2D image:register(t0); SamplerState imageSampler:register(s0);
cbuffer Hud:register(b0) {column_major float4x4 viewProjection; float4 fade;};
struct Vertex {float4 p:SV_Position;float2 uv:TEXCOORD;};
Vertex vs(uint id:SV_VertexID) {
    const float2 points[6]={float2(0,0),float2(1,0),float2(0,1),float2(0,1),float2(1,0),float2(1,1)};
    Vertex v;v.uv=points[id];
    v.p=mul(viewProjection,float4((v.uv.x-.5)*.64,-.32+(.5-v.uv.y)*.17,-1.5,1));return v;
}
float4 ps(Vertex v):SV_Target {
    float4 colour=image.SampleLevel(imageSampler,v.uv,0);
    colour.a*=fade.x;return float4(colour.rgb*colour.a,colour.a);
})";
        ComPtr<ID3DBlob> vs, ps, error;
        check_hr(D3DCompile(shader, sizeof(shader) - 1, nullptr, nullptr, nullptr, "vs", "vs_5_0",
                            0, 0, &vs, &error),
                 "Compile headset notification vertex shader");
        check_hr(D3DCompile(shader, sizeof(shader) - 1, nullptr, nullptr, nullptr, "ps", "ps_5_0",
                            0, 0, &ps, &error),
                 "Compile headset notification pixel shader");
        D3D12_GRAPHICS_PIPELINE_STATE_DESC state{};
        state.pRootSignature = root;
        state.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
        state.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
        auto& blend = state.BlendState.RenderTarget[0];
        blend.BlendEnable = TRUE;
        blend.SrcBlend = blend.SrcBlendAlpha = D3D12_BLEND_ONE;
        blend.DestBlend = blend.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
        blend.BlendOp = blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        state.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        state.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        state.RasterizerState.DepthClipEnable = TRUE;
        state.SampleMask = UINT_MAX;
        state.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        state.NumRenderTargets = 1;
        state.RTVFormats[0] = format;
        state.SampleDesc.Count = 1;
        check_hr(device->CreateGraphicsPipelineState(&state, IID_PPV_ARGS(&pipeline)),
                 "Create headset notification pipeline");
    }
    ID3D12Resource* image() const {
        return texture.Get();
    }
    void showEyeDistance(float distance) {
        const auto pixels = eyeDistanceHudImage(distance);
        std::memcpy(mapped, pixels.data(), pixels.size() * sizeof(uint32_t));
        dirty = true;
        visibleUntil = GetTickCount64() + 4000;
    }
    void uploadIfChanged(ID3D12GraphicsCommandList* commands) {
        if (!dirty)
            return;
        if (uploaded) {
            auto barrier = transition(texture.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                                      D3D12_RESOURCE_STATE_COPY_DEST);
            commands->ResourceBarrier(1, &barrier);
        }
        D3D12_TEXTURE_COPY_LOCATION source{}, destination{};
        source.pResource = upload.Get();
        source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        source.PlacedFootprint.Footprint = {DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, hudWidth, hudHeight, 1,
                                            hudWidth * 4};
        destination.pResource = texture.Get();
        destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        commands->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
        auto barrier = transition(texture.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                                  D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        commands->ResourceBarrier(1, &barrier);
        uploaded = true;
        dirty = false;
    }
    void draw(ID3D12GraphicsCommandList* commands, D3D12_GPU_DESCRIPTOR_HANDLE image,
              const XrView& eye, const XrPosef& head) {
        const auto now = GetTickCount64();
        if (!uploaded || now >= visibleUntil)
            return;
        struct Constants {
            Matrix viewProjection;
            std::array<float, 4> fade;
        } constants{headsetHudProjection(eye, head),
                    {std::min(1.f, (visibleUntil - now) / 400.f), 0, 0, 0}};
        static_assert(sizeof(constants) == 80);
        commands->SetPipelineState(pipeline.Get());
        commands->SetGraphicsRootDescriptorTable(0, image);
        commands->SetGraphicsRoot32BitConstants(1, 20, &constants, 0);
        commands->DrawInstanced(6, 1, 0, 0);
    }
    ~HeadsetHud() {
        if (mapped)
            upload->Unmap(0, nullptr);
    }
};
} // namespace dtvr
