#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include <vector>
#include <string>
#include <DirectXMath.h>

class HeightMap
{
public:
    HeightMap() = default;
    ~HeightMap() = default;
    // Загрузка карты высот из 16-bit RAW файла
    bool LoadFromRaw(
        const std::wstring& rawFilePath,
        UINT width,
        UINT height,
        float worldSizeX,
        float worldSizeZ,
        float maxHeight);

    // Получить высоту в точке (worldX, worldZ) с билинейной интерполяцией
    float GetHeight(float worldX, float worldZ) const;

    // Получить нормаль в точке (worldX, worldZ)
    DirectX::XMFLOAT3 GetNormal(float worldX, float worldZ) const;

    // Получить высоту из сырых данных (без интерполяции)
    float GetRawHeight(UINT x, UINT z) const;

    // Размеры карты высот
    UINT GetWidth() const { return mWidth; }
    UINT GetHeight() const { return mHeight; }

    // Размеры мира
    float GetWorldSizeX() const { return mWorldSizeX; }
    float GetWorldSizeZ() const { return mWorldSizeZ; }

    // Максимальная высота
    float GetMaxHeight() const { return mMaxHeight; }

    // Проверка загруженности
    bool IsLoaded() const { return !mHeightData.empty(); }

private:
    // Конвертация мировых координат в координаты карты высот
    void WorldToHeightmap(float worldX, float worldZ, float& outU, float& outV) const;

    // Получить индекс в массиве высот
    UINT GetIndex(UINT x, UINT z) const { return z * mWidth + x; }

    std::vector<uint16_t> mHeightData; // Сырые 16-bit высоты [0..65535]
    UINT mWidth = 0;
    UINT mHeight = 0;
    float mWorldSizeX = 0.0f;
    float mWorldSizeZ = 0.0f;
    float mMaxHeight = 0.0f;
};