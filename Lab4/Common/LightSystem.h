#pragma once
#include <DirectXMath.h>

using namespace DirectX;

// Максимальное количество источников света каждого типа
static const int MaxDirectionalLights = 4;
static const int MaxPointLights = 32;
static const int MaxSpotLights = 32;

struct DirectionalLight
{
    XMFLOAT3 Direction = { 0.0f, -1.0f, 0.0f };
    float Padding1 = 0.0f;
    XMFLOAT3 Color = { 1.0f, 1.0f, 1.0f };
    float Intensity = 1.0f;
};

struct PointLight
{
    XMFLOAT3 Position = { 0.0f, 0.0f, 0.0f };
    float Radius = 10.0f;
    XMFLOAT3 Color = { 1.0f, 1.0f, 1.0f };
    float Intensity = 1.0f;
};

struct SpotLight
{
    XMFLOAT3 Position = { 0.0f, 0.0f, 0.0f };
    float Radius = 10.0f;
    XMFLOAT3 Direction = { 0.0f, -1.0f, 0.0f };
    float SpotAngle = 0.5f;
    XMFLOAT3 Color = { 1.0f, 1.0f, 1.0f };
    float Intensity = 1.0f;
    float InnerAngle = 0.8f;
    XMFLOAT3 Padding = { 0.0f, 0.0f, 0.0f };
};

// Константный буфер для lighting pass
struct LightingConstants
{
    DirectionalLight DirLights[MaxDirectionalLights];
    PointLight PointLights[MaxPointLights];
    SpotLight SpotLights[MaxSpotLights];

    XMFLOAT3 CameraPosition = { 0.0f, 0.0f, 0.0f };
    int NumDirectionalLights = 0;

    int NumPointLights = 0;
    int NumSpotLights = 0;
    XMFLOAT2 ScreenDimensions = { 0.0f, 0.0f };

    XMFLOAT3 AmbientLight = { 0.1f, 0.1f, 0.1f };
    float Padding = 0.0f;
};

// ============================================================
// GBuffer Object Constants - для geometry pass
// ============================================================
struct GBufferObjectConstants
{
    XMFLOAT4X4 World;
    XMFLOAT4X4 WorldViewProj;
    XMFLOAT4X4 WorldInvTranspose;

    GBufferObjectConstants()
    {
        XMMATRIX I = XMMatrixIdentity();
        XMStoreFloat4x4(&World, I);
        XMStoreFloat4x4(&WorldViewProj, I);
        XMStoreFloat4x4(&WorldInvTranspose, I);
    }
};