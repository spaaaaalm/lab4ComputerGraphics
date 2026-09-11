#include "TerrainChunk.h"
#include "../Common/d3dUtil.h"
#include <DirectXMath.h>
#include <vector>
#include <windows.h> 

using namespace DirectX;

void TerrainChunk::Initialize(
    const HeightMap* heightMap,
    UINT chunkIndexX,
    UINT chunkIndexZ,
    float chunkSize,
    float worldSize,
    UINT gridResolution)
{
    mHeightMap = heightMap;
    mChunkIndexX = chunkIndexX;
    mChunkIndexZ = chunkIndexZ;
    mChunkSize = chunkSize;
    mWorldSize = worldSize;
    mGridResolution = gridResolution;

    // Центр чанка в мировых координатах
    const float startX = -mWorldSize * 0.5f + mChunkIndexX * mChunkSize;
    const float startZ = -mWorldSize * 0.5f + mChunkIndexZ * mChunkSize;

    mCenter.x = startX + mChunkSize * 0.5f;
    mCenter.y = 0.0f; // обновится после генерации
    mCenter.z = startZ + mChunkSize * 0.5f;
}

XMFLOAT3 TerrainChunk::CalculateVertexPosition(UINT localX, UINT localZ) const
{
    const float localPosX = (localX / (float)mGridResolution) * mChunkSize;
    const float localPosZ = (localZ / (float)mGridResolution) * mChunkSize;

    const float startX = -mWorldSize * 0.5f + mChunkIndexX * mChunkSize;
    const float startZ = -mWorldSize * 0.5f + mChunkIndexZ * mChunkSize;

    const float worldX = startX + localPosX;
    const float worldZ = startZ + localPosZ;

    const float height = mHeightMap->GetHeight(worldX, worldZ);

    return XMFLOAT3(worldX, height, worldZ);
}

XMFLOAT3 TerrainChunk::CalculateVertexNormal(UINT localX, UINT localZ) const
{
    const float localPosX = (localX / (float)mGridResolution) * mChunkSize;
    const float localPosZ = (localZ / (float)mGridResolution) * mChunkSize;

    const float startX = -mWorldSize * 0.5f + mChunkIndexX * mChunkSize;
    const float startZ = -mWorldSize * 0.5f + mChunkIndexZ * mChunkSize;

    const float worldX = startX + localPosX;
    const float worldZ = startZ + localPosZ;

    return mHeightMap->GetNormal(worldX, worldZ);
}

void TerrainChunk::BuildGeometry(
    ID3D12Device* device,
    ID3D12GraphicsCommandList* cmdList)
{
    if (!mHeightMap)
        return;

    const UINT vertexCount = (mGridResolution + 1) * (mGridResolution + 1);
    const UINT triangleCount = mGridResolution * mGridResolution * 2;
    const UINT indexCount = triangleCount * 3;

    std::vector<Vertex> vertices(vertexCount);

    for (UINT z = 0; z <= mGridResolution; ++z)
    {
        for (UINT x = 0; x <= mGridResolution; ++x)
        {
            const UINT vertexIndex = z * (mGridResolution + 1) + x;

            vertices[vertexIndex].Pos = CalculateVertexPosition(x, z);
            vertices[vertexIndex].Normal = CalculateVertexNormal(x, z);
            vertices[vertexIndex].TexC.x = x / (float)mGridResolution;
            vertices[vertexIndex].TexC.y = z / (float)mGridResolution;
        }
    }

    std::vector<uint32_t> indices(indexCount);
    UINT indexOffset = 0;

    for (UINT z = 0; z < mGridResolution; ++z)
    {
        for (UINT x = 0; x < mGridResolution; ++x)
        {
            const UINT v0 = z * (mGridResolution + 1) + x;
            const UINT v1 = v0 + 1;
            const UINT v2 = (z + 1) * (mGridResolution + 1) + x;
            const UINT v3 = v2 + 1;

            indices[indexOffset++] = v0;
            indices[indexOffset++] = v2;
            indices[indexOffset++] = v1;

            indices[indexOffset++] = v1;
            indices[indexOffset++] = v2;
            indices[indexOffset++] = v3;
        }
    }

    mGeometry = std::make_unique<MeshGeometry>();
    mGeometry->Name = "TerrainChunk_" + std::to_string(mChunkIndexX) + "_" + std::to_string(mChunkIndexZ);

    const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);
    const UINT ibByteSize = (UINT)indices.size() * sizeof(uint32_t);

    ThrowIfFailed(D3DCreateBlob(vbByteSize, &mGeometry->VertexBufferCPU));
    CopyMemory(mGeometry->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

    ThrowIfFailed(D3DCreateBlob(ibByteSize, &mGeometry->IndexBufferCPU));
    CopyMemory(mGeometry->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

    mGeometry->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(
        device, cmdList, vertices.data(), vbByteSize, mGeometry->VertexBufferUploader);

    mGeometry->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(
        device, cmdList, indices.data(), ibByteSize, mGeometry->IndexBufferUploader);

    mGeometry->VertexByteStride = sizeof(Vertex);
    mGeometry->VertexBufferByteSize = vbByteSize;
    mGeometry->IndexFormat = DXGI_FORMAT_R32_UINT;
    mGeometry->IndexBufferByteSize = ibByteSize;

    SubmeshGeometry submesh;
    submesh.IndexCount = (UINT)indices.size();
    submesh.StartIndexLocation = 0;
    submesh.BaseVertexLocation = 0;
    mGeometry->DrawArgs["terrain"] = submesh;

    ComputeBoundingBox();

    char msg[256];
    sprintf_s(msg, "TerrainChunk[%u,%u]: built %u vertices, %u indices, center=(%.1f, %.1f, %.1f)\n",
        mChunkIndexX, mChunkIndexZ, vertexCount, indexCount,
        mCenter.x, mCenter.y, mCenter.z);
    OutputDebugStringA(msg);
}

void TerrainChunk::ComputeBoundingBox()
{
    if (!mGeometry || mGeometry->VertexBufferCPU == nullptr)
        return;

    const Vertex* vertices = reinterpret_cast<const Vertex*>(
        mGeometry->VertexBufferCPU->GetBufferPointer());
    const UINT vertexCount = (UINT)mGeometry->VertexBufferByteSize / sizeof(Vertex);

    if (vertexCount == 0)
        return;

    XMVECTOR vmin = XMLoadFloat3(&vertices[0].Pos);
    XMVECTOR vmax = vmin;

    for (UINT i = 1; i < vertexCount; ++i)
    {
        XMVECTOR pos = XMLoadFloat3(&vertices[i].Pos);
        vmin = XMVectorMin(vmin, pos);
        vmax = XMVectorMax(vmax, pos);
    }

    XMVECTOR center = XMVectorScale(XMVectorAdd(vmin, vmax), 0.5f);
    XMStoreFloat3(&mCenter, center);

    std::vector<XMFLOAT3> points;
    points.reserve(vertexCount);
    for (UINT i = 0; i < vertexCount; ++i)
        points.push_back(vertices[i].Pos);

    BoundingBox::CreateFromPoints(mBoundingBox, points.size(), points.data(), sizeof(XMFLOAT3));
}