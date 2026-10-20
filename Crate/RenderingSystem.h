#pragma once

#include "../Common/d3dUtil.h"
#include "GBuffer.h"
#include "FrameResource.h"

class RenderingSystem
{
public:
    void Initialize(
        ID3D12Device* device,
        UINT width,
        UINT height,
        DXGI_FORMAT backBufferFormat,
        DXGI_FORMAT depthStencilFormat,
        bool msaaEnabled,
        UINT msaaQuality);

    void OnResize(UINT width, UINT height);

    void SetLightingResources(
        ID3D12Resource* shadowMapResource,
        UINT shadowCascadeCount,
        ID3D12Resource* shadowOverlayResource);

    void SetIblResources(
        ID3D12Resource* irradianceMap,
        ID3D12Resource* prefilterEnvMap,
        ID3D12Resource* integrationMap);

    void UpdateLightBufferSrv(
        UINT frameIndex,
        ID3D12Resource* lightBufferResource,
        UINT lightCount,
        UINT lightStrideBytes);

    void BeginGeometryPass(
        ID3D12GraphicsCommandList* cmdList,
        D3D12_CPU_DESCRIPTOR_HANDLE dsv,
        D3D12_GPU_VIRTUAL_ADDRESS passCbAddress,
        D3D12_GPU_DESCRIPTOR_HANDLE checkerTextureHandle,
        bool wireframe);

    void EndGeometryPass(ID3D12GraphicsCommandList* cmdList);

    void ExecuteLightingPass(
        ID3D12GraphicsCommandList* cmdList,
        D3D12_CPU_DESCRIPTOR_HANDLE backBufferRtv,
        D3D12_GPU_VIRTUAL_ADDRESS passCbAddress,
        D3D12_GPU_VIRTUAL_ADDRESS lightParamsCbAddress,
        D3D12_GPU_VIRTUAL_ADDRESS shadowLightingCbAddress,
        UINT frameIndex);

    void BeginTransparentWaterPass(
        ID3D12GraphicsCommandList* cmdList,
        D3D12_CPU_DESCRIPTOR_HANDLE backBufferRtv,
        D3D12_CPU_DESCRIPTOR_HANDLE dsv,
        D3D12_GPU_VIRTUAL_ADDRESS passCbAddress,
        bool wireframe);

    void ExecutePostProcessPasses(
        ID3D12GraphicsCommandList* cmdList,
        ID3D12Resource* backBuffer,
        D3D12_CPU_DESCRIPTOR_HANDLE backBufferRtv,
        D3D12_GPU_VIRTUAL_ADDRESS passCbAddress,
        D3D12_GPU_VIRTUAL_ADDRESS postCbAddress,
        bool enableEdge,
        bool enableVcr,
        D3D12_RESOURCE_STATES& backBufferState);

    ID3D12RootSignature* GetBillboardRootSignature() const { return mBillboardRootSignature.Get(); }
    ID3D12PipelineState* GetBillboardTreePSO() const { return mBillboardTreePSO.Get(); }
    ID3D12PipelineState* GetTreeMeshInstancedPSO() const { return mTreeMeshInstancedPSO.Get(); }
    ID3D12PipelineState* GetGeometrySolidPSO() const { return mGeometrySolidPSO.Get(); }
    void ExecuteTaaPass(ID3D12GraphicsCommandList* cmdList, D3D12_GPU_VIRTUAL_ADDRESS passCbAddress);
    D3D12_CPU_DESCRIPTOR_HANDLE GetTaaCurrentRtv() const;

private:
    void BuildGeometryRootSignature();
    void BuildBillboardRootSignature();
    void BuildLightingRootSignature();
    void BuildPostProcessRootSignature();
    void BuildShadersAndInputLayout();
    void BuildBillboardShadersAndLayout();
    void BuildPSOs();
    void BuildPostProcessResources();
    void CreatePostProcessSrvs();
    void CopyBackBufferToScene(
        ID3D12GraphicsCommandList* cmdList,
        ID3D12Resource* backBuffer,
        D3D12_RESOURCE_STATES& backBufferState);

private:
    ID3D12Device* mDevice = nullptr;

    UINT mWidth = 1;
    UINT mHeight = 1;
    DXGI_FORMAT mBackBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    DXGI_FORMAT mDepthStencilFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    bool mMsaaEnabled = false;
    UINT mMsaaQuality = 0;

    GBuffer mGBuffer;
    ID3D12Resource* mShadowMapForLighting = nullptr;
    UINT mShadowCascadeCountForLighting = 0;
    ID3D12Resource* mShadowOverlayForLighting = nullptr;
    ID3D12Resource* mIrradianceMapForLighting = nullptr;
    ID3D12Resource* mPrefilterEnvMapForLighting = nullptr;
    ID3D12Resource* mIntegrationMapForLighting = nullptr;

    void BuildTaaResolveRootSignature();
    void BuildTaaResources();

    Microsoft::WRL::ComPtr<ID3D12Resource> mTaaCurrent = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource> mTaaHistory = nullptr;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mTaaRtvHeap = nullptr;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> mTaaResolveRootSignature = nullptr;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> mTaaResolvePSO = nullptr;
    UINT mTaaRtvDescriptorSize = 0;
    D3D12_RESOURCE_STATES mTaaCurrentState = D3D12_RESOURCE_STATE_RENDER_TARGET;
    D3D12_RESOURCE_STATES mTaaHistoryState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;


    void CreateLightingSrvs();

    std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID3DBlob>> mShaders;
    std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;
    std::vector<D3D12_INPUT_ELEMENT_DESC> mBillboardInputLayout;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> mGeometryRootSignature = nullptr;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> mBillboardRootSignature = nullptr;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> mLightingRootSignature = nullptr;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> mPostProcessRootSignature = nullptr;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> mGeometryPSO = nullptr;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> mGeometrySolidPSO = nullptr;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> mGeometryWireframePSO = nullptr;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> mBillboardTreePSO = nullptr;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> mTreeMeshInstancedPSO = nullptr;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> mLightingPSO = nullptr;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> mWaterTransparentPSO = nullptr;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> mWaterTransparentWireframePSO = nullptr;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> mEdgePostPSO = nullptr;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> mVcrPostPSO = nullptr;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> mTaaResolvePSO = nullptr;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> mTaaResolveRootSignature = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource> mPostSceneCopy = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource> mPostTempTarget = nullptr;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mPostRtvHeap = nullptr;
    UINT mPostRtvDescriptorSize = 0;
    D3D12_RESOURCE_STATES mPostSceneCopyState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    D3D12_RESOURCE_STATES mPostTempTargetState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
};

void RenderingSystem::BuildTaaResources()
{
    if (!mDevice || mWidth == 0 || mHeight == 0) return;
    mTaaCurrent.Reset(); mTaaHistory.Reset(); mTaaRtvHeap.Reset();

    auto createTex = [&](ComPtr<ID3D12Resource>& outTex, D3D12_RESOURCE_STATE initState) {
        D3D12_RESOURCE_DESC texDesc = {};
        texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        texDesc.Width = mWidth; texDesc.Height = mHeight;
        texDesc.DepthOrArraySize = 1; texDesc.MipLevels = 1;
        texDesc.Format = mBackBufferFormat; // Используем формат бэкбуфера
        texDesc.SampleDesc.Count = 1;
        texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        const float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        CD3DX12_CLEAR_VALUE clearValue(mBackBufferFormat, clearColor);
        CD3DX12_HEAP_PROPERTIES defaultHeap(D3D12_HEAP_TYPE_DEFAULT);
        ThrowIfFailed(mDevice->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &texDesc, initState, &clearValue, IID_PPV_ARGS(&outTex)));
        };

    createTex(mTaaCurrent, D3D12_RESOURCE_STATE_RENDER_TARGET);
    createTex(mTaaHistory, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = 2; rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    ThrowIfFailed(mDevice->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&mTaaRtvHeap)));
    mTaaRtvDescriptorSize = mDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(mTaaRtvHeap->GetCPUDescriptorHandleForHeapStart());
    mDevice->CreateRenderTargetView(mTaaCurrent.Get(), nullptr, rtvHandle);
    rtvHandle.Offset(1, mTaaRtvDescriptorSize);
    mDevice->CreateRenderTargetView(mTaaHistory.Get(), nullptr, rtvHandle);
}

D3D12_CPU_DESCRIPTOR_HANDLE RenderingSystem::GetTaaCurrentRtv() const
{
    return mTaaRtvHeap->GetCPUDescriptorHandleForHeapStart();
}

void RenderingSystem::ExecuteTaaPass(ID3D12GraphicsCommandList* cmdList, D3D12_GPU_VIRTUAL_ADDRESS passCbAddress)
{
    // 1. mTaaCurrent был RenderTarget, переводим в SRV для чтения
    auto bar1 = CD3DX12_RESOURCE_BARRIER::Transition(mTaaCurrent.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    cmdList->ResourceBarrier(1, &bar1);

    // 2. mTaaHistory был SRV, переводим в RenderTarget для записи
    auto bar2 = CD3DX12_RESOURCE_BARRIER::Transition(mTaaHistory.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
    cmdList->ResourceBarrier(1, &bar2);

    // 3. Настройка пайплайна
    D3D12_VIEWPORT viewport = { 0.0f, 0.0f, (float)mWidth, (float)mHeight, 0.0f, 1.0f };
    D3D12_RECT scissor = { 0, 0, (LONG)mWidth, (LONG)mHeight };
    cmdList->RSSetViewports(1, &viewport); cmdList->RSSetScissorRects(1, &scissor);

    ID3D12DescriptorHeap* heaps[] = { mGBuffer.GetSrvHeap() };
    cmdList->SetDescriptorHeaps(1, heaps);
    cmdList->SetGraphicsRootSignature(mTaaResolveRootSignature.Get());
    cmdList->SetPipelineState(mTaaResolvePSO.Get());
    cmdList->SetGraphicsRootConstantBufferView(3, passCbAddress);

    // SRV: t0=Current (mTaaCurrent), t1=History (mTaaHistory), t2=Velocity (GBuffer 4)
    // Но так как mTaaCurrent и mTaaHistory не в общем heap, мы используем хак с копированием 
    // или просто создадим SRV для них в GBuffer heap. 
    // Чтобы не ломать GBuffer, давай создадим SRV для mTaaCurrent и mTaaHistory прямо здесь во временные слоты, 
    // НО проще: используем PostSceneSrvIndex и PostTempSrvIndex, пересоздав их.

    // Пересоздаем SRV для TAA текстур в свободных слотах GBuffer (PostScene и PostTemp)
    UINT srvDescSize = mDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = mBackBufferFormat; srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D; srvDesc.Texture2D.MipLevels = 1;

    mDevice->CreateShaderResourceView(mTaaCurrent.Get(), &srvDesc, mGBuffer.GetSrvCpu(GBuffer::PostTempSrvIndex));
    mDevice->CreateShaderResourceView(mTaaHistory.Get(), &srvDesc, mGBuffer.GetSrvCpu(GBuffer::PostSceneSrvIndex));

    cmdList->SetGraphicsRootDescriptorTable(0, mGBuffer.GetSrvGpu(GBuffer::PostTempSrvIndex));  // Current
    cmdList->SetGraphicsRootDescriptorTable(1, mGBuffer.GetSrvGpu(GBuffer::PostSceneSrvIndex)); // History
    cmdList->SetGraphicsRootDescriptorTable(2, mGBuffer.GetSrvGpu(4)); // Velocity

    // RTV: mTaaHistory (второй дескриптор в mTaaRtvHeap)
    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(mTaaRtvHeap->GetCPUDescriptorHandleForHeapStart());
    rtvHandle.Offset(1, mTaaRtvDescriptorSize);
    cmdList->OMSetRenderTargets(1, &rtvHandle, false, nullptr);

    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmdList->DrawInstanced(3, 1, 0, 0);

    // 4. Возвращаем состояния
    auto bar3 = CD3DX12_RESOURCE_BARRIER::Transition(mTaaCurrent.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
    cmdList->ResourceBarrier(1, &bar3);
    auto bar4 = CD3DX12_RESOURCE_BARRIER::Transition(mTaaHistory.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    cmdList->ResourceBarrier(1, &bar4);

    // 5. Копируем результат (mTaaHistory) в mPostSceneCopy, чтобы постпроцесс его подхватил
    auto toCopyDest = CD3DX12_RESOURCE_BARRIER::Transition(mPostSceneCopy.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
    auto toCopySrc = CD3DX12_RESOURCE_BARRIER::Transition(mTaaHistory.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_RESOURCE_BARRIER copyBars[2] = { toCopyDest, toCopySrc };
    cmdList->ResourceBarrier(2, copyBars);

    cmdList->CopyResource(mPostSceneCopy.Get(), mTaaHistory.Get());

    auto backToSrv = CD3DX12_RESOURCE_BARRIER::Transition(mPostSceneCopy.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    auto backToRt = CD3DX12_RESOURCE_BARRIER::Transition(mTaaHistory.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    D3D12_RESOURCE_BARRIER backBars[2] = { backToSrv, backToRt };
    cmdList->ResourceBarrier(2, backBars);

    mPostSceneCopyState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
}