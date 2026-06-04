Texture2D gBillboardTex : register(t0);
SamplerState gsamLinear : register(s0);

cbuffer cbPerObject : register(b0)
{
    float4x4 gWorld;
    float4x4 gTexTransform;
}

cbuffer cbPass : register(b1)
{
    float4x4 gView;
    float4x4 gInvView;
    float4x4 gProj;
    float4x4 gInvProj;
    float4x4 gViewProj;
    float4x4 gInvViewProj;
    float3 gEyePosW;
    float cbPerObjectPad1;
    float2 gRenderTargetSize;
    float2 gInvRenderTargetSize;
    float gNearZ;
    float gFarZ;
    float gTotalTime;
    float gDeltaTime;
    float4 gAmbientLight;
    float4 gLights[32];
}

struct VertexIn
{
    float3 PosL : POSITION;
    float3 NormalL : NORMAL;
    float2 TexC : TEXCOORD;
};

struct VSOut
{
    float3 PosW : POSITION;
    float2 TexC : TEXCOORD;
};

VSOut VS(VertexIn vin)
{
    VSOut vout;
    float4 posW = mul(float4(vin.PosL, 1.0f), gWorld);
    vout.PosW = posW.xyz;
    vout.TexC = vin.TexC;
    return vout;
}

struct GSOut
{
    float4 PosH : SV_POSITION;
    float2 TexC : TEXCOORD;
    float3 PosW : POSITION;
    float3 NormalW : NORMAL;
};

[maxvertexcount(4)]
void GS(point VSOut input[1], inout TriangleStream<GSOut> triStream)
{
    float3 center = input[0].PosW;
    float size = 1.5f; // Размер билборда
    
    // Векторы камеры из матрицы View
    float3 right = normalize(float3(gView._11, gView._21, gView._31));
    float3 up = normalize(float3(gView._12, gView._22, gView._32));
    
    // 4 угла квадрата
    float3 corners[4];
    corners[0] = center - right * size * 0.5f - up * size * 0.5f;
    corners[1] = center + right * size * 0.5f - up * size * 0.5f;
    corners[2] = center - right * size * 0.5f + up * size * 0.5f;
    corners[3] = center + right * size * 0.5f + up * size * 0.5f;
    
    float2 uvs[4] = { float2(0,1), float2(1,1), float2(0,0), float2(1,0) };
    
    for (int i = 0; i < 4; i++)
    {
        GSOut gout;
        gout.PosH = mul(float4(corners[i], 1.0f), gViewProj);
        gout.TexC = uvs[i];
        gout.PosW = corners[i];
        gout.NormalW = normalize(float3(gView._13, gView._23, gView._33)); // Нормаль = направление камеры
        triStream.Append(gout);
    }
}

struct GBufferOut
{
    float4 Albedo : SV_Target0;
    float4 Normal : SV_Target1;
    float4 Material : SV_Target2;
    float4 Position : SV_Target3;
};

GBufferOut PS(GSOut pin)
{
    GBufferOut gout;
    float4 texColor = gBillboardTex.Sample(gsamLinear, pin.TexC);
    
    // Alpha test
    clip(texColor.a < 0.3f ? -1.0f : 1.0f);
    
    gout.Albedo = texColor;
    gout.Normal = float4(normalize(pin.NormalW) * 0.5f + 0.5f, 1.0f);
    gout.Material = float4(0.05f, 0.05f, 0.05f, 0.3f);
    gout.Position = float4(pin.PosW, 1.0f);
    
    return gout;
}