#include "RenderingSystem.h"
#include "d3dUtil.h"
#include "MathHelper.h"

using namespace DirectX;

// Вершина для fullscreen quad
struct QuadVertex
{
    XMFLOAT3 Position;
    XMFLOAT2 TexCoord;
};

RenderingSystem::RenderingSystem()
{
    XMStoreFloat4x4(&mView, XMMatrixIdentity());
    XMStoreFloat4x4(&mProj, XMMatrixIdentity());
    XMStoreFloat4x4(&mViewProj, XMMatrixIdentity());
    XMStoreFloat4x4(&mInvViewProj, XMMatrixIdentity());
    mCameraPosition = { 0.0f, 0.0f, 0.0f };
}

RenderingSystem::~RenderingSystem() = default;

void RenderingSystem::Initialize(
    ID3D12Device* device,
    ID3D12GraphicsCommandList* cmdList,
    UINT width,
    UINT height,
    DXGI_FORMAT backBufferFormat,
    DXGI_FORMAT depthStencilFormat)
{
    OutputDebugStringA("RenderingSystem::Initialize - start\n");

    mDevice = device;
    mWidth = width;
    mHeight = height;
    mBackBufferFormat = backBufferFormat;
    mDepthStencilFormat = depthStencilFormat;

    mCbvSrvUavDescriptorSize = device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    OutputDebugStringA("RenderingSystem::Initialize - initializing GBuffer\n");

    // Инициализация GBuffer
    mGBuffer.Initialize(device, width, height);

    OutputDebugStringA("RenderingSystem::Initialize - creating lighting CB\n");

    // Создание константного буфера для освещения
    mLightingCB = std::make_unique<UploadBuffer<LightingConstants>>(device, 1, true);

    OutputDebugStringA("RenderingSystem::Initialize - building root signatures\n");

    BuildRootSignatures(device);

    OutputDebugStringA("RenderingSystem::Initialize - building shaders\n");

    BuildShaders();

    OutputDebugStringA("RenderingSystem::Initialize - building PSOs\n");

    BuildPSOs(device);

    OutputDebugStringA("RenderingSystem::Initialize - building fullscreen quad\n");

    BuildFullscreenQuad(device, cmdList);

    OutputDebugStringA("RenderingSystem::Initialize - building lighting pass heap\n");

    BuildLightingPassDescriptorHeap(device);

    OutputDebugStringA("RenderingSystem::Initialize - complete\n");
}
void RenderingSystem::OnResize(ID3D12Device* device, UINT width, UINT height)
{
    mWidth = width;
    mHeight = height;
    mGBuffer.OnResize(device, width, height);
    
    // Пересоздаём descriptor heap для lighting pass
    BuildLightingPassDescriptorHeap(device);
}

void RenderingSystem::SetViewProjection(const XMMATRIX& view, const XMMATRIX& proj)
{
    XMStoreFloat4x4(&mView, view);
    XMStoreFloat4x4(&mProj, proj);
    
    XMMATRIX viewProj = view * proj;
    XMStoreFloat4x4(&mViewProj, viewProj);
    
    XMMATRIX invViewProj = XMMatrixInverse(nullptr, viewProj);
    XMStoreFloat4x4(&mInvViewProj, invViewProj);
}

void RenderingSystem::SetCameraPosition(const XMFLOAT3& pos)
{
    mCameraPosition = pos;
}

void RenderingSystem::AddDirectionalLight(const DirectionalLight& light)
{
    if (mDirectionalLights.size() < MaxDirectionalLights)
        mDirectionalLights.push_back(light);
}

void RenderingSystem::AddPointLight(const PointLight& light)
{
    if (mPointLights.size() < MaxPointLights)
        mPointLights.push_back(light);
}

void RenderingSystem::AddSpotLight(const SpotLight& light)
{
    if (mSpotLights.size() < MaxSpotLights)
        mSpotLights.push_back(light);
}

void RenderingSystem::ClearLights()
{
    mDirectionalLights.clear();
    mPointLights.clear();
    mSpotLights.clear();
}

void RenderingSystem::UpdateLightingConstants()
{
    LightingConstants lc = {};

    // Копируем directional lights
    for (size_t i = 0; i < mDirectionalLights.size() && i < MaxDirectionalLights; ++i)
        lc.DirLights[i] = mDirectionalLights[i];

    // Копируем point lights
    for (size_t i = 0; i < mPointLights.size() && i < MaxPointLights; ++i)
        lc.PointLights[i] = mPointLights[i];

    // Копируем spot lights
    for (size_t i = 0; i < mSpotLights.size() && i < MaxSpotLights; ++i)
        lc.SpotLights[i] = mSpotLights[i];

    lc.CameraPosition = mCameraPosition;
    lc.NumDirectionalLights = (int)mDirectionalLights.size();
    lc.NumPointLights = (int)mPointLights.size();
    lc.NumSpotLights = (int)mSpotLights.size();
    lc.ScreenDimensions = XMFLOAT2((float)mWidth, (float)mHeight);
    lc.AmbientLight = mAmbientLight;

    mLightingCB->CopyData(0, lc);
}

void RenderingSystem::BeginGeometryPass(ID3D12GraphicsCommandList* cmdList)
{
    // Переводим GBuffer в состояние render target
    mGBuffer.TransitionToRenderTarget(cmdList);

    // Получаем все RTV
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles[GBuffer::RT_COUNT];
    for (int i = 0; i < GBuffer::RT_COUNT; ++i)
        rtvHandles[i] = mGBuffer.GetRTV((GBuffer::RTIndex)i);

    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = mGBuffer.GetDSV();

    // Очищаем render targets
    const float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    for (int i = 0; i < GBuffer::RT_COUNT; ++i)
        cmdList->ClearRenderTargetView(rtvHandles[i], clearColor, 0, nullptr);

    cmdList->ClearDepthStencilView(dsvHandle,
        D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,
        1.0f, 0, 0, nullptr);

    // Устанавливаем render targets
    cmdList->OMSetRenderTargets(GBuffer::RT_COUNT, rtvHandles, FALSE, &dsvHandle);

    // Устанавливаем viewport и scissor
    D3D12_VIEWPORT viewport = { 0.0f, 0.0f, (float)mWidth, (float)mHeight, 0.0f, 1.0f };
    D3D12_RECT scissorRect = { 0, 0, (LONG)mWidth, (LONG)mHeight };
    cmdList->RSSetViewports(1, &viewport);
    cmdList->RSSetScissorRects(1, &scissorRect);

    // Устанавливаем PSO и root signature для geometry pass
    cmdList->SetPipelineState(mGeometryPassPSO.Get());
    cmdList->SetGraphicsRootSignature(mGeometryPassRootSig.Get());
}

void RenderingSystem::EndGeometryPass(ID3D12GraphicsCommandList* cmdList)
{
    // Переводим GBuffer в состояние shader resource для чтения в lighting pass
    mGBuffer.TransitionToShaderResource(cmdList);
}

void RenderingSystem::ExecuteLightingPass(
    ID3D12GraphicsCommandList* cmdList,
    ID3D12Resource* backBuffer,
    D3D12_CPU_DESCRIPTOR_HANDLE backBufferRTV,
    D3D12_CPU_DESCRIPTOR_HANDLE depthStencilView)
{
    // Обновляем константы освещения
    UpdateLightingConstants();

    // Барьер для back buffer
    cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
        backBuffer,
        D3D12_RESOURCE_STATE_PRESENT,
        D3D12_RESOURCE_STATE_RENDER_TARGET));

    // Очищаем back buffer
    const float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    cmdList->ClearRenderTargetView(backBufferRTV, clearColor, 0, nullptr);

    // Устанавливаем back buffer как render target (без depth - fullscreen quad)
    cmdList->OMSetRenderTargets(1, &backBufferRTV, TRUE, nullptr);

    // Viewport и scissor
    D3D12_VIEWPORT viewport = { 0.0f, 0.0f, (float)mWidth, (float)mHeight, 0.0f, 1.0f };
    D3D12_RECT scissorRect = { 0, 0, (LONG)mWidth, (LONG)mHeight };
    cmdList->RSSetViewports(1, &viewport);
    cmdList->RSSetScissorRects(1, &scissorRect);

    // Устанавливаем PSO и root signature для lighting pass
    cmdList->SetPipelineState(mLightingPassPSO.Get());
    cmdList->SetGraphicsRootSignature(mLightingPassRootSig.Get());

    // Устанавливаем descriptor heap
    ID3D12DescriptorHeap* heaps[] = { mLightingPassHeap.Get() };
    cmdList->SetDescriptorHeaps(1, heaps);

    // Root parameter 0: GBuffer SRVs (descriptor table)
    cmdList->SetGraphicsRootDescriptorTable(0, 
        mLightingPassHeap->GetGPUDescriptorHandleForHeapStart());

    // Root parameter 1: Lighting constants CBV
    CD3DX12_GPU_DESCRIPTOR_HANDLE cbvHandle(
        mLightingPassHeap->GetGPUDescriptorHandleForHeapStart());
    cbvHandle.Offset(GBuffer::RT_COUNT, mCbvSrvUavDescriptorSize);
    cmdList->SetGraphicsRootDescriptorTable(1, cbvHandle);

    // Рисуем fullscreen quad
    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmdList->IASetVertexBuffers(0, 1, &mQuadVBView);
    cmdList->DrawInstanced(6, 1, 0, 0);

    // Барьер для back buffer обратно в present
    cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
        backBuffer,
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_PRESENT));
}

void RenderingSystem::BuildRootSignatures(ID3D12Device* device)
{
    // ==================== Geometry Pass Root Signature ====================
    // Root Parameter 0: CBV для per-object констант (world, worldViewProj)
    // Root Parameter 1: SRV для diffuse texture
    {
        CD3DX12_DESCRIPTOR_RANGE cbvTable;
        cbvTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0); // b0

        CD3DX12_DESCRIPTOR_RANGE srvTable;
        srvTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0); // t0

        CD3DX12_ROOT_PARAMETER slotRootParameter[2];
        slotRootParameter[0].InitAsDescriptorTable(1, &cbvTable);
        slotRootParameter[1].InitAsDescriptorTable(1, &srvTable);

        // Sampler
        D3D12_STATIC_SAMPLER_DESC sampler = {};
        sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        sampler.MipLODBias = 0;
        sampler.MaxAnisotropy = 8;
        sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
        sampler.MinLOD = 0.0f;
        sampler.MaxLOD = D3D12_FLOAT32_MAX;
        sampler.ShaderRegister = 0;
        sampler.RegisterSpace = 0;
        sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(2, slotRootParameter, 1, &sampler,
            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

        ComPtr<ID3DBlob> serialized, error;
        HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc,
            D3D_ROOT_SIGNATURE_VERSION_1, &serialized, &error);
        
        if (error)
            OutputDebugStringA((char*)error->GetBufferPointer());
        ThrowIfFailed(hr);

        ThrowIfFailed(device->CreateRootSignature(0,
            serialized->GetBufferPointer(),
            serialized->GetBufferSize(),
            IID_PPV_ARGS(&mGeometryPassRootSig)));
    }

    // ==================== Lighting Pass Root Signature ====================
    // Root Parameter 0: SRV table для GBuffer (3 текстуры: position, normal, albedo)
    // Root Parameter 1: CBV для lighting constants
    {
        CD3DX12_DESCRIPTOR_RANGE srvTable;
        srvTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, GBuffer::RT_COUNT, 0); // t0, t1, t2

        CD3DX12_DESCRIPTOR_RANGE cbvTable;
        cbvTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0); // b0

        CD3DX12_ROOT_PARAMETER slotRootParameter[2];
        slotRootParameter[0].InitAsDescriptorTable(1, &srvTable);
        slotRootParameter[1].InitAsDescriptorTable(1, &cbvTable);

        // Sampler для GBuffer (point sampling)
        D3D12_STATIC_SAMPLER_DESC sampler = {};
        sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
        sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.MipLODBias = 0;
        sampler.MaxAnisotropy = 1;
        sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
        sampler.MinLOD = 0.0f;
        sampler.MaxLOD = 0.0f;
        sampler.ShaderRegister = 0;
        sampler.RegisterSpace = 0;
        sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(2, slotRootParameter, 1, &sampler,
            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

        ComPtr<ID3DBlob> serialized, error;
        HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc,
            D3D_ROOT_SIGNATURE_VERSION_1, &serialized, &error);
        
        if (error)
            OutputDebugStringA((char*)error->GetBufferPointer());
        ThrowIfFailed(hr);

        ThrowIfFailed(device->CreateRootSignature(0,
            serialized->GetBufferPointer(),
            serialized->GetBufferSize(),
            IID_PPV_ARGS(&mLightingPassRootSig)));
    }
}

void RenderingSystem::BuildShaders()
{
    mGBufferVS = d3dUtil::CompileShader(L"..\\Shaders\\GBufferPass.hlsl", nullptr, "VS", "vs_5_0");
    mGBufferPS = d3dUtil::CompileShader(L"..\\Shaders\\GBufferPass.hlsl", nullptr, "PS", "ps_5_0");
    mLightingVS = d3dUtil::CompileShader(L"..\\Shaders\\LightingPass.hlsl", nullptr, "VS", "vs_5_0");
    mLightingPS = d3dUtil::CompileShader(L"..\\Shaders\\LightingPass.hlsl", nullptr, "PS", "ps_5_0");
}

void RenderingSystem::BuildPSOs(ID3D12Device* device)
{
    // ==================== Geometry Pass PSO ====================
    {
        std::vector<D3D12_INPUT_ELEMENT_DESC> inputLayout = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
              D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12,
              D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24,
              D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            {"TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 32, 
              D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}
        };

        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.InputLayout = { inputLayout.data(), (UINT)inputLayout.size() };
        psoDesc.pRootSignature = mGeometryPassRootSig.Get();
        psoDesc.VS = { mGBufferVS->GetBufferPointer(), mGBufferVS->GetBufferSize() };
        psoDesc.PS = { mGBufferPS->GetBufferPointer(), mGBufferPS->GetBufferSize() };
        psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
        psoDesc.SampleMask = UINT_MAX;
        psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        
        // 3 render targets для GBuffer
        psoDesc.NumRenderTargets = GBuffer::RT_COUNT;
        psoDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT; // Position
        psoDesc.RTVFormats[1] = DXGI_FORMAT_R16G16B16A16_FLOAT; // Normal
        psoDesc.RTVFormats[2] = DXGI_FORMAT_R8G8B8A8_UNORM;     // Albedo
        
        psoDesc.SampleDesc.Count = 1;
        psoDesc.SampleDesc.Quality = 0;
        psoDesc.DSVFormat = mDepthStencilFormat;

        ThrowIfFailed(device->CreateGraphicsPipelineState(&psoDesc, 
            IID_PPV_ARGS(&mGeometryPassPSO)));
    }

    // ==================== Lighting Pass PSO ====================
    {
        std::vector<D3D12_INPUT_ELEMENT_DESC> inputLayout = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
              D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12,
              D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
        };

        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.InputLayout = { inputLayout.data(), (UINT)inputLayout.size() };
        psoDesc.pRootSignature = mLightingPassRootSig.Get();
        psoDesc.VS = { mLightingVS->GetBufferPointer(), mLightingVS->GetBufferSize() };
        psoDesc.PS = { mLightingPS->GetBufferPointer(), mLightingPS->GetBufferSize() };
        psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE; // Fullscreen quad
        psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        
        // Отключаем depth test для fullscreen quad
        psoDesc.DepthStencilState.DepthEnable = FALSE;
        psoDesc.DepthStencilState.StencilEnable = FALSE;
        
        psoDesc.SampleMask = UINT_MAX;
        psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        psoDesc.NumRenderTargets = 1;
        psoDesc.RTVFormats[0] = mBackBufferFormat;
        psoDesc.SampleDesc.Count = 1;
        psoDesc.SampleDesc.Quality = 0;
        psoDesc.DSVFormat = DXGI_FORMAT_UNKNOWN;

        ThrowIfFailed(device->CreateGraphicsPipelineState(&psoDesc, 
            IID_PPV_ARGS(&mLightingPassPSO)));
    }
}

void RenderingSystem::BuildFullscreenQuad(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList)
{
    // Fullscreen quad в NDC координатах
    QuadVertex quadVertices[] = {
        // Triangle 1
        { XMFLOAT3(-1.0f,  1.0f, 0.0f), XMFLOAT2(0.0f, 0.0f) }, // Top-left
        { XMFLOAT3( 1.0f,  1.0f, 0.0f), XMFLOAT2(1.0f, 0.0f) }, // Top-right
        { XMFLOAT3(-1.0f, -1.0f, 0.0f), XMFLOAT2(0.0f, 1.0f) }, // Bottom-left
        // Triangle 2
        { XMFLOAT3( 1.0f,  1.0f, 0.0f), XMFLOAT2(1.0f, 0.0f) }, // Top-right
        { XMFLOAT3( 1.0f, -1.0f, 0.0f), XMFLOAT2(1.0f, 1.0f) }, // Bottom-right
        { XMFLOAT3(-1.0f, -1.0f, 0.0f), XMFLOAT2(0.0f, 1.0f) }, // Bottom-left
    };

    const UINT vbByteSize = sizeof(quadVertices);

    mQuadVB = d3dUtil::CreateDefaultBuffer(device, cmdList,
        quadVertices, vbByteSize, mQuadVBUploader);

    mQuadVBView.BufferLocation = mQuadVB->GetGPUVirtualAddress();
    mQuadVBView.StrideInBytes = sizeof(QuadVertex);
    mQuadVBView.SizeInBytes = vbByteSize;
}

void RenderingSystem::BuildLightingPassDescriptorHeap(ID3D12Device* device)
{
    // Heap layout:
    // [0] Position SRV
    // [1] Normal SRV
    // [2] Albedo SRV
    // [3] Lighting CBV

    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.NumDescriptors = GBuffer::RT_COUNT + 1; // 3 SRVs + 1 CBV
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ThrowIfFailed(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&mLightingPassHeap)));

    CD3DX12_CPU_DESCRIPTOR_HANDLE handle(
        mLightingPassHeap->GetCPUDescriptorHandleForHeapStart());

    // Копируем SRV дескрипторы из GBuffer
    for (int i = 0; i < GBuffer::RT_COUNT; ++i)
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Format = mGBuffer.GetFormat((GBuffer::RTIndex)i);
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Texture2D.MipLevels = 1;
        
        device->CreateShaderResourceView(
            mGBuffer.GetResource((GBuffer::RTIndex)i), 
            &srvDesc, 
            handle);
        
        handle.Offset(1, mCbvSrvUavDescriptorSize);
    }

    // CBV для lighting constants
    D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
    cbvDesc.BufferLocation = mLightingCB->Resource()->GetGPUVirtualAddress();
    cbvDesc.SizeInBytes = d3dUtil::CalcConstantBufferByteSize(sizeof(LightingConstants));
    device->CreateConstantBufferView(&cbvDesc, handle);
}