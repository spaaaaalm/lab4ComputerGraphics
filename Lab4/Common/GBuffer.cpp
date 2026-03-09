#include "GBuffer.h"

GBuffer::GBuffer()
{
    mFormats[RT_POSITION] = DXGI_FORMAT_R16G16B16A16_FLOAT;
    mFormats[RT_NORMAL] = DXGI_FORMAT_R16G16B16A16_FLOAT;
    mFormats[RT_ALBEDO] = DXGI_FORMAT_R8G8B8A8_UNORM;
}

GBuffer::~GBuffer() = default;

void GBuffer::Initialize(
    ID3D12Device* device,
    UINT width,
    UINT height,
    DXGI_FORMAT positionFormat,
    DXGI_FORMAT normalFormat,
    DXGI_FORMAT albedoFormat)
{
    mWidth = width;
    mHeight = height;
    mFormats[RT_POSITION] = positionFormat;
    mFormats[RT_NORMAL] = normalFormat;
    mFormats[RT_ALBEDO] = albedoFormat;

    // Получаем размеры дескрипторов
    mRtvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    mDsvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    mCbvSrvUavDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    // Создаём descriptor heaps
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = RT_COUNT;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    ThrowIfFailed(device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&mRtvHeap)));

    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
    dsvHeapDesc.NumDescriptors = 1;
    dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    ThrowIfFailed(device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&mDsvHeap)));

    D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
    srvHeapDesc.NumDescriptors = RT_COUNT;  // SRV для каждого RT
    srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ThrowIfFailed(device->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&mSrvHeap)));

    BuildResources(device);
    BuildDescriptors(device);
}

void GBuffer::OnResize(ID3D12Device* device, UINT width, UINT height)
{
    if (mWidth == width && mHeight == height)
        return;

    mWidth = width;
    mHeight = height;

    // Освобождаем старые ресурсы
    for (auto& rt : mRenderTargets)
        rt.Reset();
    mDepthStencilBuffer.Reset();

    BuildResources(device);
    BuildDescriptors(device);
}

void GBuffer::BuildResources(ID3D12Device* device)
{
    D3D12_CLEAR_VALUE clearValue = {};
    clearValue.Color[0] = 0.0f;
    clearValue.Color[1] = 0.0f;
    clearValue.Color[2] = 0.0f;
    clearValue.Color[3] = 0.0f;

    // Создаём render targets
    for (int i = 0; i < RT_COUNT; ++i)
    {
        D3D12_RESOURCE_DESC texDesc = {};
        texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        texDesc.Width = mWidth;
        texDesc.Height = mHeight;
        texDesc.DepthOrArraySize = 1;
        texDesc.MipLevels = 1;
        texDesc.Format = mFormats[i];
        texDesc.SampleDesc.Count = 1;
        texDesc.SampleDesc.Quality = 0;
        texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

        clearValue.Format = mFormats[i];

        ThrowIfFailed(device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
            D3D12_HEAP_FLAG_NONE,
            &texDesc,
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            &clearValue,
            IID_PPV_ARGS(&mRenderTargets[i])));
    }

    // Создаём depth/stencil buffer
    D3D12_RESOURCE_DESC depthDesc = {};
    depthDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    depthDesc.Width = mWidth;
    depthDesc.Height = mHeight;
    depthDesc.DepthOrArraySize = 1;
    depthDesc.MipLevels = 1;
    depthDesc.Format = DXGI_FORMAT_R24G8_TYPELESS;  // Typeless для использования как DSV и SRV
    depthDesc.SampleDesc.Count = 1;
    depthDesc.SampleDesc.Quality = 0;
    depthDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    depthDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE depthClear = {};
    depthClear.Format = mDepthFormat;
    depthClear.DepthStencil.Depth = 1.0f;
    depthClear.DepthStencil.Stencil = 0;

    ThrowIfFailed(device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &depthDesc,
        D3D12_RESOURCE_STATE_DEPTH_WRITE,
        &depthClear,
        IID_PPV_ARGS(&mDepthStencilBuffer)));

    mIsRenderTarget = true;
}

void GBuffer::BuildDescriptors(ID3D12Device* device)
{
    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(mRtvHeap->GetCPUDescriptorHandleForHeapStart());
    CD3DX12_CPU_DESCRIPTOR_HANDLE srvHandle(mSrvHeap->GetCPUDescriptorHandleForHeapStart());

    // Создаём RTV и SRV для каждого render target
    for (int i = 0; i < RT_COUNT; ++i)
    {
        // RTV
        D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
        rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        rtvDesc.Format = mFormats[i];
        rtvDesc.Texture2D.MipSlice = 0;
        device->CreateRenderTargetView(mRenderTargets[i].Get(), &rtvDesc, rtvHandle);
        rtvHandle.Offset(1, mRtvDescriptorSize);

        // SRV
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Format = mFormats[i];
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Texture2D.MipLevels = 1;
        device->CreateShaderResourceView(mRenderTargets[i].Get(), &srvDesc, srvHandle);
        srvHandle.Offset(1, mCbvSrvUavDescriptorSize);
    }

    // DSV
    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
    dsvDesc.Format = mDepthFormat;
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    dsvDesc.Texture2D.MipSlice = 0;
    device->CreateDepthStencilView(
        mDepthStencilBuffer.Get(), 
        &dsvDesc, 
        mDsvHeap->GetCPUDescriptorHandleForHeapStart());
}

CD3DX12_CPU_DESCRIPTOR_HANDLE GBuffer::GetRTV(RTIndex index) const
{
    CD3DX12_CPU_DESCRIPTOR_HANDLE handle(mRtvHeap->GetCPUDescriptorHandleForHeapStart());
    handle.Offset(index, mRtvDescriptorSize);
    return handle;
}

CD3DX12_CPU_DESCRIPTOR_HANDLE GBuffer::GetDSV() const
{
    return CD3DX12_CPU_DESCRIPTOR_HANDLE(mDsvHeap->GetCPUDescriptorHandleForHeapStart());
}

CD3DX12_GPU_DESCRIPTOR_HANDLE GBuffer::GetSRV(RTIndex index) const
{
    CD3DX12_GPU_DESCRIPTOR_HANDLE handle(mSrvHeap->GetGPUDescriptorHandleForHeapStart());
    handle.Offset(index, mCbvSrvUavDescriptorSize);
    return handle;
}

CD3DX12_CPU_DESCRIPTOR_HANDLE GBuffer::GetSRVCpu(RTIndex index) const
{
    CD3DX12_CPU_DESCRIPTOR_HANDLE handle(mSrvHeap->GetCPUDescriptorHandleForHeapStart());
    handle.Offset(index, mCbvSrvUavDescriptorSize);
    return handle;
}

void GBuffer::TransitionToRenderTarget(ID3D12GraphicsCommandList* cmdList)
{
    if (mIsRenderTarget)
        return;

    std::vector<D3D12_RESOURCE_BARRIER> barriers;
    barriers.reserve(RT_COUNT);

    for (int i = 0; i < RT_COUNT; ++i)
    {
        barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(
            mRenderTargets[i].Get(),
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_STATE_RENDER_TARGET));
    }

    cmdList->ResourceBarrier((UINT)barriers.size(), barriers.data());
    mIsRenderTarget = true;
}

void GBuffer::TransitionToShaderResource(ID3D12GraphicsCommandList* cmdList)
{
    if (!mIsRenderTarget)
        return;

    std::vector<D3D12_RESOURCE_BARRIER> barriers;
    barriers.reserve(RT_COUNT);

    for (int i = 0; i < RT_COUNT; ++i)
    {
        barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(
            mRenderTargets[i].Get(),
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
    }

    cmdList->ResourceBarrier((UINT)barriers.size(), barriers.data());
    mIsRenderTarget = false;
}