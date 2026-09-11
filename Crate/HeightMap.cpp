#include "HeightMap.h"
#include <fstream>
#include <stdexcept>

bool HeightMap::LoadFromRaw(
    const std::wstring& rawFilePath,
    UINT width,
    UINT height,
    float worldSizeX,
    float worldSizeZ,
    float maxHeight)
{
    mWidth = width;
    mHeight = height;
    mWorldSizeX = worldSizeX;
    mWorldSizeZ = worldSizeZ;
    mMaxHeight = maxHeight;

    const size_t totalPixels = (size_t)width * height;
    mHeightData.resize(totalPixels);

    std::ifstream file(rawFilePath, std::ios::binary);
    if (!file.is_open())
    {
        char msg[512];
        sprintf_s(msg, "HeightMap: Failed to open file: %ls\n", rawFilePath.c_str());
        OutputDebugStringA(msg);
        return false;
    }

    file.seekg(0, std::ios::end);
    std::streamsize fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    const size_t expectedSize = totalPixels * sizeof(uint16_t);

    if (fileSize < (std::streamsize)expectedSize)
    {
        char msg[512];
        sprintf_s(msg, "HeightMap: File too small! Expected %zu bytes, got %lld bytes\n",
            expectedSize, (long long)fileSize);
        OutputDebugStringA(msg);
        mHeightData.clear();
        return false;
    }
    if (fileSize > (std::streamsize)expectedSize)
    {
        std::streamsize skip = fileSize - expectedSize;
        if (skip > 0 && skip < 1024) 
        {
            file.seekg(skip, std::ios::beg);
            char msg[256];
            sprintf_s(msg, "HeightMap: Skipping %lld bytes header\n", (long long)skip);
            OutputDebugStringA(msg);
        }
    }

    file.read(reinterpret_cast<char*>(mHeightData.data()), expectedSize);

    if (!file.good())
    {
        char msg[256];
        sprintf_s(msg, "HeightMap: Failed to read data from file: %ls\n", rawFilePath.c_str());
        OutputDebugStringA(msg);
        mHeightData.clear();
        return false;
    }

    file.close();

    bool hasNonZero = false;
    for (size_t i = 0; i < totalPixels; ++i)
    {
        if (mHeightData[i] != 0)
        {
            hasNonZero = true;
            break;
        }
    }

    char msg[512];
    sprintf_s(msg, "HeightMap: Loaded %ux%u heightmap from %ls, world size %.1fx%.1f, max height %.1f, has data: %s\n",
        width, height, rawFilePath.c_str(), worldSizeX, worldSizeZ, maxHeight,
        hasNonZero ? "YES" : "NO (all zeros!)");
    OutputDebugStringA(msg);

    return true;
}

void HeightMap::WorldToHeightmap(float worldX, float worldZ, float& outU, float& outV) const
{
    outU = (worldX + mWorldSizeX * 0.5f) / mWorldSizeX;
    outV = (worldZ + mWorldSizeZ * 0.5f) / mWorldSizeZ;

    // Clamp к границам
    outU = (std::max)(0.0f, (std::min)(1.0f, outU));
    outV = (std::max)(0.0f, (std::min)(1.0f, outV));
}

float HeightMap::GetHeight(float worldX, float worldZ) const
{
    if (mHeightData.empty())
        return 0.0f;

    float u, v;
    WorldToHeightmap(worldX, worldZ, u, v);

    // Конвертируем UV в координаты пикселей
    const float fx = u * (mWidth - 1);
    const float fz = v * (mHeight - 1);

    const UINT x0 = (UINT)(std::max)(0.0f, (std::min)((float)(mWidth - 2), fx));
    const UINT z0 = (UINT)(std::max)(0.0f, (std::min)((float)(mHeight - 2), fz));
    const UINT x1 = x0 + 1;
    const UINT z1 = z0 + 1;

    const float tx = fx - x0;
    const float tz = fz - z0;

    // Получаем 4 значения высот
    const float h00 = GetRawHeight(x0, z0);
    const float h10 = GetRawHeight(x1, z0);
    const float h01 = GetRawHeight(x0, z1);
    const float h11 = GetRawHeight(x1, z1);

    const float h0 = h00 + (h10 - h00) * tx;
    const float h1 = h01 + (h11 - h01) * tx;
    return h0 + (h1 - h0) * tz;
}

float HeightMap::GetRawHeight(UINT x, UINT z) const
{
    if (x >= mWidth || z >= mHeight)
        return 0.0f;

    const uint16_t rawValue = mHeightData[GetIndex(x, z)];

    // Конвертируем [0..65535] в [0..maxHeight]
    return (rawValue / 65535.0f) * mMaxHeight;
}

DirectX::XMFLOAT3 HeightMap::GetNormal(float worldX, float worldZ) const
{
    if (mHeightData.empty())
        return DirectX::XMFLOAT3(0.0f, 1.0f, 0.0f);

    // Размер одного пикселя в мировых координатах
    const float pixelSizeX = mWorldSizeX / (mWidth - 1);
    const float pixelSizeZ = mWorldSizeZ / (mHeight - 1);

    // Получаем высоты соседних точек
    const float hL = GetHeight(worldX - pixelSizeX, worldZ);
    const float hR = GetHeight(worldX + pixelSizeX, worldZ);
    const float hD = GetHeight(worldX, worldZ - pixelSizeZ);
    const float hU = GetHeight(worldX, worldZ + pixelSizeZ);

    // Вычисляем нормаль через векторное произведение
    DirectX::XMVECTOR normal = DirectX::XMVector3Normalize(
        DirectX::XMVectorSet(
            hL - hR,
            2.0f * pixelSizeX,
            hD - hU,
            0.0f
        )
    );

    DirectX::XMFLOAT3 result;
    DirectX::XMStoreFloat3(&result, normal);
    return result;
}