#include "../../Common/d3dApp.h"
#include "../../Common/MathHelper.h"
#include "../../Common/UploadBuffer.h"
#include "../../Common/DDSTextureLoader.h"
#include "../../Common/LightSystem.h"
#include "../../Common/RenderingSystem.h"



#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"

#include <vector>
#include <string>
#include <stdexcept>
#include <unordered_map>
#include <algorithm>
#include <codecvt>
#include <locale>

using Microsoft::WRL::ComPtr;
using namespace DirectX;
using namespace DirectX::PackedVector;

// ============================================================
// Vertex: Position + TexCoord (matches shader)
// ============================================================
struct Vertex
{
    XMFLOAT3 Pos;
    XMFLOAT3 Normal;
    XMFLOAT2 Tex;
};


// ============================================================
// Per-object constant buffer — must match cbuffer in HLSL
// ============================================================
struct ObjectConstants
{
    XMFLOAT4X4 WorldViewProj = MathHelper::Identity4x4();
    XMFLOAT2   TexOffset = { 0.0f, 0.0f };
    XMFLOAT2   TexScale = { 1.0f, 1.0f };
};

// ============================================================
// Per-material submesh info
// ============================================================
struct MaterialSubmesh
{
    std::string MaterialName;
    int         TextureSrvIndex = -1; // index in SRV heap, -1 = no texture
    UINT        IndexCount = 0;
    UINT        StartIndexLocation = 0;
    INT         BaseVertexLocation = 0;
};

// ============================================================
// Loaded texture resource
// ============================================================
struct LoadedTexture
{
    std::string Name;
    std::wstring Filename;
    ComPtr<ID3D12Resource> Resource = nullptr;
    ComPtr<ID3D12Resource> UploadHeap = nullptr;
};


// ============================================================
// Helper: string -> wstring
// ============================================================
static std::wstring ToWide(const std::string& str)
{
    if (str.empty()) return L"";
    int sz = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), nullptr, 0);
    std::wstring wstr(sz, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), &wstr[0], sz);
    return wstr;
}

// ============================================================
// Main Application
// ============================================================
class BoxApp : public D3DApp
{
public:
    BoxApp(HINSTANCE hInstance);
    BoxApp(const BoxApp& rhs) = delete;
    BoxApp& operator=(const BoxApp& rhs) = delete;
    ~BoxApp();

    virtual bool Initialize() override;

private:
    virtual void OnResize() override;
    virtual void Update(const GameTimer& gt) override;
    virtual void Draw(const GameTimer& gt) override;

    virtual void OnMouseDown(WPARAM btnState, int x, int y) override;
    virtual void OnMouseUp(WPARAM btnState, int x, int y) override;
    virtual void OnMouseMove(WPARAM btnState, int x, int y) override;

    void LoadTextures();
    void BuildDescriptorHeaps();
    void BuildConstantBuffers();
    void BuildRootSignature();
    // void BuildShadersAndInputLayout();
    void BuildBoxGeometry();
    // void BuildPSO();

    // Create a white 1x1 fallback texture for materials without map_Kd
    void CreateDefaultWhiteTexture();
    void SetupLights();

private:
    ComPtr<ID3D12RootSignature> mRootSignature = nullptr;
    ComPtr<ID3D12DescriptorHeap> mCbvSrvHeap = nullptr;     // combined CBV+SRV heap

    std::unique_ptr<UploadBuffer<ObjectConstants>> mObjectCB = nullptr;

    std::unique_ptr<MeshGeometry> mBoxGeo = nullptr;

    // ComPtr<ID3DBlob> mvsByteCode = nullptr;
    // ComPtr<ID3DBlob> mpsByteCode = nullptr;

    // std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;

    // ComPtr<ID3D12PipelineState> mPSO = nullptr;

    XMFLOAT4X4 mWorld = MathHelper::Identity4x4();
    XMFLOAT4X4 mView = MathHelper::Identity4x4();
    XMFLOAT4X4 mProj = MathHelper::Identity4x4();

    float mTheta = 1.5f * XM_PI;
    float mPhi = XM_PIDIV4;
    float mRadius = 5.0f;

    POINT mLastMousePos;

    // ---------- Textures & Materials ----------
    std::vector<LoadedTexture>   mTextures;              // all loaded textures
    std::vector<MaterialSubmesh> mMaterialSubmeshes;      // per-material draw info
    std::unordered_map<std::string, int> mTextureIndexMap; // texture path -> index in mTextures

    // Heap layout:
    //   slot 0          : CBV (ObjectConstants)
    //   slot 1          : default white texture SRV
    //   slot 2 .. N+1   : loaded textures SRV
    int mCbvOffset = 0;
    int mDefaultTexSrvOffset = 1;
    int mTextureSrvStartOffset = 2;

    ComPtr<ID3D12Resource> mWhiteTexResource = nullptr;
    ComPtr<ID3D12Resource> mWhiteTexUploadHeap = nullptr;

    std::unique_ptr<RenderingSystem> mRenderingSystem;
    std::unique_ptr<UploadBuffer<GBufferObjectConstants>> mGBufferObjectCB;

};

// ============================================================
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE prevInstance,
    PSTR cmdLine, int showCmd)
{
#if defined(DEBUG) | defined(_DEBUG)
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif
    try
    {
        BoxApp theApp(hInstance);
        if (!theApp.Initialize())
            return 0;
        return theApp.Run();
    }
    catch (DxException& e)
    {
        MessageBox(nullptr, e.ToString().c_str(), L"HR Failed", MB_OK);
        return 0;
    }
}

BoxApp::BoxApp(HINSTANCE hInstance) : D3DApp(hInstance) {}
BoxApp::~BoxApp() {}

bool BoxApp::Initialize()
{
    if (!D3DApp::Initialize())
        return false;

    ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

    OutputDebugStringA("=== Starting RenderingSystem initialization ===\n");

    // 1. Сначала RenderingSystem
    mRenderingSystem = std::make_unique<RenderingSystem>();

    OutputDebugStringA("=== RenderingSystem created, calling Initialize ===\n");

    mRenderingSystem->Initialize(
        md3dDevice.Get(),
        mCommandList.Get(),
        mClientWidth,
        mClientHeight,
        mBackBufferFormat,
        mDepthStencilFormat
    );

    OutputDebugStringA("=== RenderingSystem initialized ===\n");

    // 2. Настройка света
    SetupLights();

    OutputDebugStringA("=== Lights setup done ===\n");

    // 3. Загрузка текстур
    LoadTextures();

    OutputDebugStringA("=== Textures loaded ===\n");

    // 4. Descriptor heaps
    BuildDescriptorHeaps();

    OutputDebugStringA("=== Descriptor heaps built ===\n");

    // 5. Константные буферы
    BuildConstantBuffers();

    OutputDebugStringA("=== Constant buffers built ===\n");

    // 6. Root signature
    BuildRootSignature();

    OutputDebugStringA("=== Root signature built ===\n");

    // 7. Геометрия
    BuildBoxGeometry();

    OutputDebugStringA("=== Geometry built ===\n");

    ThrowIfFailed(mCommandList->Close());
    ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

    FlushCommandQueue();

    OutputDebugStringA("=== Initialize complete ===\n");

    return true;
}

void BoxApp::OnResize()
{
    D3DApp::OnResize();

    if (mRenderingSystem)
    {
        mRenderingSystem->OnResize(md3dDevice.Get(), mClientWidth, mClientHeight);
    }

    XMMATRIX P = XMMatrixPerspectiveFovLH(0.25f * MathHelper::Pi, AspectRatio(), 1.0f, 1000.0f);
    XMStoreFloat4x4(&mProj, P);
}

void BoxApp::Update(const GameTimer& gt)
{
    float x = mRadius * sinf(mPhi) * cosf(mTheta);
    float z = mRadius * sinf(mPhi) * sinf(mTheta);
    float y = mRadius * cosf(mPhi);

    XMVECTOR pos = XMVectorSet(x, y, z, 1.0f);
    XMVECTOR target = XMVectorZero();
    XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

    XMMATRIX view = XMMatrixLookAtLH(pos, target, up);
    XMStoreFloat4x4(&mView, view);

    XMMATRIX world = XMLoadFloat4x4(&mWorld);
    XMMATRIX proj = XMLoadFloat4x4(&mProj);
    XMMATRIX worldViewProj = world * view * proj;

    ObjectConstants objConstants;
    XMStoreFloat4x4(&objConstants.WorldViewProj, XMMatrixTranspose(worldViewProj));

    float totalTime = gt.TotalTime();
    objConstants.TexOffset = XMFLOAT2(totalTime * 0.05f, 0.0f);
    objConstants.TexScale = XMFLOAT2(1.0f, 1.0f);

    // mObjectCB->CopyData(0, objConstants);
}

void BoxApp::Draw(const GameTimer& gt)
{
    ThrowIfFailed(mDirectCmdListAlloc->Reset());
    ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

    // Обновляем матрицы камеры
    float x = mRadius * sinf(mPhi) * cosf(mTheta);
    float z = mRadius * sinf(mPhi) * sinf(mTheta);
    float y = mRadius * cosf(mPhi);

    XMVECTOR pos = XMVectorSet(x, y, z, 1.0f);
    XMVECTOR target = XMVectorZero();
    XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

    XMMATRIX view = XMMatrixLookAtLH(pos, target, up);
    XMMATRIX proj = XMLoadFloat4x4(&mProj);

    mRenderingSystem->SetViewProjection(view, proj);
    mRenderingSystem->SetCameraPosition(XMFLOAT3(x, y, z));

    // ==================== Geometry Pass ====================
    mRenderingSystem->BeginGeometryPass(mCommandList.Get());

    // Устанавливаем descriptor heaps
    ID3D12DescriptorHeap* descriptorHeaps[] = { mCbvSrvHeap.Get() };
    mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

    // Устанавливаем геометрию
    mCommandList->IASetVertexBuffers(0, 1, &mBoxGeo->VertexBufferView());
    mCommandList->IASetIndexBuffer(&mBoxGeo->IndexBufferView());
    mCommandList->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // Обновляем и биндим константы объекта
    XMMATRIX world = XMLoadFloat4x4(&mWorld);
    XMMATRIX worldViewProj = world * view * proj;
    XMMATRIX worldInvTranspose = MathHelper::InverseTranspose(world);

    GBufferObjectConstants objConstants;
    XMStoreFloat4x4(&objConstants.World, XMMatrixTranspose(world));
    XMStoreFloat4x4(&objConstants.WorldViewProj, XMMatrixTranspose(worldViewProj));
    XMStoreFloat4x4(&objConstants.WorldInvTranspose, XMMatrixTranspose(worldInvTranspose));
    if (mGBufferObjectCB)
    {
        GBufferObjectConstants objConstants;
        XMStoreFloat4x4(&objConstants.World, XMMatrixTranspose(world));
        XMStoreFloat4x4(&objConstants.WorldViewProj, XMMatrixTranspose(worldViewProj));
        XMStoreFloat4x4(&objConstants.WorldInvTranspose, XMMatrixTranspose(worldInvTranspose));
        mGBufferObjectCB->CopyData(0, objConstants);
    }

    // Root parameter 0: CBV
    mCommandList->SetGraphicsRootDescriptorTable(0,
        mCbvSrvHeap->GetGPUDescriptorHandleForHeapStart());

    // Рисуем каждый submesh с его текстурой
    for (const auto& submesh : mMaterialSubmeshes)
    {
        int srvHeapIndex;
        if (submesh.TextureSrvIndex < 0)
            srvHeapIndex = mDefaultTexSrvOffset;
        else
            srvHeapIndex = mTextureSrvStartOffset + submesh.TextureSrvIndex;

        CD3DX12_GPU_DESCRIPTOR_HANDLE texHandle(
            mCbvSrvHeap->GetGPUDescriptorHandleForHeapStart());
        texHandle.Offset(srvHeapIndex, mCbvSrvUavDescriptorSize);
        mCommandList->SetGraphicsRootDescriptorTable(1, texHandle);

        mCommandList->DrawIndexedInstanced(
            submesh.IndexCount, 1,
            submesh.StartIndexLocation,
            submesh.BaseVertexLocation, 0);
    }

    mRenderingSystem->EndGeometryPass(mCommandList.Get());

    // ==================== Lighting Pass ====================
    mRenderingSystem->ExecuteLightingPass(
        mCommandList.Get(),
        CurrentBackBuffer(),
        CurrentBackBufferView(),
        DepthStencilView()
    );

    ThrowIfFailed(mCommandList->Close());

    ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

    ThrowIfFailed(mSwapChain->Present(0, 0));
    mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;

    FlushCommandQueue();
}

void BoxApp::OnMouseDown(WPARAM btnState, int x, int y)
{
    mLastMousePos.x = x;
    mLastMousePos.y = y;
    SetCapture(mhMainWnd);
}

void BoxApp::OnMouseUp(WPARAM btnState, int x, int y)
{
    ReleaseCapture();
}

void BoxApp::OnMouseMove(WPARAM btnState, int x, int y)
{
    if ((btnState & MK_LBUTTON) != 0)
    {
        float dx = XMConvertToRadians(0.25f * static_cast<float>(x - mLastMousePos.x));
        float dy = XMConvertToRadians(0.25f * static_cast<float>(y - mLastMousePos.y));
        mTheta += dx;
        mPhi += dy;
        mPhi = MathHelper::Clamp(mPhi, 0.1f, MathHelper::Pi - 0.1f);
    }
    else if ((btnState & MK_RBUTTON) != 0)
    {
        float dx = 0.005f * static_cast<float>(x - mLastMousePos.x);
        float dy = 0.005f * static_cast<float>(y - mLastMousePos.y);
        mRadius += dx - dy;
        mRadius = MathHelper::Clamp(mRadius, 3.0f, 15.0f);
    }
    mLastMousePos.x = x;
    mLastMousePos.y = y;
}

// ============================================================
// Load OBJ, parse materials, load referenced DDS textures
// ============================================================
void BoxApp::LoadTextures()
{
    // --- Parse OBJ to discover material texture paths ---
    std::string inputfile = "sponza.obj";
    tinyobj::ObjReaderConfig reader_config;
    reader_config.triangulate = true;

    tinyobj::ObjReader reader;
    if (!reader.ParseFromFile(inputfile, reader_config))
    {
        if (!reader.Error().empty())
            OutputDebugStringA(reader.Error().c_str());
        throw std::runtime_error("Failed to load sponza.obj");
    }

    auto& materials = reader.GetMaterials();

    // Collect unique diffuse texture paths
    for (const auto& mat : materials)
    {
        if (mat.diffuse_texname.empty())
            continue;

        // Normalize path: replace .tga with .dds
        std::string texPath = mat.diffuse_texname;
        // Replace backslash with forward slash
        std::replace(texPath.begin(), texPath.end(), '\\', '/');

        // Change extension to .dds
        size_t dotPos = texPath.rfind('.');
        if (dotPos != std::string::npos)
            texPath = texPath.substr(0, dotPos) + ".dds";

        if (mTextureIndexMap.count(texPath) > 0)
            continue; // already registered

        int idx = (int)mTextures.size();
        mTextureIndexMap[texPath] = idx;

        LoadedTexture lt;
        lt.Name = texPath;
        lt.Filename = ToWide(texPath);
        mTextures.push_back(std::move(lt));
    }

    // --- Load DDS files ---
    for (auto& tex : mTextures)
    {
        wchar_t fullPath[MAX_PATH];
        GetFullPathNameW(tex.Filename.c_str(), MAX_PATH, fullPath, nullptr);

        char buf[512];
        sprintf_s(buf, "Looking for: %ls\n", fullPath);
        OutputDebugStringA(buf);

        // Проверим существует ли файл вообще
        DWORD attribs = GetFileAttributesW(fullPath);
        if (attribs == INVALID_FILE_ATTRIBUTES)
        {
            sprintf_s(buf, "  FILE NOT FOUND!\n");
            OutputDebugStringA(buf);
        }
        else
        {
            sprintf_s(buf, "  File exists, size check...\n");
            OutputDebugStringA(buf);
        }

        HRESULT hr = DirectX::CreateDDSTextureFromFile12(
            md3dDevice.Get(), mCommandList.Get(),
            tex.Filename.c_str(),
            tex.Resource, tex.UploadHeap);

        if (FAILED(hr))
        {
            sprintf_s(buf, "  LOAD FAILED! HRESULT = 0x%08X\n", (unsigned)hr);
            OutputDebugStringA(buf);
            tex.Resource = nullptr;
        }
        else
        {
            sprintf_s(buf, "  LOADED OK\n");
            OutputDebugStringA(buf);
        }
    }
    char buf[256];
    sprintf_s(buf, "=== Total textures found in MTL: %d ===\n", (int)mTextures.size());
    OutputDebugStringA(buf);

    for (size_t i = 0; i < mTextures.size(); ++i)
    {
        sprintf_s(buf, "Texture[%d]: '%s' -> %s\n",
            (int)i,
            mTextures[i].Name.c_str(),
            mTextures[i].Resource ? "LOADED OK" : "FAILED");
        OutputDebugStringA(buf);
    }

    sprintf_s(buf, "=== Unique texture paths in map: %d ===\n", (int)mTextureIndexMap.size());
    OutputDebugStringA(buf);
}

// ============================================================
// Create a 1x1 white texture as fallback
// ============================================================
void BoxApp::CreateDefaultWhiteTexture()
{
    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width = 1;
    texDesc.Height = 1;
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels = 1;
    texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    texDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    ThrowIfFailed(md3dDevice->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &texDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&mWhiteTexResource)));

    const UINT64 uploadBufferSize = GetRequiredIntermediateSize(mWhiteTexResource.Get(), 0, 1);

    ThrowIfFailed(md3dDevice->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize),
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&mWhiteTexUploadHeap)));

    UINT8 whitePixel[] = { 255, 255, 255, 255 };
    D3D12_SUBRESOURCE_DATA subresourceData = {};
    subresourceData.pData = whitePixel;
    subresourceData.RowPitch = 4;
    subresourceData.SlicePitch = 4;

    UpdateSubresources(mCommandList.Get(),
        mWhiteTexResource.Get(), mWhiteTexUploadHeap.Get(),
        0, 0, 1, &subresourceData);

    mCommandList->ResourceBarrier(1,
        &CD3DX12_RESOURCE_BARRIER::Transition(mWhiteTexResource.Get(),
            D3D12_RESOURCE_STATE_COPY_DEST,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
}

// ============================================================
void BoxApp::BuildDescriptorHeaps()
{
    CreateDefaultWhiteTexture();

    // Heap layout:
    //   slot 0                          : CBV
    //   slot 1                          : white texture SRV (fallback)
    //   slot 2 .. (mTextures.size()+1)  : loaded texture SRVs
    mCbvOffset = 0;
    mDefaultTexSrvOffset = 1;
    mTextureSrvStartOffset = 2;

    UINT numDescriptors = 1 + 1 + (UINT)mTextures.size(); // CBV + white + textures

    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.NumDescriptors = numDescriptors;
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    heapDesc.NodeMask = 0;
    ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&mCbvSrvHeap)));

    CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor(
        mCbvSrvHeap->GetCPUDescriptorHandleForHeapStart());

    // Slot 0: skip (CBV, created later in BuildConstantBuffers)
    hDescriptor.Offset(1, mCbvSrvUavDescriptorSize);

    // Slot 1: white texture SRV
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Texture2D.MipLevels = 1;
        srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
        md3dDevice->CreateShaderResourceView(
            mWhiteTexResource.Get(), &srvDesc, hDescriptor);
    }
    hDescriptor.Offset(1, mCbvSrvUavDescriptorSize);

    // Slots 2..N+1: loaded textures
    for (size_t i = 0; i < mTextures.size(); ++i)
    {
        if (mTextures[i].Resource)
        {
            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.Format = mTextures[i].Resource->GetDesc().Format;
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Texture2D.MostDetailedMip = 0;
            srvDesc.Texture2D.MipLevels = mTextures[i].Resource->GetDesc().MipLevels;
            srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
            md3dDevice->CreateShaderResourceView(
                mTextures[i].Resource.Get(), &srvDesc, hDescriptor);
        }
        else
        {
            // Failed texture — point to white
            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Texture2D.MostDetailedMip = 0;
            srvDesc.Texture2D.MipLevels = 1;
            md3dDevice->CreateShaderResourceView(
                mWhiteTexResource.Get(), &srvDesc, hDescriptor);
        }
        hDescriptor.Offset(1, mCbvSrvUavDescriptorSize);
    }
}

// ============================================================
void BoxApp::BuildConstantBuffers()
{
    // ====== Создаём константный буфер для GBuffer pass ======
    mGBufferObjectCB = std::make_unique<UploadBuffer<GBufferObjectConstants>>(
        md3dDevice.Get(), 1, true);

    UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(GBufferObjectConstants));
    D3D12_GPU_VIRTUAL_ADDRESS cbAddress = mGBufferObjectCB->Resource()->GetGPUVirtualAddress();

    D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc;
    cbvDesc.BufferLocation = cbAddress;
    cbvDesc.SizeInBytes = objCBByteSize;

    // CBV at slot 0 in the heap
    md3dDevice->CreateConstantBufferView(&cbvDesc,
        mCbvSrvHeap->GetCPUDescriptorHandleForHeapStart());
}

// ============================================================
void BoxApp::BuildRootSignature()
{
    CD3DX12_DESCRIPTOR_RANGE cbvTable;
    cbvTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0); // b0

        CD3DX12_DESCRIPTOR_RANGE srvTable;
    srvTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0); // t0

    CD3DX12_ROOT_PARAMETER slotRootParameter[2];
    slotRootParameter[0].InitAsDescriptorTable(1, &cbvTable);
    slotRootParameter[1].InitAsDescriptorTable(1, &srvTable);

    // Static sampler: linear filter + wrap (tiling)
    D3D12_STATIC_SAMPLER_DESC sampler = {};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.MipLODBias = 0;
    sampler.MaxAnisotropy = 1;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
    sampler.MinLOD = 0.0f;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderRegister = 0;   // s0
    sampler.RegisterSpace = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(2, slotRootParameter, 1, &sampler,
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    ComPtr<ID3DBlob> serializedRootSig = nullptr;
    ComPtr<ID3DBlob> errorBlob = nullptr;
    HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc,
        D3D_ROOT_SIGNATURE_VERSION_1,
        serializedRootSig.GetAddressOf(),
        errorBlob.GetAddressOf());

    if (errorBlob != nullptr)
        ::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
    ThrowIfFailed(hr);

    ThrowIfFailed(md3dDevice->CreateRootSignature(
        0,
        serializedRootSig->GetBufferPointer(),
        serializedRootSig->GetBufferSize(),
        IID_PPV_ARGS(&mRootSignature)));
}
// ============================================================
/* void BoxApp::BuildShadersAndInputLayout()
{
    // Шейдеры теперь не нужны здесь - они в RenderingSystem
    // Но оставим для совместимости, если нужно

    mInputLayout = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12,
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24,
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };
}*/

// ============================================================
void BoxApp::BuildBoxGeometry()
{
    std::string inputfile = "sponza.obj";
    tinyobj::ObjReaderConfig reader_config;
    reader_config.triangulate = true;

    tinyobj::ObjReader reader;
    if (!reader.ParseFromFile(inputfile, reader_config))
    {
        if (!reader.Error().empty())
            OutputDebugStringA(reader.Error().c_str());
        throw std::runtime_error("Failed to load sponza.obj");
    }

    auto& attrib = reader.GetAttrib();
    auto& shapes = reader.GetShapes();
    auto& materials = reader.GetMaterials();

    // ---------- Group faces by material_id ----------
    // Key: material_id, Value: list of vertex indices (3 per face)
    struct FaceData
    {
        tinyobj::index_t idx[3];
    };
    std::unordered_map<int, std::vector<FaceData>> matFaces;

    for (const auto& shape : shapes)
    {
        size_t indexOffset = 0;
        for (size_t f = 0; f < shape.mesh.num_face_vertices.size(); ++f)
        {
            int matId = shape.mesh.material_ids[f];
            // triangulated, so always 3
            FaceData fd;
            fd.idx[0] = shape.mesh.indices[indexOffset + 0];
            fd.idx[1] = shape.mesh.indices[indexOffset + 1];
            fd.idx[2] = shape.mesh.indices[indexOffset + 2];
            matFaces[matId].push_back(fd);
            indexOffset += 3;
        }
    }

    // ---------- Build vertex/index buffers, sorted by material ----------
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    vertices.reserve(500000);
    indices.reserve(500000);

    mMaterialSubmeshes.clear();

    for (auto& pair : matFaces)
    {
        int matId = pair.first;
        auto& faces = pair.second;

        MaterialSubmesh ms;

        if (matId >= 0 && matId < (int)materials.size())
        {
            ms.MaterialName = materials[matId].name;

            // Find texture
            std::string texPath = materials[matId].diffuse_texname;
            if (!texPath.empty())
            {
                std::replace(texPath.begin(), texPath.end(), '\\', '/');
                size_t dotPos = texPath.rfind('.');
                if (dotPos != std::string::npos)
                    texPath = texPath.substr(0, dotPos) + ".dds";

                auto it = mTextureIndexMap.find(texPath);
                if (it != mTextureIndexMap.end())
                {
                    if (mTextures[it->second].Resource)
                        ms.TextureSrvIndex = it->second;
                    else
                        ms.TextureSrvIndex = -1;
                }
            }
        }
        else
        {
            ms.MaterialName = "__no_material__";
        }

        ms.StartIndexLocation = (UINT)indices.size();
        ms.BaseVertexLocation = 0;

        for (size_t fi = 0; fi < faces.size(); ++fi)
        {
            const FaceData& face = faces[fi];
            for (int v = 0; v < 3; ++v)
            {
                const tinyobj::index_t& idx = face.idx[v];
                Vertex vert = {};

                vert.Pos.x = attrib.vertices[3 * idx.vertex_index + 0] * 0.01f;
                vert.Pos.y = attrib.vertices[3 * idx.vertex_index + 1] * 0.01f;
                vert.Pos.z = attrib.vertices[3 * idx.vertex_index + 2] * 0.01f;

                // Загружаем нормаль
                if (idx.normal_index >= 0 && !attrib.normals.empty())
                {
                    vert.Normal.x = attrib.normals[3 * idx.normal_index + 0];
                    vert.Normal.y = attrib.normals[3 * idx.normal_index + 1];
                    vert.Normal.z = attrib.normals[3 * idx.normal_index + 2];
                }
                else
                {
                    vert.Normal = XMFLOAT3(0.0f, 1.0f, 0.0f); // Default up
                }

                if (idx.texcoord_index >= 0 && !attrib.texcoords.empty())
                {
                    vert.Tex.x = attrib.texcoords[2 * idx.texcoord_index + 0];
                    vert.Tex.y = 1.0f - attrib.texcoords[2 * idx.texcoord_index + 1];
                }
                else
                {
                    vert.Tex = XMFLOAT2(0.0f, 0.0f);
                }

                indices.push_back((std::uint32_t)vertices.size());
                vertices.push_back(vert);
            }
        }

        ms.IndexCount = (UINT)indices.size() - ms.StartIndexLocation;
        mMaterialSubmeshes.push_back(ms);
    }

    // ---------- Create GPU buffers ----------
    const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);
    const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint32_t);

    mBoxGeo = std::make_unique<MeshGeometry>();
    mBoxGeo->Name = "sponzaGeo";

    ThrowIfFailed(D3DCreateBlob(vbByteSize, &mBoxGeo->VertexBufferCPU));
    CopyMemory(mBoxGeo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

    ThrowIfFailed(D3DCreateBlob(ibByteSize, &mBoxGeo->IndexBufferCPU));
    CopyMemory(mBoxGeo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

    mBoxGeo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(
        md3dDevice.Get(), mCommandList.Get(),
        vertices.data(), vbByteSize,
        mBoxGeo->VertexBufferUploader);

    mBoxGeo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(
        md3dDevice.Get(), mCommandList.Get(),
        indices.data(), ibByteSize,
        mBoxGeo->IndexBufferUploader);

    mBoxGeo->VertexByteStride = sizeof(Vertex);
    mBoxGeo->VertexBufferByteSize = vbByteSize;
    mBoxGeo->IndexFormat = DXGI_FORMAT_R32_UINT;
    mBoxGeo->IndexBufferByteSize = ibByteSize;

    // Still add a "box" DrawArgs for compatibility, covers everything
    SubmeshGeometry submesh;
    submesh.IndexCount = (UINT)indices.size();
    submesh.StartIndexLocation = 0;
    submesh.BaseVertexLocation = 0;
    mBoxGeo->DrawArgs["box"] = submesh;
    char buf[256];
    sprintf_s(buf, "=== Total submeshes: %d, vertices: %d, indices: %d ===\n",
        (int)mMaterialSubmeshes.size(), (int)vertices.size(), (int)indices.size());
    OutputDebugStringA(buf);

    for (size_t i = 0; i < mMaterialSubmeshes.size(); ++i)
    {
        sprintf_s(buf, "Submesh[%d]: mat='%s' texIdx=%d indexCount=%d startIdx=%d\n",
            (int)i,
            mMaterialSubmeshes[i].MaterialName.c_str(),
            mMaterialSubmeshes[i].TextureSrvIndex,
            mMaterialSubmeshes[i].IndexCount,
            mMaterialSubmeshes[i].StartIndexLocation);
        OutputDebugStringA(buf);
    }
}

/*
void BoxApp::BuildPSO()
{
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc;
    ZeroMemory(&psoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));

        psoDesc.InputLayout = { mInputLayout.data(), (UINT)mInputLayout.size() };
    psoDesc.pRootSignature = mRootSignature.Get();
    psoDesc.VS = { reinterpret_cast<BYTE*>(mvsByteCode->GetBufferPointer()), mvsByteCode->GetBufferSize() };
    psoDesc.PS = { reinterpret_cast<BYTE*>(mpsByteCode->GetBufferPointer()), mpsByteCode->GetBufferSize() };
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = mBackBufferFormat;
    psoDesc.SampleDesc.Count = m4xMsaaState ? 4 : 1;
    psoDesc.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;
    psoDesc.DSVFormat = mDepthStencilFormat;

    ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&mPSO)));
}*/

// ============================================================
// Setup scene lights
// ============================================================
void BoxApp::SetupLights()
{
    mRenderingSystem->ClearLights();

    // Directional light (солнце)
    DirectionalLight dirLight;
    dirLight.Direction = XMFLOAT3(0.57735f, -0.57735f, 0.57735f);
    dirLight.Color = XMFLOAT3(1.0f, 0.95f, 0.8f);
    dirLight.Intensity = 1.0f;
    mRenderingSystem->AddDirectionalLight(dirLight);

    // Point light 1 (красный)
    PointLight pointLight1;
    pointLight1.Position = XMFLOAT3(-2.0f, 1.0f, 0.0f);
    pointLight1.Color = XMFLOAT3(1.0f, 0.3f, 0.3f);
    pointLight1.Intensity = 2.0f;
    pointLight1.Radius = 5.0f;
    mRenderingSystem->AddPointLight(pointLight1);

    // Point light 2 (синий)
    PointLight pointLight2;
    pointLight2.Position = XMFLOAT3(2.0f, 1.0f, 0.0f);
    pointLight2.Color = XMFLOAT3(0.3f, 0.3f, 1.0f);
    pointLight2.Intensity = 2.0f;
    pointLight2.Radius = 5.0f;
    mRenderingSystem->AddPointLight(pointLight2);

    // Spot light (белый, направлен вниз)
    SpotLight spotLight;
    spotLight.Position = XMFLOAT3(0.0f, 3.0f, 0.0f);
    spotLight.Direction = XMFLOAT3(0.0f, -1.0f, 0.0f);
    spotLight.Color = XMFLOAT3(1.0f, 1.0f, 1.0f);
    spotLight.Intensity = 3.0f;
    spotLight.Radius = 10.0f;
    spotLight.SpotAngle = cosf(XM_PI / 6.0f);
    spotLight.InnerAngle = cosf(XM_PI / 8.0f);
    mRenderingSystem->AddSpotLight(spotLight);
}