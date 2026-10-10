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

    void UpdateBrush(
        float screenX, float screenY,
        float screenWidth, float screenHeight,
        const DirectX::XMFLOAT4X4& view,
        const DirectX::XMFLOAT4X4& proj,
        const DirectX::XMFLOAT3& eyePos,
        bool isActive);


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
    void ComputeNodeHeightRange(const DirectX::XMFLOAT2& center, float halfSize, float& outMin, float& outMax) const;
    void SelectNode(int nodeIndex, const DirectX::BoundingFrustum& fr, const DirectX::XMMATRIX& view,
        const DirectX::XMVECTOR& eye, float tanHalfFov, float screenWidth, bool cull);
    void EmitTile(const TerrainNode& node);

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
    static constexpr float kTauPx = 16.0f;          
    static constexpr float kEpsFactor = 2.0f;       

    ID3D12Device* mDevice = nullptr;
    UINT mDiffuseSrvIndex = 0;
    UINT mHeightSrvIndex = 0;

    std::vector<uint8_t> mHeightPixels;             // CPU копия heightmap
    Microsoft::WRL::ComPtr<ID3D12Resource> mHeightTex;
    Microsoft::WRL::ComPtr<ID3D12Resource> mHeightUpload;
    Microsoft::WRL::ComPtr<ID3D12Resource> mDiffuseTex;
    Microsoft::WRL::ComPtr<ID3D12Resource> mDiffuseUpload;

    std::vector<TerrainNode> mNodes;
    MeshGeometry mGridHi;
    MeshGeometry mGridLo;

    std::vector<uint8_t> mHeightPixelsA;    
    std::vector<uint8_t> mHeightPixelsB;    // шумовая карта замещения
    std::vector<uint8_t> mBlendMask;        // маска кисти: 0=A, 255=B
    bool mHeightmapDirty = false;

    void GenerateNoiseHeightMap();
    void RebuildFinalHeightmap();
    void UpdateHeightTexture(ID3D12GraphicsCommandList* cmdList);


    struct TileDraw
    {
        ObjectConstants Constants;
        bool UseHiGrid = false;
    };
    std::vector<TileDraw> mSelection;
    std::unique_ptr<UploadBuffer<ObjectConstants>> mTileCB[gNumFrameResources];

    UINT mVisibleCount = 0;
    UINT mCulledCount = 0;
};