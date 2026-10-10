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
    GenerateNoiseHeightMap();
    mBlendMask.assign((size_t)kHeightMapSize * kHeightMapSize, 0);
    mHeightPixels = mHeightPixelsA; 
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
    mHeightPixelsA.assign((size_t)kMap * kMap, 0);
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
                mHeightPixelsA[(size_t)y * kMap + x] = v;
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
            v *= (std::max)(0.0f, 1.0f - d * d);
            mHeightPixelsA[(size_t)y * kMap + x] =
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

void TerrainSystem::BuildNode(int nodeIndex, int level)
{
    TerrainNode& n = mNodes[nodeIndex];

    float minH, maxH;
    ComputeNodeHeightRange(n.Center, n.HalfSize, minH, maxH);
    const float skirtPad = kHeightScale * 0.12f + 1.0f;
    BoundingBox::CreateFromPoints(n.Bounds,
        XMVectorSet(n.Center.x - n.HalfSize, kBaseY + minH * kHeightScale - skirtPad, n.Center.y - n.HalfSize, 0.0f),
        XMVectorSet(n.Center.x + n.HalfSize, kBaseY + maxH * kHeightScale + 1.0f, n.Center.y + n.HalfSize, 0.0f));

    if (level >= kMaxDepth)
        return;

    n.HasChildren = true;
    const float childHalf = n.HalfSize * 0.5f;
    const DirectX::XMFLOAT2 offsets[4] = {
        { -childHalf, -childHalf }, { childHalf, -childHalf },
        { -childHalf,  childHalf }, { childHalf,  childHalf } };
    for (int i = 0; i < 4; ++i)
    {
        TerrainNode child;
        child.Center = { n.Center.x + offsets[i].x, n.Center.y + offsets[i].y };
        child.HalfSize = childHalf;
        child.Level = level + 1;
        n.Children[i] = (int)mNodes.size();
        mNodes.push_back(child);
        BuildNode(n.Children[i], level + 1);
    }
}

void TerrainSystem::BuildGrid(int quads, MeshGeometry& geo, ID3D12GraphicsCommandList* cmdList)
{
    const int R = quads;
    const int S = R + 1;

    std::vector<Vertex> verts;
    verts.reserve((size_t)S * S + 4 * S);
    auto vid = [&](int ix, int iz) { return (uint32_t)(iz * S + ix); };

    for (int iz = 0; iz < S; ++iz)
    {
        for (int ix = 0; ix < S; ++ix)
        {
            Vertex v = {};
            v.Pos = { -1.0f + 2.0f * ix / R, 0.0f, -1.0f + 2.0f * iz / R };
            v.Normal = { 0.0f, 1.0f, 0.0f };
            v.TexC = { ix / (float)R, iz / (float)R };
            verts.push_back(v);
        }
    }
    /*
    const uint32_t skirtBase = (uint32_t)(S * S);
    auto pushSkirt = [&](uint32_t src) {
        Vertex v = verts[src];
        v.Normal = { 0.0f, -1.0f, 0.0f };
        verts.push_back(v);
        };
    for (int ix = 0; ix < S; ++ix) pushSkirt(vid(ix, 0));
    for (int ix = 0; ix < S; ++ix) pushSkirt(vid(ix, R));
    for (int iz = 0; iz < S; ++iz) pushSkirt(vid(0, iz));
    for (int iz = 0; iz < S; ++iz) pushSkirt(vid(R, iz));
    */
    std::vector<uint32_t> idx;
    idx.reserve((size_t)R * R * 6 + (size_t)4 * R * 12);

    for (int iz = 0; iz < R; ++iz)
    {
        for (int ix = 0; ix < R; ++ix)
        {
            const uint32_t k = vid(ix, iz);
            idx.insert(idx.end(), { k, k + (uint32_t)S, k + 1 });
            idx.insert(idx.end(), { k + 1, k + (uint32_t)S, k + (uint32_t)S + 1 });
        }
    }
    /*
    auto skirtQuad = [&](uint32_t t0, uint32_t t1, uint32_t b0, uint32_t b1) {
        idx.insert(idx.end(), { t0, b0, t1, t1, b0, b1 });
        idx.insert(idx.end(), { t0, t1, b0, t1, b1, b0 });
        };
    for (int i = 0; i < R; ++i)
    {
        const uint32_t s0 = skirtBase + (uint32_t)i, s1 = s0 + 1;
        skirtQuad(vid(i, 0), vid(i + 1, 0), s0, s1);                       // iz=0
        skirtQuad(vid(i, R), vid(i + 1, R), skirtBase + S + (uint32_t)i, skirtBase + S + (uint32_t)i + 1);
        skirtQuad(vid(0, i), vid(0, i + 1), skirtBase + 2 * S + (uint32_t)i, skirtBase + 2 * S + (uint32_t)i + 1);
        skirtQuad(vid(R, i), vid(R, i + 1), skirtBase + 3 * S + (uint32_t)i, skirtBase + 3 * S + (uint32_t)i + 1);
    }*/

    geo.Name = (quads == kHiGridQuads) ? "TerrainGridHi" : "TerrainGridLo";
    const UINT vbByteSize = (UINT)(verts.size() * sizeof(Vertex));
    const UINT ibByteSize = (UINT)(idx.size() * sizeof(uint32_t));

    ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo.VertexBufferCPU));
    CopyMemory(geo.VertexBufferCPU->GetBufferPointer(), verts.data(), vbByteSize);
    ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo.IndexBufferCPU));
    CopyMemory(geo.IndexBufferCPU->GetBufferPointer(), idx.data(), ibByteSize);

    geo.VertexBufferGPU = d3dUtil::CreateDefaultBuffer(mDevice, cmdList,
        verts.data(), vbByteSize, geo.VertexBufferUploader);
    geo.IndexBufferGPU = d3dUtil::CreateDefaultBuffer(mDevice, cmdList,
        idx.data(), ibByteSize, geo.IndexBufferUploader);

    geo.VertexByteStride = sizeof(Vertex);
    geo.VertexBufferByteSize = vbByteSize;
    geo.IndexFormat = DXGI_FORMAT_R32_UINT;
    geo.IndexBufferByteSize = ibByteSize;

    SubmeshGeometry sub = {};
    sub.IndexCount = (UINT)idx.size();
    sub.StartIndexLocation = 0;
    sub.BaseVertexLocation = 0;
    geo.DrawArgs["grid"] = sub;
}

void TerrainSystem::Update(
    const XMFLOAT4X4& view,
    const XMFLOAT4X4& proj,
    const XMFLOAT3& eyePos,
    bool frustumCullEnabled,
    float screenWidth,
    UINT frameIndex)
{
    if (frameIndex >= (UINT)gNumFrameResources || !mTileCB[frameIndex])
        return;

    mVisibleCount = 0;
    mCulledCount = 0;
    mSelection.clear();

    const XMMATRIX V = XMLoadFloat4x4(&view);
    const XMMATRIX P = XMLoadFloat4x4(&proj);
    BoundingFrustum fr;
    BoundingFrustum::CreateFromMatrix(fr, P);

    const XMVECTOR eye = XMLoadFloat3(&eyePos);
    const float tanHalfFov = 1.0f / XMVectorGetY(P.r[1]); 

    SelectNode(0, fr, V, eye, tanHalfFov, screenWidth, frustumCullEnabled);
    mVisibleCount = (UINT)mSelection.size();

    UploadBuffer<ObjectConstants>* cb = mTileCB[frameIndex].get();
    for (size_t i = 0; i < mSelection.size(); ++i)
        cb->CopyData((UINT)i, mSelection[i].Constants);
}

void TerrainSystem::SelectNode(
    int nodeIndex,
    const BoundingFrustum& fr,
    const XMMATRIX& view,
    const XMVECTOR& eye,
    float tanHalfFov,
    float screenWidth,
    bool cull)
{
    if (mSelection.size() >= kMaxTiles)
        return;

    const TerrainNode& n = mNodes[nodeIndex];

    if (cull)
    {
        BoundingBox viewBox;
        n.Bounds.Transform(viewBox, view);
        if (fr.Contains(viewBox) == DirectX::DISJOINT)
        {
            ++mCulledCount;
            return;
        }
    }

    const int gridQuads = (n.Level >= kHiFromLevel) ? kHiGridQuads : kLoGridQuads;
    const float spacing = (n.HalfSize * 2.0f) / (float)gridQuads;   // шаг сетки тайла
    const float eps = spacing * kEpsFactor;                          // геометрическая ошибка
    const XMVECTOR c = XMVectorSet(n.Center.x, 0.0f, n.Center.y, 0.0f);
    const XMVECTOR e = XMVectorSetY(eye, 0.0f);
    float d = XMVectorGetX(XMVector3Length(XMVectorSubtract(c, e)));
    d = (std::max)(d, n.HalfSize * 0.5f);   // защита от деления на ~0 над тайлом
    const float rho = eps * screenWidth / (2.0f * d * tanHalfFov);  // ошибка в пикселях

    if (!n.HasChildren || rho <= kTauPx)
    {
        EmitTile(n);
        return;
    }

    for (int child : n.Children)
        SelectNode(child, fr, view, eye, tanHalfFov, screenWidth, cull);
}

void TerrainSystem::EmitTile(const TerrainNode& n)
{
    TileDraw t = {};
    const XMMATRIX world =
        XMMatrixScaling(n.HalfSize, 1.0f, n.HalfSize) *
        XMMatrixTranslation(n.Center.x, kBaseY, n.Center.y);
    XMStoreFloat4x4(&t.Constants.World, XMMatrixTranspose(world));

    // TexTransform: локальный UV тайла [0,1] -> прямоугольник глобального heightmap
    const float w = (n.HalfSize * 2.0f) / kWorldSize;
    const float u0 = n.Center.x / kWorldSize + 0.5f - w * 0.5f;
    const float v0 = n.Center.y / kWorldSize + 0.5f - w * 0.5f;
    const XMMATRIX tex =
        XMMatrixScaling(w, w, 1.0f) *
        XMMatrixTranslation(u0, v0, 0.0f);
    XMStoreFloat4x4(&t.Constants.TexTransform, XMMatrixTranspose(tex));
    XMStoreFloat4x4(&t.Constants.TexTransformDisp, XMMatrixTranspose(tex));

    t.UseHiGrid = (n.Level >= kHiFromLevel);
    mSelection.push_back(t);
}

void TerrainSystem::Draw(
    ID3D12GraphicsCommandList* cmdList,
    UINT frameIndex,
    D3D12_GPU_VIRTUAL_ADDRESS passCbAddress,
    ID3D12DescriptorHeap* srvHeap,
    UINT cbvSrvDescriptorSize,
    D3D12_GPU_VIRTUAL_ADDRESS materialCbAddress)

{
    if (mHeightmapDirty)
    {
        RebuildFinalHeightmap();
        UpdateHeightTexture(cmdList);
        mHeightmapDirty = false;
    }
    if (mSelection.empty() || frameIndex >= (UINT)gNumFrameResources || !mTileCB[frameIndex])
        return;

    const UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));
    ID3D12Resource* objectCB = mTileCB[frameIndex]->Resource();

    CD3DX12_GPU_DESCRIPTOR_HANDLE diffuse(srvHeap->GetGPUDescriptorHandleForHeapStart());
    diffuse.Offset(mDiffuseSrvIndex, cbvSrvDescriptorSize);
    CD3DX12_GPU_DESCRIPTOR_HANDLE height(srvHeap->GetGPUDescriptorHandleForHeapStart());
    height.Offset(mHeightSrvIndex, cbvSrvDescriptorSize);

    cmdList->SetGraphicsRootConstantBufferView(2, passCbAddress);
    cmdList->SetGraphicsRootConstantBufferView(3, materialCbAddress);
    cmdList->SetGraphicsRootDescriptorTable(0, diffuse);   // gDiffuseMap   (t0, PS)
    cmdList->SetGraphicsRootDescriptorTable(4, height);    // gHeightNormalMap (t1, DS/PS)
    cmdList->SetGraphicsRootDescriptorTable(5, diffuse);   // gDiffuseMapB  
    cmdList->SetGraphicsRootDescriptorTable(6, diffuse);   // gMetallicMap  
    cmdList->SetGraphicsRootDescriptorTable(7, diffuse);   // gRoughnessMap 

    for (size_t i = 0; i < mSelection.size(); ++i)
    {
        const TileDraw& t = mSelection[i];
        MeshGeometry* geo = t.UseHiGrid ? &mGridHi : &mGridLo;

        cmdList->SetGraphicsRootConstantBufferView(1,
            objectCB->GetGPUVirtualAddress() + (UINT64)i * objCBByteSize);

        auto vbv = geo->VertexBufferView();
        auto ibv = geo->IndexBufferView();
        cmdList->IASetVertexBuffers(0, 1, &vbv);
        cmdList->IASetIndexBuffer(&ibv);
        cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST);

        cmdList->DrawIndexedInstanced(geo->DrawArgs["grid"].IndexCount, 1, 0, 0, 0);
    }
}

void TerrainSystem::GenerateNoiseHeightMap()
{
    const int kMap = kHeightMapSize;
    mHeightPixelsB.assign((size_t)kMap * kMap, 0);
    for (int y = 0; y < kMap; ++y)
    {
        for (int x = 0; x < kMap; ++x)
        {
            const float nx = x / (float)kMap, ny = y / (float)kMap;
            // Другая частота и смещение для разнообразия
            float v = Fbm(nx * 12.0f + 42.5f, ny * 12.0f + 17.3f, 5);
            const float dx = nx - 0.5f, dy = ny - 0.5f;
            const float d = sqrtf(dx * dx + dy * dy) * 2.0f;
            v *= (std::max)(0.0f, 1.0f - d * d);
            mHeightPixelsB[(size_t)y * kMap + x] =
                (uint8_t)(std::clamp(v, 0.0f, 1.0f) * 255.0f);
        }
    }
    OutputDebugStringA("Terrain: noise heightmap B generated.\n");
}

void TerrainSystem::RebuildFinalHeightmap()
{
    const size_t total = (size_t)kHeightMapSize * kHeightMapSize;
    for (size_t i = 0; i < total; ++i)
    {
        float mask = mBlendMask[i] / 255.0f;
        float a = (float)mHeightPixelsA[i];
        float b = (float)mHeightPixelsB[i];
        float result = a + (b - a) * mask;
        mHeightPixels[i] = (uint8_t)(std::max)(0.0f, (std::min)(255.0f, result));
    }
}

void TerrainSystem::UpdateHeightTexture(ID3D12GraphicsCommandList* cmdList)
{
    auto barrier1 = CD3DX12_RESOURCE_BARRIER::Transition(
        mHeightTex.Get(),
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_COPY_DEST);
    cmdList->ResourceBarrier(1, &barrier1);

    D3D12_SUBRESOURCE_DATA sub = {};
    sub.pData = mHeightPixels.data();
    sub.RowPitch = kHeightMapSize;
    sub.SlicePitch = sub.RowPitch * kHeightMapSize;
    UpdateSubresources(cmdList, mHeightTex.Get(), mHeightUpload.Get(), 0, 0, 1, &sub);

    auto barrier2 = CD3DX12_RESOURCE_BARRIER::Transition(
        mHeightTex.Get(),
        D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    cmdList->ResourceBarrier(1, &barrier2);
}

void TerrainSystem::UpdateBrush(
    float screenX, float screenY,
    float screenWidth, float screenHeight,
    const DirectX::XMFLOAT4X4& view,
    const DirectX::XMFLOAT4X4& proj,
    const DirectX::XMFLOAT3& eyePos,
    bool isActive)
{
    if (!isActive) return;
    if (screenX < 0 || screenX >= screenWidth || screenY < 0 || screenY >= screenHeight) return;

    // NDC координаты
    float ndcX = (2.0f * screenX / screenWidth) - 1.0f;
    float ndcY = 1.0f - (2.0f * screenY / screenHeight);

    // Луч из камеры
    XMMATRIX viewProj = XMLoadFloat4x4(&view) * XMLoadFloat4x4(&proj);
    XMMATRIX invViewProj = XMMatrixInverse(nullptr, viewProj);
    XMVECTOR nearPoint = XMVectorSet(ndcX, ndcY, 0.0f, 1.0f);
    XMVECTOR farPoint = XMVectorSet(ndcX, ndcY, 1.0f, 1.0f);
    nearPoint = XMVector4Transform(nearPoint, invViewProj);
    farPoint = XMVector4Transform(farPoint, invViewProj);
    nearPoint = XMVectorScale(nearPoint, 1.0f / XMVectorGetW(nearPoint));
    farPoint = XMVectorScale(farPoint, 1.0f / XMVectorGetW(farPoint));

    XMVECTOR rayDir = XMVectorSubtract(farPoint, nearPoint);
    rayDir = XMVector3Normalize(rayDir);
    XMVECTOR rayOrigin = XMLoadFloat3(&eyePos);

    // Пересечение с плоскостью y = kBaseY
    float rayDirY = XMVectorGetY(rayDir);
    if (fabsf(rayDirY) < 1e-6f) return;
    float t = (kBaseY - XMVectorGetY(rayOrigin)) / rayDirY;
    if (t < 0.0f) return;

    XMVECTOR hitPoint = XMVectorAdd(rayOrigin, XMVectorScale(rayDir, t));
    float hitX = XMVectorGetX(hitPoint);
    float hitZ = XMVectorGetZ(hitPoint);

    // UV heightmap
    float u = (hitX / kWorldSize) + 0.5f;
    float v = (hitZ / kWorldSize) + 0.5f;
    if (u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f) return;

    // Пиксели
    int px = (int)(u * (kHeightMapSize - 1));
    int py = (int)(v * (kHeightMapSize - 1));

    // Параметры кисти
    const float brushRadiusWorld = 12.0f;
    const float brushRadiusPx = (brushRadiusWorld / kWorldSize) * kHeightMapSize;
    const float brushStrength = 0.25f;

    int r = (int)ceilf(brushRadiusPx);
    for (int dy = -r; dy <= r; ++dy)
    {
        for (int dx = -r; dx <= r; ++dx)
        {
            int x = px + dx;
            int y = py + dy;
            if (x < 0 || x >= kHeightMapSize || y < 0 || y >= kHeightMapSize) continue;

            float dist = sqrtf((float)(dx * dx + dy * dy));
            if (dist > brushRadiusPx) continue;

            float falloff = 1.0f - (dist / brushRadiusPx);
            falloff = falloff * falloff;

            float add = brushStrength * falloff * 255.0f;
            size_t idx = (size_t)y * kHeightMapSize + x;
            int newVal = (int)mBlendMask[idx] + (int)add;
            mBlendMask[idx] = (uint8_t)(std::min)(255, newVal);
        }
    }

    mHeightmapDirty = true;
}
