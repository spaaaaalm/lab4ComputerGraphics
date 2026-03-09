#pragma once
#include "d3dUtil.h"
#include <wrl/client.h>
#include <array>

using Microsoft::WRL::ComPtr;

class GBuffer
{
public:
    // Индексы render targets в GBuffer
    enum RTIndex
    {
        RT_POSITION = 0,    // World position (RGB) + Depth (A)
        RT_NORMAL = 1,      // World normal (RGB) + Roughness (A)
        RT_ALBEDO = 2,      // Albedo color (RGB) + Metallic (A)
        RT_COUNT
    };

    GBuffer();
    ~GBuffer();

    // Инициализация GBuffer с заданными размерами
    void Initialize(
        ID3D12Device* device,
        UINT width, 
        UINT height,
        DXGI_FORMAT positionFormat = DXGI_FORMAT_R16G16B16A16_FLOAT,
        DXGI_FORMAT normalFormat = DXGI_FORMAT_R16G16B16A16_FLOAT,
        DXGI_FORMAT albedoFormat = DXGI_FORMAT_R8G8B8A8_UNORM
    );

    // Пересоздание при изменении размера окна
    void OnResize(ID3D12Device* device, UINT width, UINT height);

    // Получение ресурсов
    ID3D12Resource* GetResource(RTIndex index) const { return mRenderTargets[index].Get(); }
    ID3D12Resource* GetDepthStencilResource() const { return mDepthStencilBuffer.Get(); }

    // Получение дескрипторов (CPU handles для создания views)
    CD3DX12_CPU_DESCRIPTOR_HANDLE GetRTV(RTIndex index) const;
    CD3DX12_CPU_DESCRIPTOR_HANDLE GetDSV() const;
    CD3DX12_GPU_DESCRIPTOR_HANDLE GetSRV(RTIndex index) const;
    CD3DX12_CPU_DESCRIPTOR_HANDLE GetSRVCpu(RTIndex index) const;

    // Descriptor heaps
    ID3D12DescriptorHeap* GetRTVHeap() const { return mRtvHeap.Get(); }
    ID3D12DescriptorHeap* GetDSVHeap() const { return mDsvHeap.Get(); }
    ID3D12DescriptorHeap* GetSRVHeap() const { return mSrvHeap.Get(); }

    // Размеры
    UINT GetWidth() const { return mWidth; }
    UINT GetHeight() const { return mHeight; }

    // Форматы
    DXGI_FORMAT GetFormat(RTIndex index) const { return mFormats[index]; }
    DXGI_FORMAT GetDepthFormat() const { return mDepthFormat; }

    // Барьеры переходов состояний
    void TransitionToRenderTarget(ID3D12GraphicsCommandList* cmdList);
    void TransitionToShaderResource(ID3D12GraphicsCommandList* cmdList);

private:
    void BuildResources(ID3D12Device* device);
    void BuildDescriptors(ID3D12Device* device);

private:
    UINT mWidth = 0;
    UINT mHeight = 0;

    std::array<DXGI_FORMAT, RT_COUNT> mFormats;
    DXGI_FORMAT mDepthFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;

    // Render target ресурсы
    std::array<ComPtr<ID3D12Resource>, RT_COUNT> mRenderTargets;
    ComPtr<ID3D12Resource> mDepthStencilBuffer;

    // Descriptor heaps
    ComPtr<ID3D12DescriptorHeap> mRtvHeap;
    ComPtr<ID3D12DescriptorHeap> mDsvHeap;
    ComPtr<ID3D12DescriptorHeap> mSrvHeap;  // Для чтения GBuffer в lighting pass

    // Размеры дескрипторов
    UINT mRtvDescriptorSize = 0;
    UINT mDsvDescriptorSize = 0;
    UINT mCbvSrvUavDescriptorSize = 0;

    // Текущее состояние ресурсов
    bool mIsRenderTarget = false;
};