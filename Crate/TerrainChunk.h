#pragma once
#include "HeightMap.h"
#include "../Common/d3dUtil.h"
#include <DirectXCollision.h>
#include <DirectXMath.h>
#include <memory>
#include <cstdint>

#ifndef VERTEX_DEFINED_TERRAIN_CHUNK
#define VERTEX_DEFINED_TERRAIN_CHUNK
struct Vertex
{
    DirectX::XMFLOAT3 Pos;
    DirectX::XMFLOAT3 Normal;
    DirectX::XMFLOAT2 TexC;

    Vertex() = default;
    Vertex(const DirectX::XMFLOAT3& p, const DirectX::XMFLOAT3& n, const DirectX::XMFLOAT2& uv)
        : Pos(p), Normal(n), TexC(uv) {
    }
};
#endif

class TerrainChunk
{
public:
    TerrainChunk() = default;
    ~TerrainChunk() = default;

    void Initialize(
        const HeightMap* heightMap,
        UINT chunkIndexX,
        UINT chunkIndexZ,
        float chunkSize,
        float worldSize,
        UINT gridResolution);

    void BuildGeometry(
        ID3D12Device* device,
        ID3D12GraphicsCommandList* cmdList);

    // Получить MeshGeometry для рендеринга
    MeshGeometry* GetGeometry() { return mGeometry.get(); }

    // Получить AABB для frustum culling
    const DirectX::BoundingBox& GetBoundingBox() const { return mBoundingBox; }
    const DirectX::XMFLOAT3& GetCenter() const { return mCenter; }

    UINT GetChunkIndexX() const { return mChunkIndexX; }
    UINT GetChunkIndexZ() const { return mChunkIndexZ; }

private:
    DirectX::XMFLOAT3 CalculateVertexPosition(UINT localX, UINT localZ) const;
    DirectX::XMFLOAT3 CalculateVertexNormal(UINT localX, UINT localZ) const;

    // Вычислить AABB после генерации вершин
    void ComputeBoundingBox();

    const HeightMap* mHeightMap = nullptr;
    UINT mChunkIndexX = 0;
    UINT mChunkIndexZ = 0;
    float mChunkSize = 0.0f;
    float mWorldSize = 0.0f;
    UINT mGridResolution = 0;
    DirectX::XMFLOAT3 mCenter = { 0.0f, 0.0f, 0.0f };
    DirectX::BoundingBox mBoundingBox;
    std::unique_ptr<MeshGeometry> mGeometry;
};