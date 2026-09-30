#include "TerrainSystem.h"
#include <DirectXTex.h>
#include <algorithm>
#include <cmath>

using Microsoft::WRL::ComPtr;
using namespace DirectX;

namespace
{

    bool FileExists(const char* p) { return p && ::GetFileAttributesA(p) != INVALID_FILE_ATTRIBUTES; }

    std::string ExeDir()
    {
        char buf[MAX_PATH]{};
        if (::GetModuleFileNameA(nullptr, buf, MAX_PATH) == 0) return {};
        std::string s(buf);
        const size_t slash = s.find_last_of("\\/");
        if (slash != std::string::npos) s.resize(slash + 1);
        return s;
    }

    std::string ResolvePath(const std::string& rel)
    {
        if (FileExists(rel.c_str())) return rel;
        const std::string exe = ExeDir();
        const std::string tries[] = {
            exe + rel, exe + "..\\" + rel, exe + "..\\..\\" + rel,
            std::string("..\\") + rel, std::string("..\\..\\") + rel };
        for (const auto& t : tries)
            if (FileExists(t.c_str())) return t;
        return rel;
    }

    float Hash2i(int x, int y)
    {
        unsigned n = (unsigned)x * 374761393u + (unsigned)y * 668265263u;
        n = (n ^ (n >> 13)) * 1274126177u;
        return float((n ^ (n >> 16)) & 0xFFFFFF) / float(0x1000000);
    }

    float ValueNoise(float x, float y)
    {
        const int xi = (int)floorf(x), yi = (int)floorf(y);
        const float xf = x - xi, yf = y - yi;
        const float u = xf * xf * (3.0f - 2.0f * xf);
        const float v = yf * yf * (3.0f - 2.0f * yf);
        const float a = Hash2i(xi, yi), b = Hash2i(xi + 1, yi);
        const float c = Hash2i(xi, yi + 1), d = Hash2i(xi + 1, yi + 1);
        return a + (b - a) * u + (c - a) * v + (a - b - c + d) * u * v;
    }

    float Fbm(float x, float y, int octaves)
    {
        float f = 0.0f, amp = 0.5f, sum = 0.0f;
        for (int i = 0; i < octaves; ++i)
        {
            f += amp * ValueNoise(x, y);
            sum += amp;
            x *= 2.0f; y *= 2.0f; amp *= 0.5f;
        }
        return f / sum;
    }
} 

void TerrainSystem::Initialize(
    ID3D12Device* device,
    ID3D12GraphicsCommandList* cmdList,
    ID3D12DescriptorHeap* srvHeap,
    UINT cbvSrvDescriptorSize,
    UINT srvBaseIndex)
{
    mDevice = device;
    mDiffuseSrvIndex = srvBaseIndex;
    mHeightSrvIndex = srvBaseIndex + 1;

    LoadHeightData();
    CreateHeightTexture(cmdList, srvHeap, cbvSrvDescriptorSize);
    CreateDiffuseTexture(cmdList, srvHeap, cbvSrvDescriptorSize);

    BuildQuadtree();
    BuildGrid(kHiGridQuads, mGridHi, cmdList);
    BuildGrid(kLoGridQuads, mGridLo, cmdList);

    mSelection.reserve(kMaxTiles);
    for (int i = 0; i < gNumFrameResources; ++i)
        mTileCB[i] = std::make_unique<UploadBuffer<ObjectConstants>>(device, kMaxTiles, true);

    char msg[192];
    sprintf_s(msg, "TerrainSystem: %zu nodes, heightmap %dx%d\n",
        mNodes.size(), kHeightMapSize, kHeightMapSize);
    OutputDebugStringA(msg);
}

bool TerrainSystem::LoadHeightData()
{
    const int kMap = kHeightMapSize;
    mHeightPixels.assign((size_t)kMap * kMap, 0);

    static const char* kPaths[] = {
        "../Textures/terrain_height.png",
        "../Textures/terrain_height.dds",
        "../Textures/heightmap.png",
        "../Textures/heightmap.dds" };

    for (const char* rel : kPaths)
    {
        const std::string path = ResolvePath(rel);
        std::wstring wpath(path.begin(), path.end());
        DirectX::ScratchImage image;
        DirectX::TexMetadata meta;
        HRESULT hr = DirectX::LoadFromWICFile(wpath.c_str(), DirectX::WIC_FLAGS_NONE, &meta, image);
        if (FAILED(hr))
            hr = DirectX::LoadFromDDSFile(wpath.c_str(), DirectX::DDS_FLAGS_NONE, &meta, image);
        if (FAILED(hr) || image.GetImageCount() == 0)
            continue;

        const DirectX::Image* img = image.GetImage(0, 0, 0);
        for (int y = 0; y < kMap; ++y)
        {
            const size_t sy = ((size_t)y * img->height) / kMap;
            const uint8_t* srcRow = img->pixels + sy * img->rowPitch;
            for (int x = 0; x < kMap; ++x)
            {
                const size_t sx = ((size_t)x * img->width) / kMap;
                uint8_t v = 0;
                switch (img->format)
                {
                case DXGI_FORMAT_R8_UNORM:            v = srcRow[sx]; break;
                case DXGI_FORMAT_R16_UNORM:           v = (uint8_t)(((const uint16_t*)srcRow)[sx] >> 8); break;
                case DXGI_FORMAT_R8G8B8A8_UNORM:      v = ((const uint8_t*)srcRow)[sx * 4 + 0]; break;
                case DXGI_FORMAT_B8G8R8A8_UNORM:      v = ((const uint8_t*)srcRow)[sx * 4 + 1]; break;
                case DXGI_FORMAT_R32_FLOAT:           v = (uint8_t)(std::clamp(((const float*)srcRow)[sx], 0.0f, 1.0f) * 255.0f); break;
                default:                              v = 128; break;
                }
                mHeightPixels[(size_t)y * kMap + x] = v;
            }
        }
        char msg[160];
        sprintf_s(msg, "Terrain: heightmap loaded: %s (%zux%zu)\n", path.c_str(), img->width, img->height);
        OutputDebugStringA(msg);
        return true;
    }

    for (int y = 0; y < kMap; ++y)
    {
        for (int x = 0; x < kMap; ++x)
        {
            const float nx = x / (float)kMap, ny = y / (float)kMap;
            float v = Fbm(nx * 6.0f, ny * 6.0f, 6);
            const float dx = nx - 0.5f, dy = ny - 0.5f;
            const float d = sqrtf(dx * dx + dy * dy) * 2.0f;
            v *= (std::max)(0.0f, 1.0f - d * d); // к краям плавно к нулю
            mHeightPixels[(size_t)y * kMap + x] =
                (uint8_t)(std::clamp(v, 0.0f, 1.0f) * 255.0f);
        }
    }
    OutputDebugStringA("Terrain: heightmap file not found, using procedural fallback.\n");
    return false;
}

void TerrainSystem::CreateHeightTexture(ID3D12GraphicsCommandList* cmdList, ID3D12DescriptorHeap* srvHeap, UINT descSize)
{
    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width = kHeightMapSize;
    texDesc.Height = kHeightMapSize;
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels = 1;
    texDesc.Format = DXGI_FORMAT_R8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

    const CD3DX12_HEAP_PROPERTIES defProps(D3D12_HEAP_TYPE_DEFAULT);
    ThrowIfFailed(mDevice->CreateCommittedResource(
        &defProps, D3D12_HEAP_FLAG_NONE, &texDesc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&mHeightTex)));

    const UINT64 uploadSize = GetRequiredIntermediateSize(mHeightTex.Get(), 0, 1);
    const CD3DX12_HEAP_PROPERTIES upProps(D3D12_HEAP_TYPE_UPLOAD);
    const CD3DX12_RESOURCE_DESC upDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadSize);
    ThrowIfFailed(mDevice->CreateCommittedResource(
        &upProps, D3D12_HEAP_FLAG_NONE, &upDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&mHeightUpload)));

    D3D12_SUBRESOURCE_DATA sub = {};
    sub.pData = mHeightPixels.data();
    sub.RowPitch = kHeightMapSize;
    sub.SlicePitch = sub.RowPitch * kHeightMapSize;
    UpdateSubresources(cmdList, mHeightTex.Get(), mHeightUpload.Get(), 0, 0, 1, &sub);

    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        mHeightTex.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    cmdList->ResourceBarrier(1, &barrier);

    CD3DX12_CPU_DESCRIPTOR_HANDLE h(srvHeap->GetCPUDescriptorHandleForHeapStart());
    h.Offset(mHeightSrvIndex, descSize);
    D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Format = DXGI_FORMAT_R8_UNORM;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Texture2D.MipLevels = 1;
    mDevice->CreateShaderResourceView(mHeightTex.Get(), &srv, h);
}

void TerrainSystem::CreateDiffuseTexture(ID3D12GraphicsCommandList* cmdList, ID3D12DescriptorHeap* srvHeap, UINT descSize)
{
    static const char* kPaths[] = {
        "../Textures/terrain_diffuse.png",
        "../Textures/terrain_diffuse.dds",
        "../Textures/terrain_albedo.png" };

    for (const char* rel : kPaths)
    {
        const std::string path = ResolvePath(rel);
        std::wstring wpath(path.begin(), path.end());
        DirectX::ScratchImage image;
        DirectX::TexMetadata meta;
        HRESULT hr = DirectX::LoadFromWICFile(wpath.c_str(), DirectX::WIC_FLAGS_NONE, &meta, image);
        if (FAILED(hr))
            hr = DirectX::LoadFromDDSFile(wpath.c_str(), DirectX::DDS_FLAGS_NONE, &meta, image);
        if (FAILED(hr) || image.GetImageCount() == 0)
            continue;

        const DirectX::Image* img = image.GetImage(0, 0, 0);
        D3D12_RESOURCE_DESC texDesc = {};
        texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        texDesc.Width = img->width;
        texDesc.Height = (UINT)img->height;
        texDesc.DepthOrArraySize = 1;
        texDesc.MipLevels = 1;
        texDesc.Format = img->format;
        texDesc.SampleDesc.Count = 1;
        texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

        const CD3DX12_HEAP_PROPERTIES defProps(D3D12_HEAP_TYPE_DEFAULT);
        ThrowIfFailed(mDevice->CreateCommittedResource(
            &defProps, D3D12_HEAP_FLAG_NONE, &texDesc,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&mDiffuseTex)));

        const UINT64 uploadSize = GetRequiredIntermediateSize(mDiffuseTex.Get(), 0, 1);
        const CD3DX12_HEAP_PROPERTIES upProps(D3D12_HEAP_TYPE_UPLOAD);
        const CD3DX12_RESOURCE_DESC upDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadSize);
        ThrowIfFailed(mDevice->CreateCommittedResource(
            &upProps, D3D12_HEAP_FLAG_NONE, &upDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&mDiffuseUpload)));

        D3D12_SUBRESOURCE_DATA sub = {};
        sub.pData = img->pixels;
        sub.RowPitch = img->rowPitch;
        sub.SlicePitch = img->slicePitch;
        UpdateSubresources(cmdList, mDiffuseTex.Get(), mDiffuseUpload.Get(), 0, 0, 1, &sub);

        auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            mDiffuseTex.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        cmdList->ResourceBarrier(1, &barrier);

        CD3DX12_CPU_DESCRIPTOR_HANDLE h(srvHeap->GetCPUDescriptorHandleForHeapStart());
        h.Offset(mDiffuseSrvIndex, descSize);
        D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Format = img->format;
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Texture2D.MipLevels = 1;
        mDevice->CreateShaderResourceView(mDiffuseTex.Get(), &srv, h);

        char msg[160];
        sprintf_s(msg, "Terrain: diffuse loaded: %s\n", path.c_str());
        OutputDebugStringA(msg);
        return;
    }

    CD3DX12_CPU_DESCRIPTOR_HANDLE h(srvHeap->GetCPUDescriptorHandleForHeapStart());
    h.Offset(mDiffuseSrvIndex, descSize);
    D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Format = DXGI_FORMAT_R8_UNORM;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Texture2D.MipLevels = 1;
    mDevice->CreateShaderResourceView(mHeightTex.Get(), &srv, h);
    OutputDebugStringA("Terrain: diffuse not found, using heightmap as albedo.\n");
}

void TerrainSystem::ComputeNodeHeightRange(const XMFLOAT2& center, float halfSize, float& outMin, float& outMax) const
{
    const int kMap = kHeightMapSize;
    auto toPx = [&](float w) { return (w / kWorldSize + 0.5f) * (kMap - 1); };
    int px0 = (std::max)(0, (int)toPx(center.x - halfSize));
    int px1 = (std::min)(kMap - 1, (int)toPx(center.x + halfSize));
    int py0 = (std::max)(0, (int)toPx(center.y - halfSize));
    int py1 = (std::min)(kMap - 1, (int)toPx(center.y + halfSize));
    const int step = (std::max)(1, (px1 - px0) / 16);

    outMin = 1.0f; outMax = 0.0f;
    for (int y = py0; y <= py1; y += step)
    {
        for (int x = px0; x <= px1; x += step)
        {
            const float h = mHeightPixels[(size_t)y * kMap + x] / 255.0f;
            outMin = (std::min)(outMin, h);
            outMax = (std::max)(outMax, h);
        }
    }
}

void TerrainSystem::BuildQuadtree()
{
    mNodes.clear();
    mNodes.reserve(1600);

    TerrainNode root;
    root.Center = { 0.0f, 0.0f };
    root.HalfSize = kWorldSize * 0.5f;
    root.Level = 0;
    mNodes.push_back(root);
    BuildNode(0, 0);
}


}