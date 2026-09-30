#pragma once

#include "../Common/d3dUtil.h"
#include "../Common/UploadBuffer.h"
#include "FrameResource.h"
#include <DirectXCollision.h>
#include <array>

struct TerrainNode
{
    DirectX::BoundingBox Bounds;           
    DirectX::XMFLOAT2 Center = { 0, 0 };    // центр тайла в мировых XZ
    float HalfSize = 0.0f;                  // полуразмер тайла в мира
    int Level = 0;
    std::array<int, 4> Children = { -1, -1, -1, -1 };
    bool HasChildren = false;
};

class TerrainSystem
{
public:
 
    void Initialize(
        ID3D12Device* device,
        ID3D12GraphicsCommandList* cmdList,
        ID3D12DescriptorHeap* srvHeap,
        UINT cbvSrvDescriptorSize,
        UINT srvBaseIndex);     

    void Update(
        const DirectX::XMFLOAT4X4& view,
        const DirectX::XMFLOAT4X4& proj,
        const DirectX::XMFLOAT3& eyePos,
        bool frustumCullEnabled,
        float screenWidth,
        UINT frameIndex);

    // Внутри geometry pass, после DrawRenderItems.
    void Draw(
        ID3D12GraphicsCommandList* cmdList,
        UINT frameIndex,
        D3D12_GPU_VIRTUAL_ADDRESS passCbAddress,
        ID3D12DescriptorHeap* srvHeap,
        UINT cbvSrvDescriptorSize,
        D3D12_GPU_VIRTUAL_ADDRESS materialCbAddress);

    UINT GetDiffuseSrvIndex() const { return mDiffuseSrvIndex; }
    UINT GetHeightSrvIndex() const { return mHeightSrvIndex; }
    UINT GetVisibleTileCount() const { return mVisibleCount; }
    UINT GetCulledTileCount() const { return mCulledCount; }

private:
    bool LoadHeightData();          // mHeightPixels, 2048x2048 R8
    void CreateHeightTexture(ID3D12GraphicsCommandList* cmdList, ID3D12DescriptorHeap* srvHeap, UINT descSize);
    void CreateDiffuseTexture(ID3D12GraphicsCommandList* cmdList, ID3D12DescriptorHeap* srvHeap, UINT descSize);
    void BuildQuadtree();
    void BuildNode(int nodeIndex, int level);
    void BuildGrid(int quads, MeshGeometry& geo, ID3D12GraphicsCommandList* cmdList);


private:
    static constexpr float kWorldSize = 512.0f;     // размер ландшафта в мире (квадрат)
    static constexpr float kBaseY = -3.0f;          
    static constexpr float kHeightScale = 35.0f;    
    static constexpr int kMaxDepth = 5;             // глубина квадродерева 
    static constexpr int kHiGridQuads = 32;         // сетка ближних тайлов 
    static constexpr int kLoGridQuads = 8;          // сетка дальних тайлов
    static constexpr int kHiFromLevel = 3;
    static constexpr int kHeightMapSize = 2048;     
    static constexpr UINT kMaxTiles = 512;

};