#pragma once
#include "GBuffer.h"
#include "LightSystem.h"
#include "UploadBuffer.h"
#include <memory>
#include <vector>

class RenderingSystem
{
public:
    RenderingSystem();
    ~RenderingSystem();

    void Initialize(
        ID3D12Device* device,
        ID3D12GraphicsCommandList* cmdList,
        UINT width,
        UINT height,
        DXGI_FORMAT backBufferFormat,
        DXGI_FORMAT depthStencilFormat
    );

    void OnResize(ID3D12Device* device, UINT width, UINT height);

    // Установка данных сцены перед рендерингом
    void SetViewProjection(const DirectX::XMMATRIX& view, const DirectX::XMMATRIX& proj);
    void SetCameraPosition(const DirectX::XMFLOAT3& pos);

    // Управление источниками света
    void AddDirectionalLight(const DirectionalLight& light);
    void AddPointLight(const PointLight& light);
    void AddSpotLight(const SpotLight& light);
    void ClearLights();

    // Geometry Pass - рендерит геометрию в GBuffer
    void BeginGeometryPass(ID3D12GraphicsCommandList* cmdList);
    void EndGeometryPass(ID3D12GraphicsCommandList* cmdList);

    // Lighting Pass - вычисляет освещение и выводит в back buffer
    void ExecuteLightingPass(
        ID3D12GraphicsCommandList* cmdList,
        ID3D12Resource* backBuffer,
        D3D12_CPU_DESCRIPTOR_HANDLE backBufferRTV,
        D3D12_CPU_DESCRIPTOR_HANDLE depthStencilView
    );

    // Получение PSO и Root Signature для geometry pass (для внешнего использования)
    ID3D12PipelineState* GetGeometryPassPSO() const { return mGeometryPassPSO.Get(); }
    ID3D12RootSignature* GetGeometryPassRootSignature() const { return mGeometryPassRootSig.Get(); }

    // Получение GBuffer для отладки
    GBuffer* GetGBuffer() { return &mGBuffer; }

    // Обновление константных буферов (вызывать перед рендерингом)
    void UpdateLightingConstants();

    // Descriptor heap для GBuffer SRVs (нужен для SetDescriptorHeaps)
    ID3D12DescriptorHeap* GetGBufferSRVHeap() const { return mGBuffer.GetSRVHeap(); }

    // Объединённый descriptor heap для lighting pass
    ID3D12DescriptorHeap* GetLightingPassHeap() const { return mLightingPassHeap.Get(); }

private:
    void BuildRootSignatures(ID3D12Device* device);
    void BuildShaders();
    void BuildPSOs(ID3D12Device* device);
    void BuildFullscreenQuad(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList);
    void BuildLightingPassDescriptorHeap(ID3D12Device* device);

private:
    GBuffer mGBuffer;

    // Размеры экрана
    UINT mWidth = 0;
    UINT mHeight = 0;
    DXGI_FORMAT mBackBufferFormat;
    DXGI_FORMAT mDepthStencilFormat;

    // Матрицы камеры
    DirectX::XMFLOAT4X4 mView;
    DirectX::XMFLOAT4X4 mProj;
    DirectX::XMFLOAT4X4 mViewProj;
    DirectX::XMFLOAT4X4 mInvViewProj;
    DirectX::XMFLOAT3 mCameraPosition;

    // Источники света
    std::vector<DirectionalLight> mDirectionalLights;
    std::vector<PointLight> mPointLights;
    std::vector<SpotLight> mSpotLights;
    DirectX::XMFLOAT3 mAmbientLight = { 0.1f, 0.1f, 0.1f };

    // Root signatures
    ComPtr<ID3D12RootSignature> mGeometryPassRootSig;
    ComPtr<ID3D12RootSignature> mLightingPassRootSig;

    // Шейдеры
    ComPtr<ID3DBlob> mGBufferVS;
    ComPtr<ID3DBlob> mGBufferPS;
    ComPtr<ID3DBlob> mLightingVS;
    ComPtr<ID3DBlob> mLightingPS;

    // PSOs
    ComPtr<ID3D12PipelineState> mGeometryPassPSO;
    ComPtr<ID3D12PipelineState> mLightingPassPSO;

    // Fullscreen quad для lighting pass
    ComPtr<ID3D12Resource> mQuadVB;
    ComPtr<ID3D12Resource> mQuadVBUploader;
    D3D12_VERTEX_BUFFER_VIEW mQuadVBView;

    // Константные буферы
    std::unique_ptr<UploadBuffer<LightingConstants>> mLightingCB;

    // Descriptor heap для lighting pass (GBuffer SRVs + CBV)
    ComPtr<ID3D12DescriptorHeap> mLightingPassHeap;
    UINT mCbvSrvUavDescriptorSize = 0;

    ID3D12Device* mDevice = nullptr;
};