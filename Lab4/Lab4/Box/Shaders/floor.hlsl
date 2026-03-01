Texture2D gTex0 : register(t0);
Texture2D gTex1 : register(t1);
SamplerState gSampler : register(s0);

cbuffer cbPerObject : register(b0)
{
    float4x4 gWorldViewProj;
    float    gTileCount;   // сколько клеток по каждой оси (например 8)
    float3   _pad;
};

struct VertexIn
{
    float3 PosL : POSITION;
    float2 Tex  : TEXCOORD;
};

struct VertexOut
{
    float4 PosH : SV_POSITION;
    float2 Tex  : TEXCOORD;
};

VertexOut VS(VertexIn vin)
{
    VertexOut vout;
    vout.PosH = mul(float4(vin.PosL, 1.0f), gWorldViewProj);
    vout.Tex = vin.Tex;
    return vout;
}

float4 PS(VertexOut pin) : SV_Target
{
    // Шахматная доска: (floor(u*N) + floor(v*N)) % 2
    float2 scaled = pin.Tex * gTileCount;
    int check = ((int)floor(scaled.x) + (int)floor(scaled.y)) % 2;

    float4 c0 = gTex0.Sample(gSampler, pin.Tex * gTileCount);
    float4 c1 = gTex1.Sample(gSampler, pin.Tex * gTileCount);

    return (check == 0) ? c0 : c1;
}